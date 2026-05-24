// =============================================================================
// processor.cpp  –  Thread-pool chunk consumer / aggregator
//
// Usage: ./processor <fifo_path> <num_threads> <queue_size> <shm_name> <sem_name>
//
// Responsibilities:
//   1. Open the named FIFO for reading.
//   2. Launch a reader thread: pulls chunks from FIFO into bounded queue.
//   3. Launch N worker threads: dequeue chunks, parse CSV rows,
//      update per-user aggregation table (protected by mutex).
//   4. When reader gets EOF chunk it enqueues N poison pills so workers exit.
//   5. Serialise aggregation table into POSIX shared memory.
//   6. sem_post() named semaphore so reporter knows data is ready.
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define MAX_WORKERS     32
#define MAX_USERS       20000
#define SHM_USER_ID_LEN 64
#define SHM_RECORD_SIZE (SHM_USER_ID_LEN + sizeof(long long) + sizeof(int) + sizeof(int))

// ---------------------------------------------------------------------------
// Shared chunk header  (must match ingester.cpp exactly)
// ---------------------------------------------------------------------------
struct ChunkHeader {
    int chunk_id;
    int byte_count;
    int source_file_id;
};

// ---------------------------------------------------------------------------
// One item in the bounded queue
// is_poison != 0  →  worker must exit
// ---------------------------------------------------------------------------
struct QueueItem {
    int   is_poison;
    int   chunk_id;
    int   source_file_id;
    char *data;          // malloc'd; worker must free()
    int   byte_count;
};

// ---------------------------------------------------------------------------
// Per-user aggregation record stored in a flat open-address hash table
// ---------------------------------------------------------------------------
struct UserStats {
    char      user_id[SHM_USER_ID_LEN];
    long long total_session_length;
    int       total_sessions;
    int       bounce_sessions;
    int       in_use;            // 0 = empty slot
};

// ---------------------------------------------------------------------------
// Globals shared between threads
// ---------------------------------------------------------------------------
static int              g_queue_size  = 0;
static struct QueueItem *g_queue      = NULL;
static int              g_head        = 0;   // producer writes here
static int              g_tail        = 0;   // consumer reads here

static sem_t            g_sem_empty;          // free slots count
static sem_t            g_sem_full;           // filled slots count
static pthread_mutex_t  g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct UserStats g_agg_table[MAX_USERS];
static int              g_user_count  = 0;
static pthread_mutex_t  g_agg_mutex   = PTHREAD_MUTEX_INITIALIZER;

static int              g_fifo_fd     = -1;
static int              g_num_workers = 0;

static volatile sig_atomic_t g_stop   = 0;

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------
static void HANDLE_SIGNAL(int sig) {
    (void)sig;
    g_stop = 1;
}

static void INSTALL_HANDLER(void) {
    struct sigaction sa;
    sa.sa_handler = HANDLE_SIGNAL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
}

// ---------------------------------------------------------------------------
// Simple djb2 hash for user_id string
// ---------------------------------------------------------------------------
static unsigned int HASH_STR(const char *s) {
    unsigned int h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)(*s);
        s++;
    }
    return h % MAX_USERS;
}

// ---------------------------------------------------------------------------
// Find or insert a UserStats slot for user_id.
// MUST be called with g_agg_mutex held.
// ---------------------------------------------------------------------------
static struct UserStats *FIND_USER(const char *user_id) {
    unsigned int idx = HASH_STR(user_id);
    int tries = 0;

    while (tries < MAX_USERS) {
        if (!g_agg_table[idx].in_use) {
            // Empty slot — insert new entry
            strncpy(g_agg_table[idx].user_id, user_id, SHM_USER_ID_LEN - 1);
            g_agg_table[idx].user_id[SHM_USER_ID_LEN - 1] = '\0';
            g_agg_table[idx].in_use                = 1;
            g_agg_table[idx].total_session_length  = 0;
            g_agg_table[idx].total_sessions        = 0;
            g_agg_table[idx].bounce_sessions       = 0;
            g_user_count++;
            return &g_agg_table[idx];
        }
        if (strncmp(g_agg_table[idx].user_id, user_id, SHM_USER_ID_LEN) == 0) {
            return &g_agg_table[idx];
        }
        idx = (idx + 1) % MAX_USERS;
        tries++;
    }
    return NULL; // table full
}

// ---------------------------------------------------------------------------
// Parse one CSV line and update the aggregation table.
// CSV columns: user_id, url, session_id, timestamp, page_views,
//              session_length_seconds, is_bounce
// ---------------------------------------------------------------------------
static void PROCESS_ROW(char *line) {
    char *user_id        = strtok(line, ",");
                           strtok(NULL, ","); // url
                           strtok(NULL, ","); // session_id
                           strtok(NULL, ","); // timestamp
                           strtok(NULL, ","); // page_views
    char *sess_len_str   = strtok(NULL, ",");
    char *is_bounce_str  = strtok(NULL, ",");

    if (!user_id || !sess_len_str || !is_bounce_str) return;
    if (strcmp(user_id, "user_id") == 0) return;   // skip CSV header row

    long long sess_len  = atoll(sess_len_str);
    int       is_bounce = atoi(is_bounce_str);

    pthread_mutex_lock(&g_agg_mutex);
    struct UserStats *s = FIND_USER(user_id);
    if (s != NULL) {
        s->total_session_length += sess_len;
        s->total_sessions++;
        if (is_bounce) s->bounce_sessions++;
    }
    pthread_mutex_unlock(&g_agg_mutex);
}

// ---------------------------------------------------------------------------
// Worker thread: dequeue items, split into lines, call PROCESS_ROW.
// Exits when it receives a poison pill.
// ---------------------------------------------------------------------------
static void *WORKER_THREAD(void *arg) {
    int thread_id = *((int *)arg);
    free(arg);

    printf("[PROCESSOR][Worker-%d] PID=%d started.\n", thread_id, getpid());
    fflush(stdout);

    while (1) {
        sem_wait(&g_sem_full);

        pthread_mutex_lock(&g_queue_mutex);
        struct QueueItem item = g_queue[g_tail];
        g_tail = (g_tail + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);

        sem_post(&g_sem_empty);

        if (item.is_poison) {
            printf("[PROCESSOR][Worker-%d] poison pill – exiting.\n", thread_id);
            fflush(stdout);
            break;
        }

        // Walk the data buffer line by line (split on '\n')
        char *p   = item.data;
        char *end = item.data + item.byte_count;
        int   lines = 0;

        while (p < end) {
            char *nl       = (char *)memchr(p, '\n', (size_t)(end - p));
            char *line_end = nl ? nl : end;
            int   line_len = (int)(line_end - p);

            if (line_len > 0) {
                char line_copy[512];
                int  copy_len = (line_len < 511) ? line_len : 511;
                memcpy(line_copy, p, copy_len);
                line_copy[copy_len] = '\0';
                PROCESS_ROW(line_copy);
                lines++;
            }
            p = nl ? nl + 1 : end;
        }

        printf("[PROCESSOR][Worker-%d] processed chunk %d (%d bytes, %d lines)\n",
               thread_id, item.chunk_id, item.byte_count, lines);
        fflush(stdout);

        free(item.data);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Reader thread: pull raw chunks from FIFO, enqueue for workers.
// When EOF chunk (chunk_id == -1) arrives, enqueue poison pills.
// ---------------------------------------------------------------------------
static void *READER_THREAD(void *arg) {
    (void)arg;
    printf("[PROCESSOR][Reader] PID=%d reading FIFO.\n", getpid());
    fflush(stdout);

    while (!g_stop) {
        struct ChunkHeader hdr;
        ssize_t total = 0;

        // Read full header (handle partial reads)
        while (total < (ssize_t)sizeof(struct ChunkHeader)) {
            ssize_t n = read(g_fifo_fd,
                             (char *)&hdr + total,
                             sizeof(struct ChunkHeader) - (size_t)total);
            if (n <= 0) {
                fprintf(stderr, "[PROCESSOR][Reader] FIFO closed or error.\n");
                goto enqueue_poisons;
            }
            total += n;
        }

        if (hdr.chunk_id == -1) {
            printf("[PROCESSOR][Reader] EOF chunk received.\n");
            fflush(stdout);
            break;
        }

        // Read chunk data
        char *data = (char *)malloc((size_t)hdr.byte_count + 1);
        if (!data) {
            fprintf(stderr, "[PROCESSOR][Reader] malloc failed.\n");
            goto enqueue_poisons;
        }

        total = 0;
        while (total < hdr.byte_count) {
            ssize_t n = read(g_fifo_fd,
                             data + total,
                             (size_t)(hdr.byte_count - total));
            if (n <= 0) {
                fprintf(stderr, "[PROCESSOR][Reader] data read error.\n");
                free(data);
                goto enqueue_poisons;
            }
            total += n;
        }
        data[hdr.byte_count] = '\0';

        printf("[PROCESSOR][Reader] chunk %d from file %d (%d bytes)\n",
               hdr.chunk_id, hdr.source_file_id, hdr.byte_count);
        fflush(stdout);

        // Enqueue into bounded buffer
        struct QueueItem item;
        item.is_poison      = 0;
        item.chunk_id       = hdr.chunk_id;
        item.source_file_id = hdr.source_file_id;
        item.data           = data;
        item.byte_count     = hdr.byte_count;

        sem_wait(&g_sem_empty);
        pthread_mutex_lock(&g_queue_mutex);
        g_queue[g_head] = item;
        g_head = (g_head + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);
        sem_post(&g_sem_full);
    }

enqueue_poisons:
    // One poison pill per worker so every worker exits cleanly
    for (int i = 0; i < g_num_workers; i++) {
        struct QueueItem poison;
        poison.is_poison      = 1;
        poison.chunk_id       = -1;
        poison.source_file_id = -1;
        poison.data           = NULL;
        poison.byte_count     = 0;

        sem_wait(&g_sem_empty);
        pthread_mutex_lock(&g_queue_mutex);
        g_queue[g_head] = poison;
        g_head = (g_head + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);
        sem_post(&g_sem_full);
    }

    printf("[PROCESSOR][Reader] enqueued %d poison pills – reader done.\n",
           g_num_workers);
    fflush(stdout);
    return NULL;
}

// ---------------------------------------------------------------------------
// Serialise aggregation table into POSIX shared memory.
//
// Layout: [int record_count]
//         [record_count × { char[64] user_id,
//                           long long total_session_length,
//                           int total_sessions,
//                           int bounce_sessions }]
// ---------------------------------------------------------------------------
static int WRITE_SHM(const char *shm_name) {
    int    record_count = g_user_count;
    size_t shm_size     = sizeof(int) + (size_t)record_count * SHM_RECORD_SIZE;

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("[PROCESSOR] shm_open"); return 0; }

    if (ftruncate(shm_fd, (off_t)shm_size) < 0) {
        perror("[PROCESSOR] ftruncate");
        close(shm_fd);
        return 0;
    }

    void *shm_ptr = mmap(NULL, shm_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (shm_ptr == MAP_FAILED) { perror("[PROCESSOR] mmap"); return 0; }

    char *ptr = (char *)shm_ptr;
    memcpy(ptr, &record_count, sizeof(int));
    ptr += sizeof(int);

    for (int i = 0; i < MAX_USERS; i++) {
        if (!g_agg_table[i].in_use) continue;

        memset(ptr, 0, SHM_USER_ID_LEN);
        strncpy(ptr, g_agg_table[i].user_id, SHM_USER_ID_LEN - 1);
        ptr += SHM_USER_ID_LEN;

        memcpy(ptr, &g_agg_table[i].total_session_length, sizeof(long long));
        ptr += sizeof(long long);

        memcpy(ptr, &g_agg_table[i].total_sessions,  sizeof(int));
        ptr += sizeof(int);

        memcpy(ptr, &g_agg_table[i].bounce_sessions, sizeof(int));
        ptr += sizeof(int);
    }

    munmap(shm_ptr, shm_size);
    printf("[PROCESSOR] Wrote %d records to SHM '%s'\n",
           record_count, shm_name);
    fflush(stdout);
    return 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    printf("[PROCESSOR] PID:  %d\n", getpid());
    printf("[PROCESSOR] PPID: %d\n", getppid());

    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <fifo_path> <num_threads> <queue_size>"
                " <shm_name> <sem_name>\n", argv[0]);
        return 10;
    }

    const char *fifo_path = argv[1];
    g_num_workers         = atoi(argv[2]);
    g_queue_size          = atoi(argv[3]);
    const char *shm_name  = argv[4];
    const char *sem_name  = argv[5];

    if (g_num_workers <= 0 || g_num_workers > MAX_WORKERS) {
        fprintf(stderr, "[PROCESSOR] num_threads must be 1-%d\n", MAX_WORKERS);
        return 10;
    }
    if (g_queue_size <= 0) {
        fprintf(stderr, "[PROCESSOR] queue_size must be > 0\n");
        return 10;
    }

    INSTALL_HANDLER();
    memset(g_agg_table, 0, sizeof(g_agg_table));

    // ── Open FIFO for reading ────────────────────────────────────────────────
    g_fifo_fd = open(fifo_path, O_RDONLY);
    if (g_fifo_fd < 0) {
        perror("[PROCESSOR] open FIFO");
        return 20;
    }
    printf("[PROCESSOR] FIFO '%s' opened.\n", fifo_path);

    // ── Init bounded queue ──────────────────────────────────────────────────
    g_queue = (struct QueueItem *)malloc(
                  (size_t)g_queue_size * sizeof(struct QueueItem));
    if (!g_queue) { perror("[PROCESSOR] malloc queue"); return 20; }

    if (sem_init(&g_sem_empty, 0, (unsigned)g_queue_size) != 0) {
        perror("[PROCESSOR] sem_init empty"); return 20;
    }
    if (sem_init(&g_sem_full, 0, 0) != 0) {
        perror("[PROCESSOR] sem_init full"); return 20;
    }

    // ── Create worker threads ────────────────────────────────────────────────
    pthread_t workers[MAX_WORKERS];
    for (int i = 0; i < g_num_workers; i++) {
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        if (pthread_create(&workers[i], NULL, WORKER_THREAD, id) != 0) {
            perror("[PROCESSOR] pthread_create worker");
            return 20;
        }
    }

    // ── Create reader thread ─────────────────────────────────────────────────
    pthread_t reader;
    if (pthread_create(&reader, NULL, READER_THREAD, NULL) != 0) {
        perror("[PROCESSOR] pthread_create reader");
        return 20;
    }

    // ── Wait for all threads ─────────────────────────────────────────────────
    pthread_join(reader, NULL);
    printf("[PROCESSOR] Reader joined.\n");

    for (int i = 0; i < g_num_workers; i++)
        pthread_join(workers[i], NULL);
    printf("[PROCESSOR] All workers joined.\n");

    // ── Write shared memory ──────────────────────────────────────────────────
    if (!WRITE_SHM(shm_name)) return 20;

    // ── Signal reporter via named semaphore ──────────────────────────────────
    sem_t *done_sem = sem_open(sem_name, O_CREAT, 0666, 0);
    if (done_sem == SEM_FAILED) {
        perror("[PROCESSOR] sem_open");
        return 20;
    }
    sem_post(done_sem);
    sem_close(done_sem);
    printf("[PROCESSOR] Posted semaphore '%s' – reporter can read SHM.\n",
           sem_name);

    // ── Cleanup ──────────────────────────────────────────────────────────────
    sem_destroy(&g_sem_empty);
    sem_destroy(&g_sem_full);
    pthread_mutex_destroy(&g_queue_mutex);
    pthread_mutex_destroy(&g_agg_mutex);
    free(g_queue);
    close(g_fifo_fd);

    printf("[PROCESSOR] PID=%d finished.\n", getpid());
    return 0;
}
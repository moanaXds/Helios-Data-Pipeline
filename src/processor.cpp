// =============================================================================
// processor.cpp  –  Web-Clickstream variant
//
// Usage: ./processor <fifo_path> <num_threads N> <queue_size Q>
//                    <shm_name> <sem_name>
//
// What this file does:
//   1. Opens the named FIFO for reading.
//   2. Launches a dedicated "reader thread" that pulls raw chunks out of the
//      FIFO and enqueues them in a bounded buffer.
//   3. Launches N worker threads that dequeue chunks, parse CSV rows, and
//      update a shared aggregation table (per-user session stats).
//   4. Uses two POSIX semaphores (sem_empty / sem_full) for the bounded buffer
//      and a mutex to protect the aggregation table.
//   5. When the EOF chunk arrives the reader enqueues N poison pills so every
//      worker exits cleanly; the main thread then pthread_join()s all workers.
//   6. Serialises the aggregation table into a POSIX shared-memory segment.
//   7. sem_post()s a named semaphore so the reporter knows the data is ready.
// =============================================================================

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <csignal>

using namespace std;

// ---------------------------------------------------------------------------
// Shared chunk header (must match ingester.cpp exactly)
// ---------------------------------------------------------------------------
struct ChunkHeader {
    int chunk_id;
    int byte_count;
    int source_file_id;
};

// ---------------------------------------------------------------------------
// One item in the bounded queue.
// is_poison == true  →  worker should exit.
// ---------------------------------------------------------------------------
struct QueueItem {
    bool   is_poison;
    int    chunk_id;
    int    source_file_id;
    char  *data;        // heap-allocated; worker must delete[] it
    int    byte_count;
};

// ---------------------------------------------------------------------------
// Per-user aggregation record (Web Clickstream variant)
// ---------------------------------------------------------------------------
struct UserStats {
    long long total_session_length;   // sum of session_length_seconds
    int       total_sessions;         // number of sessions seen
    int       bounce_sessions;        // sessions where is_bounce == 1
};

// ---------------------------------------------------------------------------
// Globals shared between threads
// ---------------------------------------------------------------------------
static int              g_queue_size   = 0;
static QueueItem       *g_queue        = nullptr;   // ring buffer
static int              g_head         = 0;         // producer writes here
static int              g_tail         = 0;         // consumer reads here

static sem_t            g_sem_empty;                // counts free slots
static sem_t            g_sem_full;                 // counts filled slots
static pthread_mutex_t  g_queue_mutex  = PTHREAD_MUTEX_INITIALIZER;

// Aggregation table + its mutex
static unordered_map<string, UserStats> g_agg_table;
static pthread_mutex_t  g_agg_mutex    = PTHREAD_MUTEX_INITIALIZER;

// FIFO file descriptor (opened by main, read by reader thread)
static int g_fifo_fd = -1;

// How many worker threads
static int g_num_workers = 0;

// ---------------------------------------------------------------------------
// Shared-memory layout
//
// The shared-memory segment starts with a 4-byte count of records, then
// each record is laid out as:
//
//   char user_id[64]
//   long long total_session_length
//   int   total_sessions
//   int   bounce_sessions
//
// This is a simple, portable layout the reporter can read without needing
// the same header file.
// ---------------------------------------------------------------------------
#define SHM_USER_ID_LEN   64
#define SHM_RECORD_SIZE   (SHM_USER_ID_LEN + sizeof(long long) + sizeof(int) + sizeof(int))

// ---------------------------------------------------------------------------
// Helper: parse one CSV line and update the aggregation table.
//
// CSV format (Web Clickstream):
//   user_id, url, session_id, timestamp, page_views,
//   session_length_seconds, is_bounce
//
// We skip the header line (starts with "user_id").
// ---------------------------------------------------------------------------
static void process_row(const string &line) {
    // Work on a mutable copy for strtok
    char *buf = new char[line.size() + 1];
    strcpy(buf, line.c_str());

    char *user_id               = strtok(buf,  ",");
    /* url        */               strtok(NULL, ",");
    /* session_id */               strtok(NULL, ",");
    /* timestamp  */               strtok(NULL, ",");
    /* page_views */               strtok(NULL, ",");
    char *session_length_str    = strtok(NULL, ",");
    char *is_bounce_str         = strtok(NULL, ",");

    // Skip malformed lines or the CSV header
    if (!user_id || !session_length_str || !is_bounce_str) {
        delete[] buf;
        return;
    }
    if (strcmp(user_id, "user_id") == 0) {   // header row
        delete[] buf;
        return;
    }

    long long session_length = atoll(session_length_str);
    int       is_bounce      = atoi(is_bounce_str);

    // Update aggregation table (guarded by mutex)
    pthread_mutex_lock(&g_agg_mutex);
    UserStats &s = g_agg_table[string(user_id)];
    s.total_session_length += session_length;
    s.total_sessions++;
    if (is_bounce) s.bounce_sessions++;
    pthread_mutex_unlock(&g_agg_mutex);

    delete[] buf;
}

// ---------------------------------------------------------------------------
// Worker thread function
// Dequeues items, parses rows, calls process_row() for each line.
// Exits when it dequeues a poison pill.
// ---------------------------------------------------------------------------
static void *worker_thread(void *arg) {
    int thread_id = *((int *)arg);
    delete (int *)arg;

    cout << "[PROCESSOR][Worker-" << thread_id
         << "] PID=" << getpid()
         << " started." << endl;

    while (true) {
        // Wait until there is something in the queue
        sem_wait(&g_sem_full);

        pthread_mutex_lock(&g_queue_mutex);
        QueueItem item = g_queue[g_tail];
        g_tail = (g_tail + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);

        // Signal that one slot is now free
        sem_post(&g_sem_empty);

        if (item.is_poison) {
            cout << "[PROCESSOR][Worker-" << thread_id
                 << "] received poison pill – exiting." << endl;
            break;
        }

        // Split chunk into lines and process each
        vector<string> lines;
        char *saveptr = nullptr;
        char *tok = strtok_r(item.data, "\n", &saveptr);
        while (tok != nullptr) {
            lines.push_back(string(tok));
            tok = strtok_r(nullptr, "\n", &saveptr);
        }

        for (const string &l : lines) {
            if (!l.empty())
                process_row(l);
        }

        cout << "[PROCESSOR][Worker-" << thread_id
             << "] processed chunk " << item.chunk_id
             << " (" << item.byte_count << " bytes, "
             << lines.size() << " lines)" << endl;

        delete[] item.data;
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Reader thread function
// Pulls raw chunks from the FIFO and enqueues them for the workers.
// When EOF chunk arrives it enqueues g_num_workers poison pills.
// ---------------------------------------------------------------------------
static void *reader_thread(void * /*arg*/) {
    cout << "[PROCESSOR][Reader] PID=" << getpid()
         << " started reading FIFO." << endl;

    while (true) {
        ChunkHeader hdr;
        // Read full header (loop to handle partial reads)
        ssize_t total = 0;
        while (total < (ssize_t)sizeof(ChunkHeader)) {
            ssize_t n = read(g_fifo_fd,
                             (char *)&hdr + total,
                             sizeof(ChunkHeader) - total);
            if (n <= 0) {
                cerr << "[PROCESSOR][Reader] FIFO read error or closed." << endl;
                goto enqueue_poisons;
            }
            total += n;
        }

        if (hdr.chunk_id == -1) {
            cout << "[PROCESSOR][Reader] EOF chunk received." << endl;
            break;
        }

        // Read the chunk data
        char *data = new char[hdr.byte_count + 1];
        total = 0;
        while (total < hdr.byte_count) {
            ssize_t n = read(g_fifo_fd,
                             data + total,
                             hdr.byte_count - total);
            if (n <= 0) {
                cerr << "[PROCESSOR][Reader] data read error." << endl;
                delete[] data;
                goto enqueue_poisons;
            }
            total += n;
        }
        data[hdr.byte_count] = '\0';

        cout << "[PROCESSOR][Reader] received chunk " << hdr.chunk_id
             << " from file " << hdr.source_file_id
             << " (" << hdr.byte_count << " bytes)" << endl;

        // Enqueue item (bounded buffer – wait if full)
        QueueItem item;
        item.is_poison      = false;
        item.chunk_id       = hdr.chunk_id;
        item.source_file_id = hdr.source_file_id;
        item.data           = data;
        item.byte_count     = hdr.byte_count;

        sem_wait(&g_sem_empty);   // wait for a free slot

        pthread_mutex_lock(&g_queue_mutex);
        g_queue[g_head] = item;
        g_head = (g_head + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);

        sem_post(&g_sem_full);    // signal that an item is available
    }

enqueue_poisons:
    // Send one poison pill per worker so every worker exits
    for (int i = 0; i < g_num_workers; i++) {
        QueueItem poison;
        poison.is_poison      = true;
        poison.chunk_id       = -1;
        poison.source_file_id = -1;
        poison.data           = nullptr;
        poison.byte_count     = 0;

        sem_wait(&g_sem_empty);

        pthread_mutex_lock(&g_queue_mutex);
        g_queue[g_head] = poison;
        g_head = (g_head + 1) % g_queue_size;
        pthread_mutex_unlock(&g_queue_mutex);

        sem_post(&g_sem_full);
    }

    cout << "[PROCESSOR][Reader] enqueued " << g_num_workers
         << " poison pills – reader exiting." << endl;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Write aggregation results into POSIX shared memory.
//
// Segment layout:
//   [int  record_count]
//   [record_count × { char[64] user_id,
//                     long long total_session_length,
//                     int       total_sessions,
//                     int       bounce_sessions }]
// ---------------------------------------------------------------------------
static bool write_shared_memory(const char *shm_name) {
    int record_count = (int)g_agg_table.size();
    size_t shm_size  = sizeof(int) + record_count * SHM_RECORD_SIZE;

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[PROCESSOR] shm_open");
        return false;
    }
    if (ftruncate(shm_fd, shm_size) < 0) {
        perror("[PROCESSOR] ftruncate");
        close(shm_fd);
        return false;
    }

    void *shm_ptr = mmap(nullptr, shm_size,
                         PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (shm_ptr == MAP_FAILED) {
        perror("[PROCESSOR] mmap");
        return false;
    }

    char *ptr = (char *)shm_ptr;

    // Write record count
    memcpy(ptr, &record_count, sizeof(int));
    ptr += sizeof(int);

    // Write each record
    for (auto &kv : g_agg_table) {
        const string   &uid  = kv.first;
        const UserStats &s   = kv.second;

        // user_id (null-padded to SHM_USER_ID_LEN)
        memset(ptr, 0, SHM_USER_ID_LEN);
        strncpy(ptr, uid.c_str(), SHM_USER_ID_LEN - 1);
        ptr += SHM_USER_ID_LEN;

        memcpy(ptr, &s.total_session_length, sizeof(long long)); ptr += sizeof(long long);
        memcpy(ptr, &s.total_sessions,       sizeof(int));        ptr += sizeof(int);
        memcpy(ptr, &s.bounce_sessions,      sizeof(int));        ptr += sizeof(int);
    }

    munmap(shm_ptr, shm_size);

    cout << "[PROCESSOR] Wrote " << record_count
         << " user records to shared memory '" << shm_name << "'" << endl;
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    cout << "[PROCESSOR] PID:  " << getpid()  << endl;
    cout << "[PROCESSOR] PPID: " << getppid() << endl;

    // ── Argument parsing ──────────────────────────────────────────────────
    if (argc != 6) {
        cerr << "Usage: " << argv[0]
             << " <fifo_path> <num_threads> <queue_size>"
             << " <shm_name> <sem_name>" << endl;
        return 10;
    }

    const char *fifo_path  = argv[1];
    g_num_workers          = atoi(argv[2]);
    g_queue_size           = atoi(argv[3]);
    const char *shm_name   = argv[4];
    const char *sem_name   = argv[5];

    if (g_num_workers <= 0 || g_queue_size <= 0) {
        cerr << "[PROCESSOR] num_threads and queue_size must be > 0." << endl;
        return 10;
    }

    // ── Open FIFO ─────────────────────────────────────────────────────────
    g_fifo_fd = open(fifo_path, O_RDONLY);
    if (g_fifo_fd < 0) {
        perror("[PROCESSOR] open FIFO");
        return 20;
    }
    cout << "[PROCESSOR] FIFO '" << fifo_path << "' opened for reading." << endl;

    // ── Initialise bounded queue ──────────────────────────────────────────
    g_queue = new QueueItem[g_queue_size];

    if (sem_init(&g_sem_empty, 0, g_queue_size) != 0) {
        perror("[PROCESSOR] sem_init empty");
        return 20;
    }
    if (sem_init(&g_sem_full, 0, 0) != 0) {
        perror("[PROCESSOR] sem_init full");
        return 20;
    }

    // ── Create worker threads with explicit pthread_attr_t ────────────────
    //
    // We set:
    //   • detach state  = PTHREAD_CREATE_JOINABLE  (so we can pthread_join)
    //   • stack size    = 2 MB  (default is often 8 MB; 2 MB is enough here)
    //
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&thread_attr, 2 * 1024 * 1024);  // 2 MB

    vector<pthread_t> workers(g_num_workers);
    for (int i = 0; i < g_num_workers; i++) {
        int *id = new int(i);
        if (pthread_create(&workers[i], &thread_attr, worker_thread, id) != 0) {
            perror("[PROCESSOR] pthread_create worker");
            return 20;
        }
    }
    pthread_attr_destroy(&thread_attr);

    // ── Create reader thread ───────────────────────────────────────────────
    pthread_t reader;
    if (pthread_create(&reader, nullptr, reader_thread, nullptr) != 0) {
        perror("[PROCESSOR] pthread_create reader");
        return 20;
    }

    // ── Wait for reader to finish ──────────────────────────────────────────
    pthread_join(reader, nullptr);
    cout << "[PROCESSOR] Reader thread joined." << endl;

    // ── Wait for all workers to finish ────────────────────────────────────
    for (int i = 0; i < g_num_workers; i++) {
        pthread_join(workers[i], nullptr);
    }
    cout << "[PROCESSOR] All worker threads joined." << endl;

    // ── Write results to shared memory ────────────────────────────────────
    if (!write_shared_memory(shm_name)) {
        return 20;
    }

    // ── Signal reporter via named semaphore ──────────────────────────────
    //
    // We create (or open) the named semaphore initialised to 0.
    // sem_post() increments it so the reporter's sem_wait() unblocks.
    //
    sem_t *done_sem = sem_open(sem_name, O_CREAT, 0666, 0);
    if (done_sem == SEM_FAILED) {
        perror("[PROCESSOR] sem_open named semaphore");
        return 20;
    }
    sem_post(done_sem);
    sem_close(done_sem);
    cout << "[PROCESSOR] Posted named semaphore '" << sem_name
         << "' – reporter can now read shared memory." << endl;

    // ── Cleanup ───────────────────────────────────────────────────────────
    sem_destroy(&g_sem_empty);
    sem_destroy(&g_sem_full);
    pthread_mutex_destroy(&g_queue_mutex);
    pthread_mutex_destroy(&g_agg_mutex);
    delete[] g_queue;
    close(g_fifo_fd);

    cout << "[PROCESSOR] PID=" << getpid() << " finished work." << endl;
    return 0;
}
// =============================================================================
// reporter.cpp  –  Result reader and report generator
//
// Usage: ./reporter <shm_name> <sem_name> <output_dir>
//
// Responsibilities:
//   1. Wait on named semaphore until processor signals data is ready.
//   2. Open and mmap POSIX shared memory written by processor.
//   3. Parse per-user records from SHM.
//   4. Write a CSV report: output_dir/report.csv
//   5. Print a summary to stdout (top users, bounce rate, totals).
//   6. Handle SIGTERM for graceful shutdown.
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

// SHM layout constants – must match processor.cpp exactly
#define SHM_USER_ID_LEN 64
#define SHM_RECORD_SIZE (SHM_USER_ID_LEN + sizeof(long long) + sizeof(int) + sizeof(int))

#define MAX_REPORT_PATH 512
#define MAX_RECORDS     20000

// ---------------------------------------------------------------------------
// One parsed record (read from SHM into local array for sorting)
// ---------------------------------------------------------------------------
struct Record {
    char      user_id[SHM_USER_ID_LEN];
    long long total_session_length;
    int       total_sessions;
    int       bounce_sessions;
};

static volatile sig_atomic_t g_stop = 0;

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
// Block on named semaphore until processor posts it.
// ---------------------------------------------------------------------------
static int WAIT_FOR_DATA(const char *sem_name) {
    sem_t *sem = sem_open(sem_name, 0);
    if (sem == SEM_FAILED) {
        perror("[REPORTER] sem_open");
        return 0;
    }
    printf("[REPORTER] Waiting for processor to finish...\n");
    fflush(stdout);
    sem_wait(sem);
    sem_close(sem);
    printf("[REPORTER] Data ready – reading shared memory.\n");
    fflush(stdout);
    return 1;
}

// ---------------------------------------------------------------------------
// Read all records from shared memory into local array.
// Returns number of records read, or -1 on error.
// ---------------------------------------------------------------------------
static int READ_SHM(const char *shm_name,
                     struct Record *records,
                     int max_records)
{
    int shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    if (shm_fd < 0) {
        perror("[REPORTER] shm_open");
        return -1;
    }

    // Get segment size via fstat
    struct stat st;
    if (fstat(shm_fd, &st) < 0) {
        perror("[REPORTER] fstat");
        close(shm_fd);
        return -1;
    }
    size_t shm_size = (size_t)st.st_size;

    if (shm_size < sizeof(int)) {
        fprintf(stderr, "[REPORTER] SHM too small (%zu bytes)\n", shm_size);
        close(shm_fd);
        return -1;
    }

    void *shm_ptr = mmap(NULL, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (shm_ptr == MAP_FAILED) {
        perror("[REPORTER] mmap");
        return -1;
    }

    char *ptr = (char *)shm_ptr;

    // First 4 bytes = number of records
    int record_count = 0;
    memcpy(&record_count, ptr, sizeof(int));
    ptr += sizeof(int);

    if (record_count < 0 || record_count > max_records) {
        fprintf(stderr, "[REPORTER] Unexpected record_count=%d\n", record_count);
        munmap(shm_ptr, shm_size);
        return -1;
    }

    for (int i = 0; i < record_count; i++) {
        memcpy(records[i].user_id, ptr, SHM_USER_ID_LEN);
        records[i].user_id[SHM_USER_ID_LEN - 1] = '\0';
        ptr += SHM_USER_ID_LEN;

        memcpy(&records[i].total_session_length, ptr, sizeof(long long));
        ptr += sizeof(long long);

        memcpy(&records[i].total_sessions, ptr, sizeof(int));
        ptr += sizeof(int);

        memcpy(&records[i].bounce_sessions, ptr, sizeof(int));
        ptr += sizeof(int);
    }

    munmap(shm_ptr, shm_size);
    printf("[REPORTER] Read %d user records from SHM '%s'\n",
           record_count, shm_name);
    fflush(stdout);
    return record_count;
}

// ---------------------------------------------------------------------------
// Simple bubble sort: order records by total_sessions descending.
// (Small enough for viva-level code; dataset is per-user not per-row.)
// ---------------------------------------------------------------------------
static void SORT_RECORDS(struct Record *records, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (records[j].total_sessions < records[j + 1].total_sessions) {
                struct Record tmp = records[j];
                records[j]       = records[j + 1];
                records[j + 1]   = tmp;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Write CSV report and print summary to stdout.
// ---------------------------------------------------------------------------
static void WRITE_REPORT(struct Record *records, int count,
                          const char *output_dir)
{
    // Build output file path
    char report_path[MAX_REPORT_PATH];
    snprintf(report_path, MAX_REPORT_PATH, "%s/report.csv", output_dir);

    FILE *fp = fopen(report_path, "w");
    if (fp == NULL) {
        perror("[REPORTER] fopen report.csv");
        return;
    }

    // CSV header
    fprintf(fp, "user_id,total_sessions,total_session_length_sec,"
                "avg_session_length_sec,bounce_sessions,bounce_rate_pct\n");

    long long grand_total_sessions = 0;
    long long grand_total_length   = 0;
    long long grand_bounces        = 0;

    for (int i = 0; i < count; i++) {
        struct Record *r = &records[i];

        double avg_len     = (r->total_sessions > 0)
                             ? (double)r->total_session_length / r->total_sessions
                             : 0.0;
        double bounce_rate = (r->total_sessions > 0)
                             ? (100.0 * r->bounce_sessions / r->total_sessions)
                             : 0.0;

        fprintf(fp, "%s,%d,%lld,%.2f,%d,%.1f\n",
                r->user_id,
                r->total_sessions,
                r->total_session_length,
                avg_len,
                r->bounce_sessions,
                bounce_rate);

        grand_total_sessions += r->total_sessions;
        grand_total_length   += r->total_session_length;
        grand_bounces        += r->bounce_sessions;
    }

    fclose(fp);
    printf("[REPORTER] Report written to '%s'\n", report_path);

    // ── Print summary to stdout ─────────────────────────────────────────────
    printf("\n========== PIPELINE SUMMARY ==========\n");
    printf("Unique users       : %d\n", count);
    printf("Total sessions     : %lld\n", grand_total_sessions);
    printf("Total session time : %lld seconds\n", grand_total_length);
    if (grand_total_sessions > 0) {
        printf("Avg session length : %.2f seconds\n",
               (double)grand_total_length / grand_total_sessions);
        printf("Overall bounce rate: %.1f%%\n",
               100.0 * grand_bounces / grand_total_sessions);
    }

    // Top 5 users by session count
    printf("\n--- Top 5 Users by Session Count ---\n");
    int top = (count < 5) ? count : 5;
    for (int i = 0; i < top; i++) {
        printf("  %d. %-10s  sessions=%d  total_time=%lld s\n",
               i + 1,
               records[i].user_id,
               records[i].total_sessions,
               records[i].total_session_length);
    }
    printf("======================================\n\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    printf("[REPORTER] PID:  %d\n", getpid());
    printf("[REPORTER] PPID: %d\n", getppid());

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <shm_name> <sem_name> <output_dir>\n",
                argv[0]);
        return 1;
    }

    const char *shm_name   = argv[1];
    const char *sem_name   = argv[2];
    const char *output_dir = argv[3];

    INSTALL_HANDLER();

    // Ensure output directory exists
    mkdir(output_dir, 0755);

    // ── Wait for processor to signal completion ──────────────────────────────
    if (!WAIT_FOR_DATA(sem_name)) return 1;
    if (g_stop) {
        printf("[REPORTER] Shutdown requested – exiting.\n");
        return 0;
    }

    // ── Read records from shared memory ─────────────────────────────────────
    static struct Record records[MAX_RECORDS];
    int count = READ_SHM(shm_name, records, MAX_RECORDS);
    if (count < 0) return 1;

    // ── Sort and write report ────────────────────────────────────────────────
    SORT_RECORDS(records, count);
    WRITE_REPORT(records, count, output_dir);

    printf("[REPORTER] PID=%d finished.\n", getpid());
    return 0;
}
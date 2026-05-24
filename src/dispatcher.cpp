// =============================================================================
// dispatcher.cpp  –  Master orchestration process
//
// Usage: ./dispatcher <input_dir> <output_dir> <num_threads> <queue_size>
//
// Responsibilities:
//   1. Parse CLI args.
//   2. Create FIFO and shared-memory segment and named semaphore.
//   3. Install signal handlers (SIGINT, SIGTERM, SIGCHLD, SIGUSR1).
//   4. Fork + exec ingester, processor, reporter, each with stdout/stderr
//      redirected to a per-process log file via dup2().
//   5. Block in sigsuspend() loop until all 3 children exit.
//   6. On shutdown: forward SIGTERM to children, waitpid, cleanup IPC.
//   7. Print final summary (PID, exit status, runtime) per child.
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>

// IPC names – fixed so every component agrees
#define FIFO_PATH   "/tmp/clickstream_pipe"
#define SHM_NAME    "/clickstream_shm"
#define SEM_NAME    "/clickstream_done_sem"
#define LOGS_DIR    "logs"

// Globals used inside signal handlers
static pid_t g_pid_ingester  = -1;
static pid_t g_pid_processor = -1;
static pid_t g_pid_reporter  = -1;
static volatile sig_atomic_t g_children_done = 0;
static volatile sig_atomic_t g_shutdown      = 0;

// ---------------------------------------------------------------------------
// Signal handlers
// ---------------------------------------------------------------------------
static void HANDLE_SIGCHLD(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
    g_children_done = 1;
}

static void HANDLE_SHUTDOWN(int sig) {
    (void)sig;
    g_shutdown = 1;
    if (g_pid_ingester  > 0) kill(g_pid_ingester,  SIGTERM);
    if (g_pid_processor > 0) kill(g_pid_processor, SIGTERM);
    if (g_pid_reporter  > 0) kill(g_pid_reporter,  SIGTERM);
}

static void HANDLE_SIGUSR1(int sig) {
    (void)sig;
    fprintf(stderr,
            "[DISPATCHER] SIGUSR1: ingester=%d processor=%d reporter=%d\n",
            (int)g_pid_ingester,
            (int)g_pid_processor,
            (int)g_pid_reporter);
}

// ---------------------------------------------------------------------------
// Install all signal handlers
// ---------------------------------------------------------------------------
static void INSTALL_HANDLER(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = HANDLE_SIGCHLD;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = HANDLE_SHUTDOWN;
    sa.sa_flags   = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = HANDLE_SIGUSR1;
    sa.sa_flags   = 0;
    sigaction(SIGUSR1, &sa, NULL);
}

// ---------------------------------------------------------------------------
// Open a log file and redirect stdout + stderr into it.
// Called inside the child process BEFORE exec().
// ---------------------------------------------------------------------------
static void REDIRECT_LOGS(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open log"); return; }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    printf("[DISPATCHER] PID:  %d\n", getpid());
    printf("[DISPATCHER] PPID: %d\n", getppid());

    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <input_dir> <output_dir> <num_threads> <queue_size>\n",
                argv[0]);
        return 10;
    }

    const char *input_dir  = argv[1];
    const char *output_dir = argv[2];
    const char *n_threads  = argv[3];
    const char *q_size     = argv[4];

    // ── Create directories ───────────────────────────────────────────────────
    mkdir(LOGS_DIR,    0755);
    mkdir(output_dir,  0755);

    // ── Create FIFO ──────────────────────────────────────────────────────────
    unlink(FIFO_PATH);
    if (mkfifo(FIFO_PATH, 0666) < 0) {
        perror("[DISPATCHER] mkfifo");
        return 20;
    }
    printf("[DISPATCHER] FIFO created: %s\n", FIFO_PATH);

    // Open FIFO O_RDWR so it always has both ends open.
    // This prevents ingester (writer) and processor (reader) from blocking
    // on open() waiting for the other end to connect first.
    int fifo_keeper = open(FIFO_PATH, O_RDWR);
    if (fifo_keeper < 0) {
        perror("[DISPATCHER] open FIFO keeper");
        return 20;
    }

    // ── Create shared-memory segment (placeholder; processor resizes it) ─────
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("[DISPATCHER] shm_open"); return 20; }
    ftruncate(shm_fd, sizeof(int));
    close(shm_fd);
    printf("[DISPATCHER] Shared memory '%s' created.\n", SHM_NAME);

    // ── Create named semaphore (init=0; processor will sem_post it) ──────────
    sem_unlink(SEM_NAME);
    sem_t *done_sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 0);
    if (done_sem == SEM_FAILED) { perror("[DISPATCHER] sem_open"); return 20; }
    sem_close(done_sem);
    printf("[DISPATCHER] Semaphore '%s' created.\n", SEM_NAME);

    // ── Install signal handlers ──────────────────────────────────────────────
    INSTALL_HANDLER();

    // ── Record start times ───────────────────────────────────────────────────
    time_t t_ingester_start, t_processor_start, t_reporter_start;

    // ── Fork ingester ─────────────────────────────────────────────────────────
    t_ingester_start = time(NULL);
    g_pid_ingester = fork();
    if (g_pid_ingester == 0) {
        execl("./build/ingester", "ingester", input_dir, NULL);
        perror("execl ingester");
        _exit(40);
    }
    printf("[DISPATCHER] Ingester  PID=%d\n", (int)g_pid_ingester);

    // ── Fork processor ────────────────────────────────────────────────────────
    t_processor_start = time(NULL);
    g_pid_processor = fork();
    if (g_pid_processor == 0) {
        execl("./build/processor", "processor",
              FIFO_PATH, n_threads, q_size, SHM_NAME, SEM_NAME, NULL);
        perror("execl processor");
        _exit(40);
    }
    printf("[DISPATCHER] Processor PID=%d\n", (int)g_pid_processor);

    // ── Fork reporter ─────────────────────────────────────────────────────────
    t_reporter_start = time(NULL);
    g_pid_reporter = fork();
    if (g_pid_reporter == 0) {
        execl("./build/reporter", "reporter",
              SHM_NAME, SEM_NAME, output_dir, NULL);
        perror("execl reporter");
        _exit(40);
    }
    printf("[DISPATCHER] Reporter  PID=%d\n", (int)g_pid_reporter);

    // ── Wait loop (sigsuspend – no busy-wait) ────────────────────────────────
    sigset_t wait_mask;
    sigfillset(&wait_mask);
    sigdelset(&wait_mask, SIGCHLD);
    sigdelset(&wait_mask, SIGINT);
    sigdelset(&wait_mask, SIGTERM);
    sigdelset(&wait_mask, SIGUSR1);

    int remaining = 3;
    while (remaining > 0 && !g_shutdown) {
        sigsuspend(&wait_mask);

        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            time_t end   = time(NULL);
            const char *name  = "unknown";
            time_t      start = end;

            if (pid == g_pid_ingester)  { name = "ingester";  start = t_ingester_start; }
            if (pid == g_pid_processor) { name = "processor"; start = t_processor_start; }
            if (pid == g_pid_reporter)  { name = "reporter";  start = t_reporter_start; }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            printf("[DISPATCHER] Child %s PID=%d status=%d runtime=%lds\n",
                   name, (int)pid, exit_code, (long)(end - start));
            remaining--;
        }
    }

    // If we got a shutdown signal, reap remaining children
    if (g_shutdown) {
        if (g_pid_ingester  > 0) kill(g_pid_ingester,  SIGTERM);
        if (g_pid_processor > 0) kill(g_pid_processor, SIGTERM);
        if (g_pid_reporter  > 0) kill(g_pid_reporter,  SIGTERM);
        waitpid(g_pid_ingester,  NULL, 0);
        waitpid(g_pid_processor, NULL, 0);
        waitpid(g_pid_reporter,  NULL, 0);
    }

    // ── Cleanup IPC ──────────────────────────────────────────────────────────
    // Close keeper now – all children are done with the FIFO.
    close(fifo_keeper);
    unlink(FIFO_PATH);
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    printf("[DISPATCHER] IPC resources cleaned up.\n");
    printf("[DISPATCHER] All processes finished. Exiting.\n");
    return 0;
}
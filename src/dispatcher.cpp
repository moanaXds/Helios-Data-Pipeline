// =============================================================================
// dispatcher.cpp  –  Master process
//
// Usage: ./dispatcher <input_dir> <output_dir> <num_threads N> <queue_size Q>
//
// Responsibilities:
//   1. Parse CLI args.
//   2. Create FIFO and shared-memory segment.
//   3. Install signal handlers (SIGINT, SIGTERM, SIGCHLD, SIGUSR1).
//   4. Fork + exec ingester, processor, reporter (each redirecting
//      stdout/stderr to a per-process log file via dup2).
//   5. Block in sigsuspend() loop until all 3 children exit.
//   6. On shutdown: forward SIGTERM to children, waitpid, cleanup IPC.
//   7. Print final summary (PID, exit status, runtime) per child.
// =============================================================================

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>

using namespace std;

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

// ── Signal handlers ──────────────────────────────────────────────────────────

static void handle_sigchld(int /*sig*/) {
    // Reap any child that finished; avoid zombies
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
    g_children_done = 1;
}

static void handle_shutdown(int /*sig*/) {
    g_shutdown = 1;
    // Forward SIGTERM to children so they can clean up
    if (g_pid_ingester  > 0) kill(g_pid_ingester,  SIGTERM);
    if (g_pid_processor > 0) kill(g_pid_processor, SIGTERM);
    if (g_pid_reporter  > 0) kill(g_pid_reporter,  SIGTERM);
}

static void handle_sigusr1(int /*sig*/) {
    // Dump live status to stderr
    fprintf(stderr,
            "[DISPATCHER] SIGUSR1: ingester=%d processor=%d reporter=%d\n",
            (int)g_pid_ingester, (int)g_pid_processor, (int)g_pid_reporter);
}

// ── Helper: open a log file and dup2 stdout+stderr to it ────────────────────
// Called inside the child BEFORE exec.
static void redirect_logs(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open log"); return; }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

// ── main ────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    cout << "[DISPATCHER] PID:  " << getpid()  << endl;
    cout << "[DISPATCHER] PPID: " << getppid() << endl;

    if (argc != 5) {
        cerr << "Usage: " << argv[0]
             << " <input_dir> <output_dir> <num_threads> <queue_size>" << endl;
        return 10;
    }

    const char *input_dir  = argv[1];
    const char *output_dir = argv[2];
    const char *n_threads  = argv[3];
    const char *q_size     = argv[4];

    // ── Create logs directory ────────────────────────────────────────────
    mkdir(LOGS_DIR, 0755);
    mkdir(output_dir, 0755);

    // ── Create FIFO ──────────────────────────────────────────────────────
    unlink(FIFO_PATH);   // remove stale pipe if any
    if (mkfifo(FIFO_PATH, 0666) < 0) {
        perror("[DISPATCHER] mkfifo");
        return 20;
    }
    cout << "[DISPATCHER] FIFO created: " << FIFO_PATH << endl;

    // ── Pre-create shared-memory segment (tiny placeholder; processor
    //    will ftruncate to the real size after aggregation) ───────────────
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("[DISPATCHER] shm_open"); return 20; }
    ftruncate(shm_fd, sizeof(int));   // placeholder – just 4 bytes
    close(shm_fd);
    cout << "[DISPATCHER] Shared memory '" << SHM_NAME << "' created." << endl;

    // ── Pre-create named semaphore (init=0; processor will sem_post it) ──
    sem_unlink(SEM_NAME);
    sem_t *done_sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 0);
    if (done_sem == SEM_FAILED) { perror("[DISPATCHER] sem_open"); return 20; }
    sem_close(done_sem);
    cout << "[DISPATCHER] Named semaphore '" << SEM_NAME << "' created." << endl;

    // ── Install signal handlers ──────────────────────────────────────────
    struct sigaction sa{};
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    sa.sa_handler = handle_shutdown;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sa.sa_handler = handle_sigusr1;
    sigaction(SIGUSR1, &sa, nullptr);

    // ── Record start times ───────────────────────────────────────────────
    time_t t_ingester_start, t_processor_start, t_reporter_start;

    // ── Fork ingester ────────────────────────────────────────────────────
    t_ingester_start = time(nullptr);
    g_pid_ingester = fork();
    if (g_pid_ingester == 0) {
        redirect_logs(LOGS_DIR "/ingester.log");
        execl("./build/ingester", "ingester", input_dir, nullptr);
        perror("execl ingester"); _exit(40);
    }
    cout << "[DISPATCHER] Ingester  PID=" << g_pid_ingester << endl;

    // ── Fork processor ───────────────────────────────────────────────────
    t_processor_start = time(nullptr);
    g_pid_processor = fork();
    if (g_pid_processor == 0) {
        redirect_logs(LOGS_DIR "/processor.log");
        execl("./build/processor", "processor",
              FIFO_PATH, n_threads, q_size, SHM_NAME, SEM_NAME, nullptr);
        perror("execl processor"); _exit(40);
    }
    cout << "[DISPATCHER] Processor PID=" << g_pid_processor << endl;

    // ── Fork reporter ────────────────────────────────────────────────────
    t_reporter_start = time(nullptr);
    g_pid_reporter = fork();
    if (g_pid_reporter == 0) {
        redirect_logs(LOGS_DIR "/reporter.log");
        execl("./build/reporter", "reporter",
              SHM_NAME, SEM_NAME, output_dir, nullptr);
        perror("execl reporter"); _exit(40);
    }
    cout << "[DISPATCHER] Reporter  PID=" << g_pid_reporter << endl;

    // ── Wait loop (sigsuspend – no busy-wait) ────────────────────────────
    sigset_t wait_mask;
    sigfillset(&wait_mask);
    sigdelset(&wait_mask, SIGCHLD);
    sigdelset(&wait_mask, SIGINT);
    sigdelset(&wait_mask, SIGTERM);
    sigdelset(&wait_mask, SIGUSR1);

    int remaining = 3;
    while (remaining > 0 && !g_shutdown) {
        sigsuspend(&wait_mask);

        // Reap any finished children
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            time_t end = time(nullptr);
            const char *name = "unknown";
            time_t start = end;
            if (pid == g_pid_ingester)  { name = "ingester";  start = t_ingester_start; }
            if (pid == g_pid_processor) { name = "processor"; start = t_processor_start; }
            if (pid == g_pid_reporter)  { name = "reporter";  start = t_reporter_start; }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            cout << "[DISPATCHER] Child " << name
                 << " PID=" << pid
                 << " exited with status=" << exit_code
                 << " runtime=" << (end - start) << "s" << endl;
            remaining--;
        }
    }

    // If we got a shutdown signal, forward SIGTERM and reap
    if (g_shutdown) {
        if (g_pid_ingester  > 0) kill(g_pid_ingester,  SIGTERM);
        if (g_pid_processor > 0) kill(g_pid_processor, SIGTERM);
        if (g_pid_reporter  > 0) kill(g_pid_reporter,  SIGTERM);
        waitpid(g_pid_ingester,  nullptr, 0);
        waitpid(g_pid_processor, nullptr, 0);
        waitpid(g_pid_reporter,  nullptr, 0);
    }

    // ── Cleanup IPC ──────────────────────────────────────────────────────
    unlink(FIFO_PATH);
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    cout << "[DISPATCHER] IPC resources cleaned up." << endl;
    cout << "[DISPATCHER] All processes finished. Exiting." << endl;
    return 0;
}
// =============================================================================
// ingester.cpp  –  File reader / chunk sender
//
// Usage: ./ingester <input_dir>
//
// Responsibilities:
//   1. Scan input directory for .csv files.
//   2. Open the FIFO (created by dispatcher) for writing.
//   3. Read each file in chunks of CHUNK_LINES lines.
//   4. Send each chunk as: [ChunkHeader][raw bytes] over the FIFO.
//   5. Send an EOF sentinel chunk (chunk_id == -1) when done.
//   6. Handle SIGTERM for graceful shutdown.
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>

// Must match dispatcher.cpp exactly
#define FIFO_PATH    "/tmp/clickstream_pipe"

#define MAX_FILES    256
#define MAX_PATH     512
#define CHUNK_LINES  1000
#define LINE_BUF     512
// Worst case: CHUNK_LINES lines × LINE_BUF bytes each
#define CHUNK_BUF    (CHUNK_LINES * LINE_BUF)

// Shared chunk header (must match processor.cpp exactly)
struct ChunkHeader {
    int chunk_id;
    int byte_count;
    int source_file_id;
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

// Scan dir for .csv files; return count, fill file_list[][MAX_PATH]
static int COLLECT_FILES(const char *dir_path,
                          char file_list[][MAX_PATH],
                          int max_files)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("[INGESTER] opendir");
        return -1;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_files) {
        if (entry->d_type != DT_REG) continue;

        int len = strlen(entry->d_name);
        if (len < 4) continue;
        if (strcmp(entry->d_name + len - 4, ".csv") != 0) continue;

        snprintf(file_list[count], MAX_PATH, "%s/%s",
                 dir_path, entry->d_name);
        count++;
    }

    closedir(dir);
    return count;
}

// Write one chunk (header + data) to the FIFO
static void SEND_CHUNK(int fd, int chunk_id, int file_id,
                        char *buf, int len)
{
    struct ChunkHeader hdr;
    hdr.chunk_id       = chunk_id;
    hdr.byte_count     = len;
    hdr.source_file_id = file_id;

    write(fd, &hdr, sizeof(struct ChunkHeader));
    write(fd, buf, len);

    printf("[INGESTER] Sent chunk %d from file %d (%d bytes)\n",
           chunk_id, file_id, len);
    fflush(stdout);
}

// =============================================================================
int main(int argc, char *argv[])
{
    printf("[INGESTER] PID:  %d\n", getpid());
    printf("[INGESTER] PPID: %d\n", getppid());

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_dir>\n", argv[0]);
        return 1;
    }

    INSTALL_HANDLER();

    // ── Collect CSV file paths ──────────────────────────────────────────────
    static char file_list[MAX_FILES][MAX_PATH];
    int file_count = COLLECT_FILES(argv[1], file_list, MAX_FILES);
    if (file_count < 0) return 1;

    printf("[INGESTER] Found %d CSV file(s) in '%s'\n", file_count, argv[1]);

    // ── Open FIFO for writing (dispatcher already created it) ───────────────
    int fifo_fd = open(FIFO_PATH, O_WRONLY);
    if (fifo_fd < 0) {
        perror("[INGESTER] open FIFO");
        return 1;
    }
    printf("[INGESTER] FIFO '%s' opened for writing.\n", FIFO_PATH);

    // ── Read files and send chunks ──────────────────────────────────────────
    static char chunk_buf[CHUNK_BUF];
    char        line_buf[LINE_BUF];
    int         chunk_id  = 0;
    int         buf_pos   = 0;
    int         line_count = 0;

    for (int i = 0; i < file_count && !g_stop; i++) {
        FILE *fp = fopen(file_list[i], "r");
        if (fp == NULL) {
            fprintf(stderr, "[INGESTER] Cannot open: %s\n", file_list[i]);
            continue;
        }

        buf_pos    = 0;
        line_count = 0;

        while (fgets(line_buf, sizeof(line_buf), fp) != NULL && !g_stop) {
            int len = strlen(line_buf);

            // Flush if this line would overflow the chunk buffer
            if (buf_pos + len >= CHUNK_BUF || line_count >= CHUNK_LINES) {
                if (buf_pos > 0) {
                    SEND_CHUNK(fifo_fd, chunk_id++, i, chunk_buf, buf_pos);
                    buf_pos    = 0;
                    line_count = 0;
                }
            }

            memcpy(chunk_buf + buf_pos, line_buf, len);
            buf_pos   += len;
            line_count++;
        }

        // Send any remaining lines that did not fill a full chunk
        if (buf_pos > 0 && !g_stop) {
            SEND_CHUNK(fifo_fd, chunk_id++, i, chunk_buf, buf_pos);
        }

        fclose(fp);
    }

    // ── EOF sentinel ────────────────────────────────────────────────────────
    struct ChunkHeader eof_hdr;
    eof_hdr.chunk_id       = -1;
    eof_hdr.byte_count     = 0;
    eof_hdr.source_file_id = -1;

    write(fifo_fd, &eof_hdr, sizeof(struct ChunkHeader));
    printf("[INGESTER] Sent EOF chunk. Total chunks sent: %d\n", chunk_id);
    fflush(stdout);

    close(fifo_fd);
    printf("[INGESTER] PID=%d finished.\n", getpid());
    return 0;
}
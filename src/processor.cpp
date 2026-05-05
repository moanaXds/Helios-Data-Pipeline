#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <fcntl.h>
#include <vector>
#include <string.h>
using namespace std;

struct header_chunk
{
    int chunk_id;
    int byte_count;
    int source_file_id;
};

int main() {
    cout << "[PROCESSOR PID: " << getpid() << endl;
    cout << "[PROCESSOR PPID: " << getppid() << endl;

    int fifo_read = open("/tmp/my_pipe", O_RDONLY);
    if (fifo_read < 0) {
        cout << "Error opening pipe for reading" << endl;
        return 1;
    }

    while(true)
    {
        header_chunk header;
        read(fifo_read, &header, sizeof(header_chunk));
        if (header.chunk_id == -1) {
            cout << "Received EOF signal. No more chunks to process." << endl;
            break;
        }
        char *chunk_data = new char[header.byte_count + 1];
        read(fifo_read, chunk_data, header.byte_count);
        chunk_data[header.byte_count] = '\0'; // Null-terminate the chunk data
        cout << "[PROCESSOR] Received chunk " << header.chunk_id
             << " from file " << header.source_file_id
             << " (" << header.byte_count << " bytes)" << endl;
        
        // split by newlines 
        vector<string> lines;
        char *line = strtok(chunk_data, "\n");
        while (line != NULL) {
            lines.push_back(string(line));
            // get the next line
            line = strtok(NULL, "\n");
        }
        // parse each into fields user_id,url,session_id,timestamp,page_views,session_length_seconds,is_bounce
        for (const string &line : lines) {
            char *line_copy = new char[line.size() + 1];
            strcpy(line_copy, line.c_str());
            char *user_id = strtok(line_copy, ",");
            char *url = strtok(NULL, ",");
            char *session_id = strtok(NULL, ",");
            char *timestamp = strtok(NULL, ",");
            char *page_views = strtok(NULL, ",");
            char *session_length_seconds = strtok(NULL, ",");
            char *is_bounce = strtok(NULL, ",");
            cout << "Parsed fields: user_id=" << user_id 
                 << ", url=" << url 
                 << ", session_id=" << session_id 
                 << ", timestamp=" << timestamp 
                 << ", page_views=" << page_views 
                 << ", session_length_seconds=" << session_length_seconds 
                 << ", is_bounce=" << is_bounce << endl;
            delete[] line_copy;
        }

        delete[] chunk_data;
    }



    cout << "[PROCESSOR finished work" << endl;

    return 0;
}
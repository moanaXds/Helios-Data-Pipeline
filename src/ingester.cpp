#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <fcntl.h>
#include <vector>
#include <string.h>
using namespace std;

struct ChunkHeader {
    int chunk_id;
    int byte_count;
    int source_file_id;
};

int main(int argc, char* argv[]) {
    cout << "[INGESTER PID: " << getpid() << endl;
    cout << "[INGESTER PPID: " << getppid() << endl;
    
    // store the file names
    vector<string> csv_file_names;

    // open the input directory and list all the files
    if (argc != 2) {
        cout << "usage: "<< argv[0] << " <input_directory_path>" << endl;
        return 1;
    } 

    DIR* dir = opendir(argv[1]);
    if (dir == NULL) {
        cout << "error while opening directory:" << argv[1] <<endl;
        return 1;
    }
    struct dirent* entry;
    cout << "files in the input directory are:"<< endl;
    while((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG)  { 
        string name = entry->d_name;
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".csv") {
            csv_file_names.push_back(string(argv[1]) + "/" + name);
        }
        }
    }
    closedir(dir);


    const char* pipePath = "/tmp/my_pipe";

    // Attempt to create the named pipe
    if (mkfifo(pipePath, 0666) == -1) {
        if (errno == EEXIST) {
            std::cout << "Pipe already exists, proceeding." << std::endl;
        } else {
            // Handle other potential errors (e.g., EACCES, ENOSPC)
            std::cerr << "Failed to create pipe." << endl;
            return 1;
        }
    } else {
        std::cout << "Pipe created successfully." << std::endl;
    }
    
    int fifo_write = open(pipePath,O_WRONLY);
    if (fifo_write <0)
    {
        cout << "Error opening file" << endl;
    }
    else
    {
        char buffer[1024];
        string chunk_buffer = "";
        int chunk_size = 1000;
        int chunk_id = 0;

        for (int i = 0; i < csv_file_names.size(); i++) {
            string& filepath = csv_file_names[i];
            FILE* file = fopen(filepath.c_str(), "r");
            
            if (file == NULL) {
                cout << "Error opening file: " << filepath << endl;
                continue;
            }

            int line_count = 0;
            while (fgets(buffer, sizeof(buffer), file) != NULL) {
                chunk_buffer += buffer;
                line_count++;

                if (line_count == chunk_size) {
                    ChunkHeader header;
                    header.chunk_id = chunk_id++;
                    header.source_file_id = i;
                    header.byte_count = chunk_buffer.size();

                    write(fifo_write, &header, sizeof(ChunkHeader));
                    write(fifo_write, chunk_buffer.c_str(), chunk_buffer.size());

                    cout << "[INGESTER] Sent chunk " << header.chunk_id 
                        << " from file " << i 
                        << " (" << header.byte_count << " bytes)" << endl;

                    chunk_buffer = "";
                    line_count = 0;
                }
            }

            // send remaining lines that didn't fill a full chunk
            if (!chunk_buffer.empty()) {
                ChunkHeader header;
                header.chunk_id = chunk_id++;
                header.source_file_id = i;
                header.byte_count = chunk_buffer.size();

                write(fifo_write, &header, sizeof(ChunkHeader));
                write(fifo_write, chunk_buffer.c_str(), chunk_buffer.size());

                cout << "[INGESTER] Sent final chunk " << header.chunk_id 
                    << " from file " << i 
                    << " (" << header.byte_count << " bytes)" << endl;

                chunk_buffer = "";
            }

            fclose(file);
        }
    }
    close(fifo_write);



    cout << "[INGESTER finished work" << endl;

    return 0;
}
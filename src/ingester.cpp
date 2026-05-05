#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <fcntl.h>
#include <vector>
#include <string.h>
using namespace std;

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
        char buffer[1024];  // line buffer

        for (const string& filepath : csv_file_names) {
            FILE* file = fopen(filepath.c_str(), "r");
            if (file == NULL) { /* handle error */ continue; }
            
            while (fgets(buffer, sizeof(buffer), file) != NULL) {
                write(fifo_write, buffer, strlen(buffer));
                
            }
            fclose(file);
        }
    }
    close(fifo_write);



    cout << "[INGESTER finished work" << endl;

    return 0;
}
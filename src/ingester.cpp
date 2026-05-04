#include <iostream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>

using namespace std;

int main(int argc, char* argv[]) {
    cout << "[INGESTER PID: " << getpid() << endl;
    cout << "[INGESTER PPID: " << getppid() << endl;
    
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
        if (entry->d_type == DT_REG) { // only print regular files
            cout << entry->d_name << endl;
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



    sleep(2);

    cout << "[INGESTER finished work" << endl;

    return 0;
}
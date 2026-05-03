#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
    cout << "[REPORTER PID: " << getpid() << endl;
    cout << "[REPORTER PPID: " << getppid() << endl;

    sleep(2);

    cout << "[REPORTER Finished work" << endl;

    return 0;
}
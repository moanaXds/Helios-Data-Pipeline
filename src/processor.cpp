#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
    cout << "[PROCESSOR PID: " << getpid() << endl;
    cout << "[PROCESSOR PPID: " << getppid() << endl;

    sleep(2);

    cout << "[PROCESSOR finished work" << endl;

    return 0;
}
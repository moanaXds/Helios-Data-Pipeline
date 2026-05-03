#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
    cout << "[INGESTER PID: " << getpid() << endl;
    cout << "[INGESTER PPID: " << getppid() << endl;

    sleep(2);

    cout << "[INGESTER finished work" << endl;

    return 0;
}
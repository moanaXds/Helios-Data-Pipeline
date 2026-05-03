#include<iostream>
#include<unistd.h>
#include<sys/wait.h>

using namespace std;


int main()
{
    cout<<"   DISPATCHER PID "<<getpid()<<endl;

    pid_t pid1=fork();

    if(pid1==0)
    {
        cout<<"   INGESTER STARTED "<<endl;   
        execl("./build/ingester","ingester",NULL);
        perror("execl failed");

    }

    
    pid_t pid2=fork();

    if(pid2==0)
    {
        cout<<"   PROCESSOR STARTED "<<endl;   
        execl("./build/processor","proessor",NULL);
        perror("execl failed");
    }

    pid_t pid3=fork();

    if(pid1==0)
    {
        cout<<"   REPORTER STARTED "<<endl;  
        execl("./build/reporter","reporter",NULL);
        perror("execl failed"); 
    }

    wait(NULL);
    wait(NULL);
    wait(NULL);

    cout<<" DISPATCHERS :: all prossese finished "<<endl;

    return 0;

}
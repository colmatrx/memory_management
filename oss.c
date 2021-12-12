#include<stdio.h>
#include<stdlib.h>
#include <sys/ipc.h>
#include<sys/shm.h>
#include<sys/types.h>
#include<unistd.h>
#include<string.h>
#include<strings.h>
#include<time.h>
#include<sys/wait.h>
#include<signal.h>
#include<sys/msg.h>
#include<getopt.h>
#include<sys/sem.h>
#include "config.h"

/*Author Idris Adeleke CS4760 Project 6 - Memory Management*/

/* Submitted December 1, 2021*/

//This is the main oss application that randomly creates child processes which in turn exec user_proc application

struct msgqueue{

    long int msgtype;   //to store processID
    char msgcontent[20]; //to store address and read/write bit
};

struct processControlBlock{ //process control block

    int pID;
    int pageNumber[32];
    int dirtyBit[32];
    int numberOfMemoryRequest;
    int numOfMemoryPagefault; //resulting in blocked state and page fault
    int numOfMemoryGranted;
};

/*struct processPageTable{    //struct to keep page table
    
    int pID;
    int pageNumber[32];
    int dirtyBit[32];
};*/

int frameNumber[256];   //frame table to simulate main memory
int framepermissionFlag[256]; //to track memory access permissions; 0 for read, 1 for write
int blockedQueue[18];   //to store blocked processes

int memoryRequestMessageQueueID;  //message queue ID

int processID[max_number_of_processes];

void initclock(void);   //function to initialize the two clocks in shared memory

void cleanUp(void);

void siginthandler(int);    //handles Ctrl+C interrupt

void timeouthandler(int sig);   //timeout handler function declaration

unsigned int ossclockid, *ossclockaddress, *osstimeseconds, *osstimenanoseconds; //used for ossclock shared memory

unsigned int ossofflinesecondclock; unsigned int ossofflinenanosecondclock; //used as offline clock before detaching from the oss clock shared memory

int randomTime;

char *logfilename = "logfile.log"; char logstring[4096]; 

int main(int argc, char *argv[]){   //start of main() function

    int msgrcverr, msgsnderr; char memoryRequest[20]; char resourceMessageCopy[20]; char permission[15];

    char *resourceNum, *resourceID; char *userIDToken, *memoryAddressToken, *permissionToken;

    long int mtype; int pid; int pidIndex;

    signal(SIGINT, siginthandler); //handles Ctrl+C signal inside OSS only     

    signal(SIGALRM, timeouthandler); //handles the timeout signal

    alarm(oss_run_timeout); //fires timeout alarm after oss_run_timeout seconds defined in the config.h file

    struct msgqueue userMemoryRequest;    //to communicate memory request and termination messages with the user process

    printf("\nMaster Process ID is %d\n", getpid());
    
    //this block initializes the ossclock

    ossclockid = shmget(ossclockkey, 8, IPC_CREAT|0766); //getting the oss clock shared memory clock before initializing it so that the id can become available to the child processes

    if (ossclockid == -1){  //checking if shared memory id was successfully created

        perror("\noss: Error: In oss main function, ossclockid cannot be created; shmget() call failed");
        exit(1);
    }
    snprintf(logstring, sizeof(logstring), "\nMaster clock initializing at 0:0\n");

    logmsg(logfilename, logstring); //calling logmsg() to write to file

    initclock(); //calls function to initialize the oss seconds and nanoseconds clocks

    snprintf(logstring, sizeof(logstring), "\nMaster clock completed initialization at %hu:%hu\n", *osstimeseconds, *osstimenanoseconds+=200);

    logmsg(logfilename, logstring); //calling logmsg() to write to file

    printf("\nMaster clock initialized at %hu:%hu\n", *osstimeseconds, *osstimenanoseconds); //print out content of ossclock after initialization

    //setting up message queue here

    memoryRequestMessageQueueID = msgget(message_queue_key, IPC_CREAT|0766); //creates the message queue and gets the queue id

    if (memoryRequestMessageQueueID == -1){  //error checking the message queue creation

        perror("\nError: Master in Parent Process. Message queue creation failed\n");

        exit(1);
    }

    //end of message queue setup

    strcpy(logstring, "");

    snprintf(logstring, sizeof(logstring), "\nMaster randomly generating user processes at %hu:%hu ......\n", *osstimeseconds+=1, *osstimenanoseconds);

    logmsg(logfilename, logstring); //calling logmsg() to write to file

    printf("%s", logstring);
    
    for (int count = 0; count < 2; count++){       //randomly create user processes in this block

        pid = fork();

        if (pid < 0){
                perror("\nError: Master in main() function. fork() failed!");
                exit(1);
            }

        if (pid > 0){

            pid = pid;
        }

        if (pid == 0){  //child process was created

            printf("\nUser Process %d was created\n", getpid());

            execl("./user_proc", "./user_proc", NULL);      //execute user process
        }

        randomTime = randomNumber(1,500);    //generate random number between 1 and 500ms

        randomTime = randomTime * 100000000; //to nansosec

        *osstimenanoseconds+=randomTime;    //increment oss nanosecond by randomTime

        printf("\nMaster created User Process %d at %hu:%hu\n", pid, *osstimeseconds, *osstimenanoseconds);
    }  

    while (1){

        printf("\nMaster listening for memory request or process termination\n");

        mtype = 0; strcpy(permission, "");

        snprintf(logstring, sizeof(logstring), "\nMaster: Listening for Memory Access Requests %hu:%hu\n", *osstimeseconds+=1, *osstimenanoseconds);

        logmsg(logfilename, logstring); //calling logmsg() to write to file

        //sleep(2);

        msgrcverr = msgrcv(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), getpid(), 0); //read master's from message queue without waiting

        if (msgrcverr == -1){ //error checking msgrcverror()

            perror("\nMaster in oss main() function. msgrcv() failed!");
            exit(1);

            //break;
        }
        //decode the message content to extract the user processID, memory address and permission

        printf("\nMaster received memory request from message queue\n");

        strcpy(resourceMessageCopy, userMemoryRequest.msgcontent);    //copy the memory address and r/w bit

        userIDToken = strtok(resourceMessageCopy, " "); memoryAddressToken = strtok(NULL, " "); permissionToken = strtok(NULL, " "); //extract the message tokens from msgcontent
        //strcpy(memoryRequest, memoryAddressToken);

        if (strtol(permissionToken, NULL, 10) == 0){

            strcpy(permission, "read");
            snprintf(logstring, sizeof(logstring), "\nMaster: PID %s is requesting %s of memory address %s at %hu:%hu\n", userIDToken, permission, memoryAddressToken, *osstimeseconds, *osstimenanoseconds+=5);
            logmsg(logfilename, logstring); //calling logmsg() to write to file
        }
        else if(strtol(permissionToken, NULL, 10) == 1){

            strcpy(permission, "write");
            snprintf(logstring, sizeof(logstring), "\nMaster: PID %s is requesting %s of memory address %s at %hu:%hu\n", userIDToken, permission, memoryAddressToken, *osstimeseconds, *osstimenanoseconds+=5);
            logmsg(logfilename, logstring); //calling logmsg() to write to file
        }

        else if(strtol(permissionToken, NULL, 10) == -1){

            strcpy(permission, "termination");
            snprintf(logstring, sizeof(logstring), "\nMaster: PID %s is informing Master of its termination at %hu:%hu\n", userIDToken, *osstimeseconds, *osstimenanoseconds+=5);
            logmsg(logfilename, logstring); //calling logmsg() to write to file
            kill(strtol(userIDToken,NULL,10), SIGKILL); //handle clearing of PCB and Frame table here
            continue;
        }        

        mtype = strtol(userIDToken, NULL, 10); //store message type extracted above as int; shoudl be the user processID    

        printf("\nInside master, user PID is %d, memory requested is %s and permission token is %s\n", mtype, memoryAddressToken, permissionToken);

        userMemoryRequest.msgtype = mtype; strcpy(userMemoryRequest.msgcontent, "1");

        printf("oss sending message type %d and message content %s to user process\n", userMemoryRequest.msgtype, userMemoryRequest.msgcontent);

        sleep(10);

        msgsnderr = msgsnd(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), IPC_NOWAIT);   //send message granted to user process  

        snprintf(logstring, sizeof(logstring), "\nMaster: Memory request for address %s was granted to PID %s at %hu:%hu\n", memoryAddressToken, userIDToken, *osstimeseconds, *osstimenanoseconds+=3);

        logmsg(logfilename, logstring); //calling logmsg() to write to file      
    }

    //cleanUp();      //call cleanup before exiting main() to free up used resources*/

    snprintf(logstring, sizeof(logstring), "\nMaster: No more requests. Master completed execution at %hu:%hu\n", ossofflinesecondclock+=1, ossofflinenanosecondclock+=05);

    logmsg(logfilename, logstring); //calling logmsg() to write to file

    return 0;

}   //end of main function


//initclock() function initializes the ossclock 

void initclock(){ //initializes the seconds and nanoseconds parts of the oss

    ossclockaddress = shmat(ossclockid, NULL, 0); //shmat returns the address of the shared memory
    if (ossclockaddress == (void *) -1){

        perror("\nMaster in initclock(), ossclockaddress not returned by shmat()");
        exit(1);

    }

    osstimeseconds = ossclockaddress + 0;   //the first 4 bytes of the address stores the seconds part of the oss clock, note the total address space is for 8 bytes from shmget above
    osstimenanoseconds = ossclockaddress + 1;   //the second 4 bytes of the address stores the seconds part of the oss clock

    *osstimeseconds = 0;    //storing integer data in the seconds part of the oss clock
    *osstimenanoseconds = 0;    //storing int data in the nanoseconds part of the oss clock

}   //end of initclock()


void cleanUp(void){ //frees up used resources including shared memory

    char pidToString[6];

    printf("\nCleaning up used resources....\n");

    snprintf(logstring, sizeof(logstring), "\nMaster cleaning up resources at %hu:%hu\n", *osstimeseconds, *osstimenanoseconds+=5);
    logmsg(logfilename, logstring);

    // first kill all user processes that are still holding resources

    strcpy(logstring, "");
    snprintf(logstring, sizeof(logstring), "\nMaster stopping all pending user processes at %hu:%hu\n", *osstimeseconds, *osstimenanoseconds+=2);
    logmsg(logfilename, logstring);
    printf("%s", logstring);

        for (int index = 0; index < max_number_of_processes; index++){

                if (processID[index] > 0){    //if there a process ID in this location

                    //sprintf(pidToString, "%d", processID[index]);   //convert process id to string

                    //printf("\nStopping user process %d\n", processID[index]);

                    //snprintf(logstring, sizeof(logstring), "\nMaster stopping Process %s at %hu:%hu\n", pidToString, *osstimeseconds, *osstimenanoseconds+=1);
                    //logmsg(logfilename, logstring);

                    kill(processID[index], SIGKILL);
                }
        }
    strcpy(logstring, "");
    snprintf(logstring, sizeof(logstring), "\nMaster stopped all pending user processes at %hu:%hu\n", *osstimeseconds+=2, *osstimenanoseconds+=10);
    logmsg(logfilename, logstring);
    printf("%s", logstring);
    ossofflinesecondclock = *osstimeseconds; ossofflinenanosecondclock = *osstimenanoseconds; //save the clock before detaching from clock shared memory

    if ((shmdt(ossclockaddress)) == -1){    //detaching from the oss clock shared memory

        perror("\nMaster in cleanUp() function, OSS clock address shared memory cannot be detached");
        exit(1);
    }

    printf("\nMaster clock shared memory was detached.\n");

    if (shmctl(ossclockid, IPC_RMID, NULL) != 0){      //shmctl() marks the oss clock shared memory for destruction so it can be deallocated from memory after no process is using it
        perror("\nMaster in cleanUp() function, OSS clockid shared memory segment cannot be marked for destruction\n"); //error checking shmctl() call
        exit(1);
    }

    printf("\nMaster clock shared memory ID %hu was deleted.\n\n", ossclockid);

    ossofflinesecondclock+=1; ossofflinenanosecondclock+=25;

    snprintf(logstring, sizeof(logstring), "\nMaster Clock Shared Memory ID %hu has been detached and deleted at %hu:%hu\n", ossclockid, ossofflinesecondclock, ossofflinenanosecondclock);
    logmsg(logfilename, logstring);

    if ( msgctl(memoryRequestMessageQueueID, IPC_RMID, 0) == 0)
        printf("\nMessage Queue ID %d has been removed.\n", memoryRequestMessageQueueID);

    else{    
        printf("\nErrror: Master in cleanUp(), Message Queue removal failed!\n");
        exit(1);
    }

    snprintf(logstring, sizeof(logstring), "\nMaster removed Messaqe Queue ID %d at %hu:%hu\n", memoryRequestMessageQueueID, ossofflinesecondclock, ossofflinenanosecondclock+=15);
    logmsg(logfilename, logstring);


}   //end of cleanUP()


void siginthandler(int sigint){

        printf("\nMaster: Ctrl+C interrupt received. In siginthandler() handler. Aborting Processes..\n");

        snprintf(logstring, sizeof(logstring), "\nMaster in Signal Handler; Ctrl+C interrupt received at %hu:%hu\n", *osstimeseconds+=1, *osstimenanoseconds);
        logmsg(logfilename, logstring);

        cleanUp();  //calling cleanUp() before terminating oss

        snprintf(logstring, sizeof(logstring), "\nMaster terminating at %hu:%hu\n", ossofflinesecondclock+=1, ossofflinenanosecondclock+=15);
        logmsg(logfilename, logstring);
        
        kill(getpid(), SIGTERM);

        exit(1);
}   //end of siginthandler()


void timeouthandler(int sig){   //this function is called if the program times out after oss_run_timeout seconds. Handle killing child processes and freeing resources in here later

    printf("\nMaster timed out. In timeout handler. Aborting Processes..\n");

    snprintf(logstring, sizeof(logstring), "\nMaster timed out at %hu:%hu", *osstimeseconds+=1, *osstimenanoseconds+=5);
    logmsg(logfilename, logstring);

    cleanUp(); //call cleanup to free up used resources
    snprintf(logstring, sizeof(logstring), "\nMaster terminating at %hu:%hu", ossofflinesecondclock+=1, ossofflinenanosecondclock+=5);
    logmsg(logfilename, logstring);

    kill(getpid(), SIGTERM);

    exit(1);

}   //end of timeouthandler()
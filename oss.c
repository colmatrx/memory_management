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

    int pID;    //default is 0
    int numberOfMemoryRequest;  //default is 0
    int numOfMemoryPagefault; //resulting in blocked state and page fault deafult is 0
    int pageNumber[32]; //default is -1
    int dirtyBit[32];   //default is 0 
};
struct processControlBlock pageTable[max_number_of_processes];  //process and page table for 18 processes

struct frame{
int frameIndex[256];   //frame table to simulate main memory; all initialized to -1
int framePermission[256]; //to track memory access permissions; 0 for read, 1 for write; all initialized to -1
int frameFIFO[256];    //to track the FIFI index for swapping
};
struct frame frameTable;


int blockedQueue[18];   //to store blocked processes

int memoryRequestMessageQueueID;  //message queue ID

int processID[max_number_of_processes];

void initclock(void);   //function to initialize the two clocks in shared memory

void cleanUp(void);

void siginthandler(int);    //handles Ctrl+C interrupt

void timeouthandler(int sig);   //timeout handler function declaration

void displayFrameTable(void);

unsigned int ossclockid, *ossclockaddress, *osstimeseconds, *osstimenanoseconds; //used for ossclock shared memory

unsigned int ossofflinesecondclock; unsigned int ossofflinenanosecondclock; //used as offline clock before detaching from the oss clock shared memory

int randomTime;

char *logfilename = "logfile.log"; char logstring[4096]; 

int main(int argc, char *argv[]){   //start of main() function

    int msgrcverr, msgsnderr, pageRequest = -1; char memoryRequest[20]; char resourceMessageCopy[20]; char permission[15];

    char *resourceNum, *resourceID; char *userIDToken, *memoryAddressToken, *permissionToken;

    long int mtype; int pid; int pidIndex; int fifoCount = 0;

    signal(SIGINT, siginthandler); //handles Ctrl+C signal inside OSS only     

    signal(SIGALRM, timeouthandler); //handles the timeout signal

    alarm(oss_run_timeout); //fires timeout alarm after oss_run_timeout seconds defined in the config.h file

    //initiallize the process table and page table here
    for (int index = 0; index < max_number_of_processes; index++){

        pageTable[index].pID = 0;
        pageTable[index].numberOfMemoryRequest = 0;
        pageTable[index].numOfMemoryPagefault = 0;

        for(int i = 0; i < 32; i++){    //initialize the page table and corresponding page dirty bit
            pageTable[index].pageNumber[i] = -1;
            pageTable[index].dirtyBit[i] = 0;
        }
    }

    //initialize frame table here

    for (int i = 0; i < 256; i++){

        frameTable.frameIndex[i] = -1;
        frameTable.framePermission[i] = -1;
        frameTable.frameFIFO[i] = 0;
    }

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

            pageTable[count].pID = pid; //set the pid in the process_page table
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

        displayFrameTable();

        printf("\nMaster listening for memory request or process termination\n");

        mtype = 0; strcpy(permission, ""); int memoryAddressTokenInt = -1; pageRequest = -1; int permissionTokenInt = -1; int frameNumber;

        snprintf(logstring, sizeof(logstring), "\nMaster: Listening for Memory Access Requests %hu:%hu\n", *osstimeseconds+=1, *osstimenanoseconds);

        logmsg(logfilename, logstring); //calling logmsg() to write to file

        msgrcverr = msgrcv(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), getpid(), 0); //read master's from message queue without waiting

        if (msgrcverr == -1){ //error checking msgrcverror()

            perror("\nMaster in oss main() function. msgrcv() failed!");
            exit(1);
        }
        //decode the message content to extract the user processID, memory address and permission

        printf("\nMaster received memory request from message queue\n");

        strcpy(resourceMessageCopy, userMemoryRequest.msgcontent);    //copy the memory address and r/w bit

        userIDToken = strtok(resourceMessageCopy, " "); memoryAddressToken = strtok(NULL, " "); permissionToken = strtok(NULL, " "); //extract the message tokens from msgcontent

        //calculate the page referenced from the memory address
        memoryAddressTokenInt = strtol(memoryAddressToken, NULL, 10);
        pageRequest = memoryAddressTokenInt / 1024; //gets the Page that holds the main memory frame address

        permissionTokenInt = strtol(permissionToken, NULL, 10); //converts permissionToken to int

        mtype = strtol(userIDToken, NULL, 10); //converts the user process ID to int  

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

            float avgMemoryAccessSpeed, totalMemoryAccessTime; char frameToString[5];

            strcpy(permission, "termination");
            kill(strtol(userIDToken,NULL,10), SIGKILL); //be sure to kill the child process
            snprintf(logstring, sizeof(logstring), "\nMaster: PID %s has informed Master of its termination at %hu:%hu\n", userIDToken, *osstimeseconds, *osstimenanoseconds+=5);
            logmsg(logfilename, logstring); //calling logmsg() to write to file

            //print out the stats here 
            for (int i = 0; i < max_number_of_processes; i++){  //traverse the process table to find the pid and the requested Page

                if (pageTable[i].pID == mtype){ //true if pid is found in process table 

                    snprintf(logstring, sizeof(logstring), "\nMaster: PID %s stats:\n", userIDToken);
                    logmsg(logfilename, logstring); //calling logmsg() to write to file
                    snprintf(logstring, sizeof(logstring), "\nTotal memory requests: %d\n", pageTable[i].numberOfMemoryRequest);
                    logmsg(logfilename, logstring); //calling logmsg() to write to file
                    snprintf(logstring, sizeof(logstring), "\nTotal memory page faults: %d\n", pageTable[i].numOfMemoryPagefault);
                    logmsg(logfilename, logstring); //calling logmsg() to write to file
                    snprintf(logstring, sizeof(logstring), "\nTotal non-memory page faults: %d\n", pageTable[i].numberOfMemoryRequest - pageTable[i].numOfMemoryPagefault);
                    logmsg(logfilename, logstring); //calling logmsg() to write to file
                    totalMemoryAccessTime = (0.00000001 * (pageTable[i].numberOfMemoryRequest - pageTable[i].numOfMemoryPagefault)) + (0.000000014 * pageTable[i].numOfMemoryPagefault);
                    avgMemoryAccessSpeed = pageTable[i].numberOfMemoryRequest / totalMemoryAccessTime;
                    snprintf(logstring, sizeof(logstring), "\nAverage memory access speed: %.2f frames per second\n", avgMemoryAccessSpeed);
                    logmsg(logfilename, logstring); //calling logmsg() to write to file

                    //now reset the process control block and free the frames used by the process
                    strcpy(logstring, ""); strcat(logstring, "\nFrames -> ");
                    pageTable[i].numberOfMemoryRequest = 0; //set the default value
                    pageTable[i].numOfMemoryPagefault = 0; //set the default value
                    for (int index = 0; index < 32; index++){   //traverse the page table to clear the dirtyBit and free the corresponding frames inside page table

                        pageTable[i].dirtyBit[index] = 0;   //clear the dirtyBit
                        if (pageTable[i].pageNumber[index] > -1){   //if a frame address is tored in the page table location

                            frameTable.frameIndex[pageTable[i].pageNumber[index]] = -1; //reset the frame
                            frameTable.framePermission[pageTable[i].pageNumber[index]] = -1;    //reset the corresponding frame permission
                            frameTable.frameFIFO[pageTable[i].pageNumber[index]] = 0;   //reset the fifo counter
                            sprintf(frameToString, "%d", pageTable[i].pageNumber[index]);
                            strcat(logstring, frameToString); strcat(logstring, ", ");
                        }
                    } 
                    strcat(logstring, " have been cleared in the Frame Table\n")                   ;
                    logmsg(logfilename, logstring); //calling logmsg() to write to file
                    break;
                }
            }          
            continue;
        }          

        //Process the memory request here
        for (int i = 0; i < max_number_of_processes; i++){  //traverse the process table to find the pid and the requested Page

            if (pageTable[i].pID == mtype){ //true if pid is found in process table    
                pageTable[i].numberOfMemoryRequest+=1;  //increment the number of times process has requested memory access

                if (pageTable[i].pageNumber[pageRequest] == -1){ //if page number does not contain any Frame number ie page fault

                    pageTable[i].numOfMemoryPagefault+=1;   //increment the number of times process has had a page fault

                    //this is a page fault condition, block the process here and perform a page swap
                    userMemoryRequest.msgtype = mtype; strcpy(userMemoryRequest.msgcontent, "0");

                    sleep(5);

                    msgsnderr = msgsnd(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), IPC_NOWAIT);   //send message granted to user process
                    
                    snprintf(logstring, sizeof(logstring), "\nMaster: Page Fault; Memory Address %s not in Frame Table; Process %s is considered blocked at %hu:%hu\n", memoryAddressToken, userIDToken, *osstimeseconds, *osstimenanoseconds+=1);

                    logmsg(logfilename, logstring); //calling logmsg() to write to file

                    //now scan the frame table for available slot to simulate memory access
                    for (int frameNumber = 0; frameNumber < 256; frameNumber++){   //find an available frame in Frame Table ie no swapping

                        if (frameTable.frameIndex[frameNumber] == -1){      //true for an empty frame

                            frameTable.frameIndex[frameNumber] = pageTable[i].pID;  //store the calling processID in the frame to simulate which process is using it
                            frameTable.framePermission[frameNumber] = permissionTokenInt;   //store read/write permission for the frame
                            frameTable.frameFIFO[frameNumber] = fifoCount++;  //to track the order in which frames are filled to use for FIFO swapping
                            pageTable[i].pageNumber[pageRequest] = frameNumber; //keep the main memory frame number in the process page table index
                            pageTable[i].dirtyBit[pageRequest] = permissionTokenInt; //store the permission; 1 indicates dirtyBit is set                            

                                snprintf(logstring, sizeof(logstring), "\nMaster: Empty Frame found, no sawpping needed; Fetching data from secondary storage at %hu:%hu\n", *osstimeseconds, *osstimenanoseconds);

                                logmsg(logfilename, logstring); //calling logmsg() to write to file

                                userMemoryRequest.msgtype = mtype; strcpy(userMemoryRequest.msgcontent, "1");
                                msgsnderr = msgsnd(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), IPC_NOWAIT);   //send message granted to user process
                                snprintf(logstring, sizeof(logstring), "\n Master: %s permission granted to PID %s on memory address %s in Frame %d at %hu:%hu\n", permission, userIDToken, memoryAddressToken, frameNumber, *osstimeseconds, *osstimenanoseconds+=14);

                                logmsg(logfilename, logstring); //calling logmsg() to write to file                            
                                break; //break out of the loop once an empty frame is found
                        }

                        else{   //if no empty frame is found

                                //implement frame swapping here
                        }
                    }
                }//end of page fault if block

                else{   //no page fault; page table contains the frame of the main memory address

                        userMemoryRequest.msgtype = mtype; strcpy(userMemoryRequest.msgcontent, "1");

                        printf("No Page fault; Master sending message type %d and message content %s to user process\n", userMemoryRequest.msgtype, userMemoryRequest.msgcontent);

                        sleep(5);

                        msgsnderr = msgsnd(memoryRequestMessageQueueID, &userMemoryRequest, sizeof(userMemoryRequest), IPC_NOWAIT);   //send message granted to user process  

                        snprintf(logstring, sizeof(logstring), "\nMaster: Master to PID %s -> Memory Address %s is in Frame %d at %hu:%hu\n", userIDToken, memoryAddressToken, pageTable[i].pageNumber[pageRequest],  *osstimeseconds, *osstimenanoseconds+=10);

                        logmsg(logfilename, logstring); //calling logmsg() to write to file 

                        break;  //break out of the for loop if page has been found with the frame number inside  
                }   //end of page fault else block

                break;
            }
            
        }

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

void displayFrameTable(void){   //function to display the frame table

    char indexToString[14]; char ownerToString[14]; char permissionToString[19];
 
    strcpy(logstring, ""); strcpy(indexToString, ""); strcpy(ownerToString, ""); strcpy(permissionToString, "");
    snprintf(logstring, sizeof(logstring), "\nMaster Displaying Frame Table at %hu:%hu\n\n", *osstimeseconds, *osstimenanoseconds);
    logmsg(logfilename, logstring); //calling logmsg() to write to file

    strcpy(logstring, "");
    strcat(logstring, "|Frame Index|\t\t"); strcat(logstring, "|  Frame Owner |\t\t"); strcat(logstring, "|Frame Permission|\n");
    logmsg(logfilename, logstring); //calling logmsg() to write to file

    strcpy(logstring, "");
    for (int i = 0; i < 20; i++){

        printf("\nindex is %d, owner is %d and permission is %d", i, frameTable.frameIndex[i], frameTable.framePermission[i]);
        sprintf(indexToString, "%d", i); //converts int to string
        sprintf(ownerToString, "%d", frameTable.frameIndex[i]);
        if (frameTable.framePermission[i] == 0)
            strcpy(permissionToString, "read");

        if (frameTable.framePermission[i] == 1)
            strcpy(permissionToString, "write");

        if (frameTable.framePermission[i] == -1)
            strcpy(permissionToString, "");

        strcat(logstring, "\t");
        strcat(logstring, indexToString); strcat(logstring, "\t\t\t\t\t"); strcat(logstring, "PID ");
        strcat(logstring, ownerToString); strcat(logstring, "\t\t\t\t");
        strcat(logstring, permissionToString); strcat(logstring, "\n");
    }
    logmsg(logfilename, logstring); //calling logmsg() to write to file
}
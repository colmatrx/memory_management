#include<stdio.h>
#include <sys/ipc.h>
#include<sys/types.h>
#include<sys/shm.h>
#include<unistd.h>
#include<string.h>
#include<strings.h>
#include<time.h>
#include<sys/wait.h>
#include<sys/msg.h>
#include<signal.h>
#include<stdlib.h>
#include "config.h"

struct msgqueue{

    long int msgtype;
    char msgcontent[20];
};

int resourceMessageID; unsigned int ossclkid, *ossclockaddr, *osstimesec, *osstimensec;

/*Author Idris Adeleke CS4760 Project 6 - Memory Management*/

/* Submitted December 15, 2021*/

//This is the user_proc application that gets called by the execl command inside oss when a child process is successfully forked

int main(int argc, char *argv[]){

    ossclkid = shmget(ossclockkey, 8, 0); //gets the id of the shared memory clock
    ossclockaddr = shmat(ossclkid, NULL, 0); //shmat returns the address of the shared memory

    int memoryAddress, memoryPermission, masterPID; 
    char memoryAddressToString[6], memoryPermissionToString[2], completeMemoryAddress[20], permission[6], userPIDToSTring[7], masterPIDToString[7];

    struct msgqueue resourceMessageChild;    //to communicate memory request and termination messages with the master

    masterPID = getppid(); //get the parent process ID

    sprintf(masterPIDToString, "%d", masterPID);    //converts user masterPID from int to string

    sprintf(userPIDToSTring, "%d", getpid());    //converts user pid from int to string

    int msgsnderror, msgrcverror; char msgreceived[20];

    resourceMessageID = msgget(message_queue_key, 0);

    if (ossclockaddr == (void *) -1){

        perror("\nuser process cannot attach to ossclock");
        exit(1);

    }

    osstimesec = ossclockaddr + 0;   //the first 4 bytes of the address stores the seconds part of the oss clock, note the total address space is for 8 bytes from shmget above
    osstimensec = ossclockaddr + 1;   //the second 4 bytes of the address stores the seconds part of the oss clock

    printf("\nppid() inside user process is %d\n", getppid());

    while(1){

        strcpy(completeMemoryAddress, ""); strcpy(permission, "");

        memoryAddress = ((randomNumber(0, 31))*1024) + randomNumber(0,1023);    //generate memory address

        memoryPermission = randomNumber(0,1); //generate memory read/write permission

        if (memoryPermission == 0){     //set read/write permission

            strcpy(permission, "read");
        }

        else
            strcpy(permission, "write");

        sprintf(memoryAddressToString, "%d", memoryAddress);    //converts memory address from int to string

        sprintf(memoryPermissionToString, "%d", memoryPermission); //convert memory permission from int to string

        strcat(completeMemoryAddress, userPIDToSTring); strcat(completeMemoryAddress, " "); strcat(completeMemoryAddress, memoryAddressToString); 
        strcat(completeMemoryAddress, " "); strcat(completeMemoryAddress, memoryPermissionToString); //construct a message like "PID1234 0x1234 1"    
        
        strcpy(resourceMessageChild.msgcontent, completeMemoryAddress);    //copies memory address into message content

        resourceMessageChild.msgtype = masterPID;    //initialize msgtype with PID of master so only master can read it

        msgsnderror = msgsnd(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), 0);

        if (msgsnderror == -1){ //error checking msgsnd()

            perror("\nError: In user_proc(). msgsnd() failed!");

            exit(1);
        }

        printf("\nUser Process %d is requesting memory address %d and %s permission from Master at %hu:%hu\n", getpid(), memoryAddress, permission, *osstimesec, *osstimensec);

        sleep(10);

        msgrcverror = msgrcv(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), getpid(), 0); //receive message back from master

        if (msgrcverror == -1){ //error checking msgrcverror()

            perror("\nError: In user_proc(). msgrcv() failed!");

            exit(1);
        }
        strcpy(msgreceived, resourceMessageChild.msgcontent);

        if (strcmp(msgreceived, "0") == 0){    //if resource not granted

            printf("\nMemory access not granted by Master...User Process %d in blocked state\n", getpid());

            //implement blocked state condition here, keep waiting to receive a memory resource grant
                while(1){

                    msgrcverror = msgrcv(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), getpid(), 0); //receive message back from master  

                    if (msgrcverror == -1){ //error checking msgrcverror()

                        perror("\nError: In user_proc() blocked state. msgrcv() failed!");

                        exit(1);
                    }
                    if (strcmp(resourceMessageChild.msgcontent, "0") == 0)  //if still blocked
                        continue;
                    else
                        break;
                }
                printf("\nMemory access now granted by Master...User Process %d out of blocked state\n", getpid());
        } 

        else if (strcmp(msgreceived, "1") == 0){ //if memory is granted

            printf("\nMemory address %d with %s permission granted by Master to Process %d\n", memoryAddress, permission, getpid());
        
            int rnd = randomNumber(100, 1100);  //generate a random number to decide whether to terminate or continue to request memory addresses

            if (rnd > 900){ //this is a condition to terminate by sending -1 as permission to oss

                strcpy(completeMemoryAddress, "");
                strcat(completeMemoryAddress, userPIDToSTring); strcat(completeMemoryAddress, " "); strcat(completeMemoryAddress, "00000"); strcat(completeMemoryAddress, " ");
                strcat(completeMemoryAddress, "-1");    //construct a special termination message

                resourceMessageChild.msgtype = masterPID; strcpy(resourceMessageChild.msgcontent, completeMemoryAddress);

                msgsnderror = msgsnd(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), 0);
                if (msgsnderror == -1){ //error checking msgsnd()

                    perror("\nError: In user_proc(). msgsnd() failed!");

                    exit(1);
                }
                printf("\nprocess %d is terminating at %hu:%hu\n", getpid(), *osstimesec, *osstimensec);
                break;
            }
            else
                continue;
        }
    }

    printf("\nProcess %d completed user process execution\n", getpid());
    exit(0);
}
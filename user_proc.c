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
    char msgcontent[8];
};

int resourceMessageID; unsigned int ossclkid, *ossclockaddr, *osstimesec, *osstimensec;

/*Author Idris Adeleke CS4760 Project 6 - Memory Management*/

/* Submitted December 15, 2021*/

//This is the user_proc application that gets called by the execl command inside oss child processes

int main(int argc, char *argv[]){

    ossclkid = shmget(ossclockkey, 8, 0); //gets the id of the shared memory clock
    ossclockaddr = shmat(ossclkid, NULL, 0); //shmat returns the address of the shared memory
    if (ossclockaddr == (void *) -1){

        perror("\nuser process cannot attach to ossclock");
        exit(1);

    }

    osstimesec = ossclockaddr + 0;   //the first 4 bytes of the address stores the seconds part of the oss clock, note the total address space is for 8 bytes from shmget above
    osstimensec = ossclockaddr + 1;   //the second 4 bytes of the address stores the seconds part of the oss clock

    struct msgqueue resourceMessageChild;    //to communicate memory request and termination messages with the master

    int msgsnderror, msgrcverror; char msgreceived[8];

    resourceMessageID = msgget(message_queue_key, 0);
    
    strcpy(resourceMessageChild.msgcontent, "33780 1");    //copies user_rname into message content

    resourceMessageChild.msgtype = getpid();    //initialize msgtype with user process' PID

    msgsnderror = msgsnd(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), 0);

    if (msgsnderror == -1){ //error checking msgsnd()

        perror("\nError: In user_proc(). msgsnd() failed!");

        exit(1);
    }

    printf("\nUser Process %d is requesting memory address %s from Master at %hu:%hu\n", getpid(), resourceMessageChild.msgcontent, *osstimesec, *osstimensec);

    sleep(10);

    while(1){

        msgrcverror = msgrcv(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), getpid(), 0); //receive message back from master

        if (msgrcverror == -1){ //error checking msgrcverror()

            perror("\nError: In user_proc(). msgrcv() failed!");

            exit(1);
        }
        strcpy(msgreceived, resourceMessageChild.msgcontent);

        if (strcmp(msgreceived, "0") == 0){    //if resource not granted

            printf("\nResource not granted by Master...User Process %d exiting\n", getpid());

            exit(1);
        } 

        else if (strcmp(msgreceived, "1") == 0)   //if resource is granted
            break;      //break out of the loop if resource i granted

        else if (strcmp(msgreceived, "33780 1") == 0){ //if you read your own message back before master could read it, reconstruct it and resend it to the queue

            printf("\nuser read back its own message, now writing it back\n");

            resourceMessageChild.msgtype = getpid();

            strcpy(resourceMessageChild.msgcontent, "33780 1");    //copies user_rname into message content

            msgsnderror = msgsnd(resourceMessageID, &resourceMessageChild, sizeof(resourceMessageChild), 0);

            if (msgsnderror == -1){ //error checking msgsnd()

                perror("\nError: In user_proc(). msgsnd() failed!");

                exit(1);
            }

            continue;
        }
    }   

    printf("\nResource %s granted by Master to Process %d\n", "33780 1", getpid());

    printf("\nProcess %d completed user process execution\n", getpid());

    return 0;
}
#include<stdio.h>
#include<stdlib.h>
#include <sys/ipc.h>
#include<sys/types.h>
#include<unistd.h>
#include<string.h>
#include<strings.h>
#include<time.h>
#include<sys/wait.h>
#include<signal.h>
#include<sys/msg.h>
#include<sys/stat.h>
#include<getopt.h>
#include"config.h"

void logmsg(char *filename, const char *msg){       //this function is for writing to logfile

    FILE *filedescriptor;

    filedescriptor = fopen(filename, "a"); //open logfile in append mode

    if (filedescriptor == NULL){
        perror("\noss: Error: Log File cannot be opened\n");
        exit(1);
    }

    fputs(msg, filedescriptor);    //writes log message to file
    fclose(filedescriptor);
}

int randomNumber(int lowertimelimit, int uppertimelimit){

    sleep(1);   //sleeping a little ensures the same number is not generated successively
    int randNum = 0;
    srand(time(NULL));          //initilize the rand function
    randNum = (rand() % ((uppertimelimit - lowertimelimit) + 1)) + lowertimelimit; //this logic produces a number between lowertimelit and uppertimelimit
    return randNum;

}   //end of randomNumber()
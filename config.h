#define max_number_of_processes 18  //maximum number of processes allowed in the system
#define oss_run_timeout 180 //time for the oss process to wait before killing all child processes and freeing up resources
#define resourceTableKey 112721
#define ossclockkey 1980725
#define message_queue_key 140699
void logmsg(char *filename, const char *msg);
int randomNumber(int lowertimelimit, int uppertimelimit);
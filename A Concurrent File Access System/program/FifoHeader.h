#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#define BUFF_SIZE 4096

#define SERVER_FIFO_TEMPLATE "/tmp/fifo_sv.%ld"
#define CLIENT_FIFO_TEMPLATE "/tmp/fifo_cl.%ld"

#define SERVER_FIFO_NAME_LEN (sizeof(SERVER_FIFO_TEMPLATE) + 20)
#define CLIENT_FIFO_NAME_LEN (sizeof(CLIENT_FIFO_TEMPLATE) + 20)

#define CLIENT_SEMA_TEMPLATE "semA_cl.%ld"
#define CLIENT_SEMB_TEMPLATE "semB_cl.%ld"
#define CLIENT_SEM_NAME_LEN (sizeof(CLIENT_SEMA_TEMPLATE) + 20)

#define RESPONSE_LENGTH 1024*5

#define RESPONSE_OK 1
#define RESPONSE_CON 0
#define RESPONSE_QUIT -1

#define PATH_LEN 512

char serverPath[256];

struct request
{
    pid_t pid;
    int connectMode;
    char clientPath[PATH_LEN];
};
struct response
{
    int status;
    char arr[RESPONSE_LENGTH];
};
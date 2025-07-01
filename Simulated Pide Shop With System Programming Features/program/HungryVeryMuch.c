#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include "utility.h"
#include <errno.h>
#include <signal.h>

#define BUFFER_SIZE 1024

int serverPid = 0;

void sig_handler(int sigNum)
{
    printf(" signal .. cancelling orders.. editing log..\n");
    if (serverPid != 0)
    {
        kill(serverPid, SIGUSR1);
    }
}
void sigusr1_handler(int sig) {
    printf("Shop has been burned.\n");
    exit(0);
}
int main(int argc, char *argv[]) {
    if(argc != 6) {
        printf("Usage: %s <ipnumber> <portnumber> <numberOfOrders> <p> <q>\n", argv[0]);
        exit(0);
    }
    
    const char *ip_address = argv[1];
    int port = atoi(argv[2]);
    int numberOfOrders = atoi(argv[3]);
    int p = atoi(argv[4]);
    int q = atoi(argv[5]);

    int a;
    //SIGINT handler
    struct sigaction sa_action={0};
    sigemptyset(&sa_action.sa_mask);
    sa_action.sa_handler=sig_handler;
    sa_action.sa_flags=0;
    while(((a=sigaction(SIGINT, &sa_action, NULL))==-1) && errno==EINTR);
    if(a==-1)
    {
        perror("Cannot assign signal handler.\n");
        exit(EXIT_FAILURE);
    }

    //SIGUSR1 handler
    struct sigaction sa_usr1;
    sa_usr1.sa_handler = sigusr1_handler;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    while(((a=sigaction(SIGUSR1, &sa_usr1, NULL))==-1) && errno==EINTR);
    if (a == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    int pid = getpid();
    int status;
    int client_fd;
    struct sockaddr_in serv_addr;

    // socket create and verification
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        printf("socket creation failed...\n");
        exit(0);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);  

    if (inet_pton(AF_INET, ip_address, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
  
    if ((status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        perror("Connection Failed \n");
        return -1;
    }
    printf("PID: %d\n", getpid());
    client_information info = {pid, numberOfOrders, p, q};
    // Send PID to the server
    if (write(client_fd, &info, sizeof(client_information)) < 0) {
        perror("Failed to send PID");
        close(client_fd);
        return -1;
    }
    read(client_fd, &serverPid, sizeof(int));
    
    client_server_message msg;
    while(read(client_fd, &msg, sizeof(client_server_message)) > 0) {
        printf("%s", msg.msg);
    }
    printf("All customers served\nlog file written ..\n");
    // Close the connection
    close(client_fd);
    return 0;
}
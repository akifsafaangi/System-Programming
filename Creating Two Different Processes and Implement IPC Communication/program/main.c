#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>

#define FIFO_PERM (S_IRUSR | S_IWUSR)
#define FIFO1 "/tmp/fifo1"
#define FIFO2 "/tmp/fifo2"

// Counter to hold finished child processes
int counter = 0;
//Signal Int flag
int sigInt = 0;

/* Check if str is digit */
int checkDigit(char *str);
/* First child process */
void first_process(int numSize);
/* Second child process */
void second_process(int numSize);
/* Signal handler function for SIGCHILD */
void handler(int signal_number);
/* Signal handler function for SIGINT */
void intHandler(int signal_number);
/* Command determine function */
int commandCheck(char *command);
/* Protection for zombie process function */
void zombieProtection();

int main (int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <integer>\n", argv[0]);
        exit(0);
    }
    int argumentNum;
    if(checkDigit(argv[1]) == 0) { // Controls if argument is digit or not
        argumentNum = atoi(argv[1]);
        if(argumentNum < 1) { // If number is less than 0. No need to continue
        printf("Argument number should be greater than 0\n");
        exit(0);
        }
    } else {
        fprintf(stderr, "Invalid argument. Input is not a number\n");
        exit(0);
    }
    int fd1;
    int fd2;
    srand(time(NULL));
    
    // SIGCHLD SIGNAL
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handler;
    sa.sa_flags = 0;
    if((sigemptyset(&sa.sa_mask) == -1) || sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Failed to install SIGCHLD signal handler");
        exit(-1);
    }

    // SIGINT SIGNAL
    struct sigaction intAct = {0};
    intAct.sa_handler = &intHandler;
    if((sigemptyset(&intAct.sa_mask) == -1) || sigaction(SIGINT, &intAct, NULL) == -1) {
        perror("Failed to install SIGINT signal handler");
        exit(-1);
    }
    
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        exit(-1);
    }
    // Creating two fifos
    if(mkfifo(FIFO1, 0666) == -1) {
        perror("Failure to create fifo1");
        exit(EXIT_FAILURE);
    }
    if(mkfifo(FIFO2, 0666) == -1) {
        perror("Failure to create fifo2");
        exit(EXIT_FAILURE);
    }
    for(int i = 0;i < 2;i++) {
        if(sigInt==1) {
            printf("SIGINT caught by: %d\n", getpid());
            unlink(FIFO1);
            unlink(FIFO2);
            exit(-1);
        }
        pid_t pid = fork();
        if(pid == -1) {
            perror("Fork failed");
            exit(1);
        } else if(pid == 0) { // Child process
            switch(i) {
                case 0:
                    first_process(argumentNum);
                    break;
                case 1:
                    second_process(argumentNum);
                    break;
            }

        }
    }
    // Open fifos and send informations to fifos
    ssize_t byteswritten;
    // Opens fifos for writing
    while((((fd1 = open(FIFO1, O_WRONLY)) == -1) || ((fd2 = open(FIFO2, O_WRONLY)) == -1)) && (errno == EINTR)) ;
    if(fd1 == -1) {
        fprintf(stderr, "[%ld]: Failed to open named pipe %s for write: %s\n", (long)getpid(), FIFO1, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(fd2 == -1) {
        fprintf(stderr, "[%ld]: Failed to open named pipe %s for write: %s\n", (long)getpid(), FIFO2, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        unlink(FIFO1);
        unlink(FIFO2);
	    close(fd1);
	    close(fd2);
        exit(-1);
    }
    // Create an array with random numbers
    int randomNumbers[argumentNum];
    printf("Number of arrays:\n");
    for(int i = 0;i < argumentNum;i++) {
        randomNumbers[i] = rand()%5 + 1;
        printf("%d ", randomNumbers[i]);
    }
    printf("\n");
    // Write array to fifo2
    while(((byteswritten=write(fd2, randomNumbers, sizeof(randomNumbers)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    // If the number couldn't write to the fifo
    if(byteswritten < 0) {
        perror("Cannot write to the fifo");
        exit(-1);
    }
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        unlink(FIFO1);
        unlink(FIFO2);
        close(fd1);
        close(fd2);
        exit(-1);
    }

    char *command = "multiply";
    // Writes command to fifo2
    while(((byteswritten=write(fd2, command, sizeof(command)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    // If the number couldn't write to the fifo
    if(byteswritten < 0) {
        perror("Cannot write to the fifo");
        exit(-1);
    }
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        unlink(FIFO1);
        unlink(FIFO2);
	    close(fd1);
	    close(fd2);
        exit(-1);
    }
    // Write the random numbers to first fifo
    while(((byteswritten=write(fd1, randomNumbers, sizeof(randomNumbers)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    // If the number couldn't write to the fifo
    if(byteswritten < 0) {
        perror("Cannot write to the fifo");
        exit(-1);
    }
    close(fd1);
    close(fd2);
    while(counter < 2) {
        if(sigInt==1) {
            printf("SIGINT caught by: %d\n", getpid());
            unlink(FIFO1);
            unlink(FIFO2);
            exit(-1);
        }
        sleep(2);
        printf("proceeding\n");
        if(sigInt==1) {
            printf("SIGINT caught by: %d\n", getpid());
            unlink(FIFO1);
            unlink(FIFO2);
            exit(-1);
        }
    }
    unlink(FIFO1);
    unlink(FIFO2);
    zombieProtection();
    return 0;
}

int checkDigit(char *str) {
    while (*str) {
        if (!isdigit(*str)) {
            return -1;
        }
        str++;
    }
    return 0;
}

void first_process(int numSize) {
    int fd1;
    int fd2;
    int numArr[numSize];
    // Open first fifo for read
    while(((fd1 = open(FIFO1, O_RDONLY)) == -1) && (errno == EINTR)) ;
    if(fd1 == -1) {
        fprintf(stderr, "[%ld]: Failed to open named pipe %s for read: %s\n", (long)getpid(), FIFO1, strerror(errno));
        exit(EXIT_FAILURE);
    }
    sleep(10);
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        close(fd1);
        exit(-1);
    }
    ssize_t bytesread;
    // Read number arrays which wrote from parent process
    while(((bytesread=read(fd1, numArr, sizeof(numArr))) == -1) && (errno == EINTR)) ; // To make sure that the string is read correctly without interrupting
    // If the number couldn't read from the fifo
    if(bytesread < 0) {
        perror("Cannot read from the fifo");
        exit(EXIT_FAILURE);
    }
    int result = 0;
    for (int i = 0; i < numSize; i++) {
        result += numArr[i];
    }
    close(fd1);
    
    ssize_t byteswritten;
    // Open fifo2 for writing to write result to fifo2
    while(((fd2 = open(FIFO2, O_WRONLY)) == -1) && (errno == EINTR)) ;
    if(fd2 == -1) {
        fprintf(stderr, "[%ld]: Failed to open named pipe %s for write: %s\n", (long)getpid(), FIFO2, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sigInt==1) {
        printf("SIGINT caught by: %d\n", getpid());
        close(fd2);
        exit(-1);
    }
    // Write result to fifo2
    while(((byteswritten=write(fd2, &result, sizeof(result)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    // If the number couldn't write to the fifo
    if(byteswritten < 0) {
        perror("Cannot write to the fifo");
        exit(-1);
    }
    close(fd2);
    exit(EXIT_SUCCESS);
}
void second_process(int numSize) {
    int fd;
    char sit[20];
    int resultChild1;
    int resultChild2;
    int numArr[numSize];
    while(((fd = open(FIFO2, O_RDONLY)) == -1) && (errno == EINTR)) ;
    if(fd == -1) {
        fprintf(stderr, "[%ld]: Failed to open named pipe %s for read: %s\n", (long)getpid(), FIFO1, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            close(fd);
            exit(-1);
        }
    sleep(10);
    ssize_t bytesread;
    // Read number arrays which wrote from parent process
    while(((bytesread=read(fd, numArr, sizeof(numArr))) == -1) && (errno == EINTR)) ; // To make sure that the string is read correctly without interrupting
    // If couldn't read from the fifo
    if(bytesread < 0) {
        perror("Cannot read from the fifo");
            exit(EXIT_FAILURE);
    }
    // Read commands which sent by parent process
    while(((bytesread=read(fd, &sit, sizeof(sit))) == -1) && (errno == EINTR)) ; // To make sure that the string is read correctly without interrupting
    if(bytesread < 0) {
        perror("Cannot read from the fifo");
        exit(EXIT_FAILURE);
    }

    while((bytesread=read(fd, &resultChild1, sizeof(resultChild1))) <= 0) ; // Wait for input from child 1
    // If couldn't read from the fifo
    if(bytesread < 0) {
        perror("Cannot read from the fifo");
        exit(EXIT_FAILURE);
    }
    close(fd);
    switch(commandCheck(sit)) {
        case 0:
            resultChild2 = 1;
            for (int i = 0; i < numSize; i++) {
                resultChild2 *= numArr[i];
            }
            break;
        case 1:
            for (int i = 0; i < numSize; i++) {
                // If not 0
                resultChild2 /= numArr[i];
            }
            break;
        case 2:
            for (int i = 0; i < numSize; i++) {
                resultChild2 -= numArr[i];
            }
            break;
        case 3:
            for (int i = 0; i < numSize; i++) {
                resultChild2 += numArr[i];
            }
            break;
        default:
            exit(EXIT_FAILURE);
            break;
    }
    printf("Sum of the two results: %d\n", resultChild1+resultChild2);
    exit(EXIT_SUCCESS);
}

/* Signal handler function */
void handler(int signal_number) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            // Child exited normally
            printf("Child process with PID %d terminated with status: %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            // Child exited due to a signal
            printf("Child process with PID %d terminated due to signal: %d\n", pid, WTERMSIG(status));
        }
    }
    counter++;
}

int commandCheck(char *command) {
    // Perform multiplication if the command is "multiply"
        if (strcmp(command, "multiply") == 0) {
            return 0;
        } else if (strcmp(command, "divide") == 0) {
            return 1;
        } else if (strcmp(command, "substract") == 0) {
            return 2;
        } else if (strcmp(command, "sum") == 0) {
            return 3;
        } else {
            printf("Invalid command\n");
            return -1;
        }
}

void zombieProtection() {
    pid_t pid;
    int status;
    // Make sure all childrens killed
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Process the terminated child process, possibly by killing it
        if (kill(pid, SIGKILL) == 0) {
            printf("Zombie %d killed\n", pid);
        } else {
            fprintf(stderr, "Failed to kill zombie %d\n", pid);
        }
    }
}

/* Signal handler function */
void intHandler(int signal_number) {
    sigInt = 1;
}

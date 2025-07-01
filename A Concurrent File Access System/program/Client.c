#include "FifoHeader.h"
#include <sys/wait.h>
#include <dirent.h>

/* Signal interrupt int value */
int sigInt = 0;

/* Pid of the server child process */
int pidChild;

/* Function handles SIGINT */
void signal_handler(int signum);

/* Controls argument to determine connect mode */
int checkConnectMode(char* mode);

/* Controls server situation to connect */
int getServerSituation();

/* Get user inputs and handles commands */
void handleClient();

/* Function to close client fifos */
void closeFiles(int client_fd1, int client_fd2);

/* Client fifo path */
static char clientFifo[CLIENT_FIFO_NAME_LEN];

/* Copy the arch file to current directory */
int copy_arch_file(const char *directory, const char *filename);

/* Copy the arch file to current directory */
int remove_directory(const char *path);

/* Check if the str is digit */
int checkDigit(char *str);

/* Function to remove current fifo(s) */
static void removeFifo() {
    unlink(clientFifo);
}

int main(int argc, char* argv[]) {
    // Check if command-line argument entered correctly
    if(argc < 3) {
        printf("Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        return 1;
    }
    if(checkDigit(argv[2]) == -1) {
        return 1;
    }

    // Server file descriptor
    int serverFd;
    struct request req;
    
    //SIGINT and SIGTERM handler
    int returnVal;
    struct sigaction sigintAct={0};
    sigemptyset(&sigintAct.sa_mask);
    sigintAct.sa_handler=signal_handler;
    while(((returnVal=sigaction(SIGINT, &sigintAct, NULL))==-1) && errno==EINTR);
    if(returnVal==-1)
    {
        exit(EXIT_FAILURE);
    }
    while(((returnVal=sigaction(SIGTERM, &sigintAct, NULL))==-1) && errno==EINTR);
    if(returnVal==-1)
    {
        exit(EXIT_FAILURE);
    }
    
    // Gets connect mode
    int connectMode = checkConnectMode(argv[1]);
    
    snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE, (long)getpid());
    // To avoid any trouble, delete the previous fifo if there is one with this name
    removeFifo();
    // Create client fifo
    while((returnVal = mkfifo(clientFifo, S_IWUSR | S_IRUSR)) == -1 && errno==EINTR);
    if(returnVal == -1 && errno != EEXIST) {
        perror("FIFO can not be created.\n");
        exit(EXIT_FAILURE);
    }

    // Get server fifo path
    char serverFifo[SERVER_FIFO_NAME_LEN];
    snprintf(serverFifo, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, (long) atoi(argv[2]));
    // Opens server fifo to connect
    while((serverFd=open(serverFifo, O_WRONLY)) == -1 && errno==EINTR);
    // If server fifo couldn't open correctly
    if (serverFd == -1) {
        perror("Failed to open server FIFO for writing");
        removeFifo();
        exit(EXIT_FAILURE);
    }

    // If interrupt happens
    if(sigInt == 1) {
        // Make clean ups and exit
        removeFifo();
        close(serverFd);
        exit(EXIT_SUCCESS);
    }

    // Set struct request which will be sent to server
    int byteswritten;
    req.pid = getpid();
    req.connectMode = connectMode;
    // Get current working directory
    if (getcwd(req.clientPath, PATH_LEN) == NULL) {
        // Make clean ups and exit
        perror("Getting current working directory failed.");
        close(serverFd);
        removeFifo();
        exit(EXIT_FAILURE);
    }

    // Send request to server fifo
    while((byteswritten=write(serverFd, &req, sizeof(struct request))) == -1 && errno==EINTR);
    if (byteswritten == -1) {
        // Make clean ups and exit
        perror("Failed to write to server FIFO");
        close(serverFd);
        removeFifo();
        exit(EXIT_FAILURE);
    }
    close(serverFd);
    // Open client fifo to get server response
    int client_fd;
    while((client_fd=open(clientFifo, O_RDONLY)) == -1);
    if (client_fd < 0) {
        // Make clean ups and exit
        perror("Failed to open server FIFO for reading");
        removeFifo();
        exit(EXIT_FAILURE);
    }
    printf("Waiting for Que.. \n");

    // Get current server situation to control is server available to connect
    int situation = getServerSituation(client_fd);
    // If situation -1, can't connect to server
    if(connectMode == 1 && situation == -1) {
        // Make clean ups and exit
        printf("Server is full...\nExiting program!\n");
        removeFifo();
        close(client_fd);
        exit(EXIT_SUCCESS);
    }

    // If interrupt happens
    if(sigInt == 1) {
        // Make clean ups and exit
        removeFifo();
        close(client_fd);
        exit(EXIT_SUCCESS);
    }

    // Get situation until server is available to connect
    while(situation == -1) {
        situation = getServerSituation(client_fd);
    }
    pidChild = situation;
    printf("Connection established:\n");
    handleClient(client_fd);
}

/* Function handles SIGINT */
void signal_handler(int signum) {
    sigInt = 1;
}

/* Controls argument to determine connect mode */
int checkConnectMode(char* mode) {
    // If argument is connect
    if(strcmp(mode, "Connect") == 0) {
        return 0;
    } else if (strcmp(mode, "tryConnect") == 0) {
        return 1;
    } 
    // If argument is invalid
    else {
        printf("Invalid mode to connect to server\n");
        exit(EXIT_SUCCESS);
    }
}

/* Controls server situation to connect */
int getServerSituation(int client_fd) {
    int situation;
    int bytesread;
    // Loop until interrupt happens
    while(sigInt == 0) {
        // Get server situation
        bytesread=read(client_fd, &situation, sizeof(situation));
        // If error occured while reading response
        if(bytesread < 0) {
            if(errno == EINTR) 
                continue;
            removeFifo();
            close(client_fd);
            perror("Error while reading from fifo");
            exit(EXIT_FAILURE);
        } 
        // If client gets situation, return the situation
        else if(bytesread > 0) {
            return situation;
        }
    }
    // If interrupt happens
    if(sigInt == 1) {
        // Make clean ups and exit
        removeFifo();
        close(client_fd);
        exit(EXIT_SUCCESS);
    }
    return -1;
}

/* Get user inputs and handles commands */
void handleClient(int client_fd2) {
    ssize_t bytesread;
    char clientASemName[CLIENT_SEM_NAME_LEN]; // Semaphore A path
    char clientBSemName[CLIENT_SEM_NAME_LEN]; // Semaphore B path
    // Semaphores to read and write synchronize with server child process
    sem_t *clientA_Sem; // Semaphore A
    sem_t *clientB_Sem; // Semaphore B

    // If interrupt happens
    if(sigInt == 1) {
        // Make clean ups and exit
        printf("Terminating client\n");
        close(client_fd2);
        removeFifo();
        kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
        exit(EXIT_SUCCESS);
    }
    snprintf(clientASemName, CLIENT_SEM_NAME_LEN, CLIENT_SEMA_TEMPLATE, (long)getpid());
    //Checking if there is any other instance of this already
    while(((clientA_Sem=sem_open(clientASemName, O_CREAT, 0644, 0))==NULL) && errno==EINTR);
    if(clientA_Sem==SEM_FAILED)
    {
        // Make clean ups and exit
        perror("Cannot open semaphore");
        close(client_fd2);
        removeFifo();
        exit(EXIT_FAILURE);
    }
    snprintf(clientBSemName, CLIENT_SEM_NAME_LEN, CLIENT_SEMB_TEMPLATE, (long)getpid());
    //Checking if there is any other instance of this already
    while(((clientB_Sem=sem_open(clientBSemName, O_CREAT, 0644, 0))==NULL) && errno==EINTR);
    if(clientB_Sem==SEM_FAILED)
    {
        // Make clean ups and exit
        perror("Cannot open semaphore");
        close(client_fd2);
        removeFifo();
        sem_close(clientA_Sem);
        sem_unlink(clientASemName);
        exit(EXIT_FAILURE);
    }

    // Open FIFO for writing
    int client_fd1;
    client_fd1 = open(clientFifo, O_WRONLY);
    if (client_fd1 == -1) {
        // Make clean ups and exit
        perror("Failed to open client fifo for writing");
        close(client_fd2);
        removeFifo();
        sem_close(clientA_Sem);
        sem_unlink(clientASemName);
        sem_close(clientB_Sem);
        sem_unlink(clientBSemName);
        exit(EXIT_FAILURE);
    }
    while(sigInt == 0) {
        struct response resp;
        memset(resp.arr, 0, sizeof(resp.arr));
        int cont = 1;
        printf("Enter comment: ");
        fflush(stdout); // Make sure "Enter comment" is printed before reading input

        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            printf("Terminating client\n");
            closeFiles(client_fd1, client_fd2);
            removeFifo();
            sem_close(clientA_Sem);
            sem_unlink(clientASemName);
            sem_close(clientB_Sem);
            sem_unlink(clientBSemName);
            kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
            exit(EXIT_SUCCESS);
        }
        // Use read system call to get input from stdin
        bytesread = read(STDIN_FILENO, resp.arr, sizeof(struct response) - 1);
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            printf("Terminating client\n");
            closeFiles(client_fd1, client_fd2);
            removeFifo();
            sem_close(clientA_Sem);
            sem_unlink(clientASemName);
            sem_close(clientB_Sem);
            sem_unlink(clientBSemName);
            kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
            exit(EXIT_SUCCESS);
        }
        if(strcmp(resp.arr, "\n") == 0) {
            printf("Please enter a command\n");
            continue;
        }
        if (bytesread == -1) {
            // Make clean ups and exit
            perror("Couldn't get input from user");
            closeFiles(client_fd1,client_fd2);
            removeFifo();
            sem_close(clientA_Sem);
            sem_unlink(clientASemName);
            sem_close(clientB_Sem);
            sem_unlink(clientBSemName);
            kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
            exit(EXIT_FAILURE);
        }
        // Null-terminate the string
        if (bytesread > 0 && resp.arr[bytesread - 1] == '\n') {
            resp.arr[bytesread - 1] = '\0'; // Replace newline if present
        } else {
            resp.arr[bytesread] = '\0';
        }
        // Assuming resp.arr is a null-terminated string
        char *original = strdup(resp.arr);  // Make a copy of resp.arr
        if (original == NULL) {
            fprintf(stderr, "Failed to allocate memory.\n");
            exit(1);
        }
        // Get command for checking command
        char *command = strtok(original, " ");
        // Get argument for archServer
        char *argument = strtok(NULL, " \n\0");
        char extension[5] = "";
        char filename[256] = "";
        int fullLength = 0;
        if(argument != NULL) {
            fullLength = strlen(argument);
        }
        if(fullLength > 3 && (strcmp(command, "archServer") == 0)) {
            // Copy the filename part
            strncpy(filename, argument, (fullLength-4));
            filename[fullLength-4] = '\0';  // Null-terminate the filename array
            // Copy the extension part
            strncpy(extension, argument + (fullLength-4), 4);
            extension[4] = '\0'; // Null-terminate the extension array
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            printf("Terminating client\n");
            closeFiles(client_fd1, client_fd2);
            removeFifo();
            sem_close(clientA_Sem);
            sem_unlink(clientASemName);
            sem_close(clientB_Sem);
            sem_unlink(clientBSemName);
            free(original);
            kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
            exit(EXIT_SUCCESS);
        }
        if(strcmp(command,"archServer") == 0) {
            if(strcmp(extension, ".tar") == 0) { // Create archive directory
                /* Create the server folder if it doesn't exist */
                if (mkdir(filename, 0766) == -1 && errno != EEXIST) { // Create archive directory
                    perror("Failed to create directory");
                    cont = 0;
                }
            } else {
                printf("File extension entered wrong\n");
                cont = 0;
            }
        }
        if(strcmp(command, "quit") == 0) {
            printf("Sending write request to server log file\n");
        }
        if(cont != 0) {
            // Write user request to server
            if(write(client_fd1, &resp, sizeof(struct response)) != sizeof(struct response)) {
                // Make clean ups and exit
                perror("Couldnt write to server using client fifo");
                closeFiles(client_fd1, client_fd2);
                removeFifo();
                sem_close(clientA_Sem);
                sem_unlink(clientASemName);
                sem_close(clientB_Sem);
                sem_unlink(clientBSemName);
                free(original);
                kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
                exit(EXIT_FAILURE);
            }
            if(strcmp(command, "quit") == 0) {
                printf("waiting for logfile ...\n");
            } else if(strcmp(command, "upload") == 0) {
                printf("File transfer request received. Beginning file transfer:\n");
            }
            // If interrupt happens
            if(sigInt == 1) {
                // Make clean ups and exit
                printf("Terminating client\n");
                closeFiles(client_fd1, client_fd2);
                removeFifo();
                sem_close(clientA_Sem);
                sem_unlink(clientASemName);
                sem_close(clientB_Sem);
                sem_unlink(clientBSemName);
                free(original);
                kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
                exit(EXIT_SUCCESS);
            }
            // Increase semaphore B, so server can read client request
            sem_post(clientB_Sem);
            // Control semaphore A before reading 
            sem_wait(clientA_Sem);
            int first = 1;
            if(strcmp(resp.arr, "killServer") == 0) {
                // Make clean ups and exit
                printf("Terminating client\n");
                closeFiles(client_fd1, client_fd2);
                removeFifo();
                sem_close(clientA_Sem);
                sem_unlink(clientASemName);
                sem_close(clientB_Sem);
                sem_unlink(clientBSemName);
                free(original);
                kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
                exit(EXIT_SUCCESS);
            }
            // Read server response until resp.status is RESPONSE_OK
            while(first == 1 || resp.status == RESPONSE_CON) {
                first = 0;
                // Read server response
                if(read(client_fd2, &resp, sizeof(struct response)) != sizeof(struct response)) {
                    // Make clean ups and exit
                    perror("Couldnt get answer from server");
                    closeFiles(client_fd1, client_fd2);
                    removeFifo();
                    sem_close(clientA_Sem);
                    sem_unlink(clientASemName);
                    sem_close(clientB_Sem);
                    sem_unlink(clientBSemName);
                    free(original);
                    kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
                    exit(EXIT_FAILURE);
                }
                // If interrupt happens
                if(sigInt == 1) {
                    // Make clean ups and exit
                    printf("Terminating client\n");
                    closeFiles(client_fd1, client_fd2);
                    removeFifo();
                    sem_close(clientA_Sem);
                    sem_unlink(clientASemName);
                    sem_close(clientB_Sem);
                    sem_unlink(clientBSemName);
                    free(original);
                    kill(pidChild, SIGINT); // Send kill signal to server child process to terminate child process
                    exit(EXIT_SUCCESS);
                }
                
                // If quit is handled
                if(strcmp(resp.arr, "quit") == 0) {
                    // Make clean ups and exit
                    printf("logfile write request granted\nbye..\n");
                    closeFiles(client_fd1, client_fd2);
                    removeFifo();
                    sem_close(clientA_Sem);
                    sem_unlink(clientASemName);
                    sem_close(clientB_Sem);
                    sem_unlink(clientBSemName);
                    free(original);
                    exit(EXIT_SUCCESS);
                } else if(strcmp(command, "archServer") == 0 && strcmp(resp.arr, "Error") != 0) {
                    // Print server response
                    printf("Calling tar utility .. ");
                    pid_t pid = fork();
                    if (pid == 0) {  // Child process
                        if (chdir(filename) != 0) {
                            perror("chdir");
                            free(original);
                            exit(EXIT_FAILURE);
                        }
                        int len = strlen(argument);
                        char argmnt[len];
                        for(int i = 0;i < len;++i) {
                            argmnt[i] = argument[i];
                        }
                        argmnt[len] = '\0';
                        free(original);
                        char *cmd = "/bin/sh";
                        char arg1[] = "sh";
                        char arg2[] = "-c";
                        char cmmnd[256];
                        sprintf(cmmnd, "tar -cf %s *", argmnt);
                        char *args[] = {arg1, arg2, cmmnd, NULL};
                        execvp(cmd, args);
                        // If execlp returns, there was an error
                        perror("execlp");
                        exit(EXIT_FAILURE);
                    } else if (pid > 0) {  // Parent process
                        printf("child PID %d\n", pid);
                        waitpid(-1, NULL, 0);
                        printf("child returned with SUCCESS..\n");
                        printf("copying the archive file..\n");
                        if(copy_arch_file(filename, argument) == -1) {
                            printf("Error while copying archive directory\n");
                            continue;
                        }
                        printf("removing archive directory... \n");
                        if(remove_directory(filename) == -1) {
                            printf("Error while removing archive directory\n");
                            continue;
                        }
                        printf("SUCCESS Server side files are achived in \"%s\"\n", filename);
                    } else {
                        perror("Failed to fork");
                    }
                } else {
                    // Print server response
                    printf("%s", resp.arr);
                }
            }
        }
        free(original);
    }
    // Make clean ups and exit
    printf("Terminating client\n");
    closeFiles(client_fd1, client_fd2);
    removeFifo();
    sem_close(clientA_Sem);
    sem_unlink(clientASemName);
    sem_close(clientB_Sem);
    sem_unlink(clientBSemName);
}

/* Function to close client fifos */
void closeFiles(int client_fd1, int client_fd2) {
    close(client_fd1);
    close(client_fd2);
}

int copy_arch_file(const char *directoryPath, const char *fileName) {
    ssize_t bytesread;
    char buffer[BUFF_SIZE];
    char dirPath[PATH_LEN];
    int srcFd;
    int dstFd;
    // Set directory path
    snprintf(dirPath, sizeof(dirPath), "%s/%s", directoryPath, fileName);
    // Open file for reading
    srcFd = open(dirPath, O_RDONLY, 0333);
    if (srcFd == -1) {
        perror("Failed to open file for reading to download");
        return -1;
    }

    // Open file for writing
    dstFd = open(fileName, O_WRONLY | O_CREAT, 0644);
    if(dstFd == -1) {
        perror("Failed to open file for writing to download");
        close(srcFd);
        return -1;
    }
    // If interrupt happens
    if(sigInt == 1) {
        // Make clean ups and exit
        close(srcFd);
        close(dstFd);
        return -1;
    }
    // Read from source file
    while(((bytesread=read(srcFd, buffer, BUFF_SIZE)) > 0) && (errno != EINTR)) {
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            return -1;
        }
        if(bytesread == -1) {
            perror("File couldn't read correctly");
            close(srcFd);
            close(dstFd);
            return -1;
        }
        // Write to destination file
        if (write(dstFd, buffer, bytesread) != bytesread) {
            perror("File couldn't write correctly");
            close(srcFd);
            close(dstFd);
            return -1;
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            return -1;
        }
    }
    return 0;
}

int remove_directory(const char *path) {
    DIR *d = opendir(path);
    struct dirent *dir;
    char filePath[1024];

    if (d) {
        // Read each entry in the directory
        while ((dir = readdir(d)) != NULL) {
            // Skip the '.' and '..' entries
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
                continue;

            // Construct full path for each file
            snprintf(filePath, sizeof(filePath), "%s/%s", path, dir->d_name);

            // Remove the file
            if (remove(filePath) != 0) {
                perror("Failed to remove a file");
                closedir(d);
                return -1;
            }
        }
        closedir(d);

        // Remove the directory itself
        if (rmdir(path) != 0) {
            perror("Failed to remove the directory");
            return -1;
        }
    } else {
        // opendir failed
        perror("Failed to open the directory");
        return -1;
    }

    return 0;
}

int checkDigit(char *str) {
    // Pointer to navigate through str
    char *p = str;

    // Check each character
    while (*p) {
        if (!isdigit((unsigned char)*p)) { // Check if the character is not a digit
            printf("Error: Non-numeric input detected\n");
            return -1;
        }
        p++; // Move to the next character
    }
    return 0;
}

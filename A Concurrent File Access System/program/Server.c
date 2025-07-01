#include <time.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/wait.h>
#include "FifoHeader.h"

#define LOG_FILE "server_log.txt"
#define WAIT_QUEUE_LONG 100
#define MAX_FILES 100
#define FILENAME_LEN 256

/* Server Fifo path */
static char serverFifo[SERVER_FIFO_NAME_LEN];

/* Struct for each file which has write and read semaphores, reader count and file name */
typedef struct {
    char filename[256];        // Name of the file
    int reader_count;          // Number of readers currently reading the file
    sem_t mutex;               // Semaphore to protect access to reader_count
    sem_t write;               // Semaphore to ensure exclusive write access
} file_access_control;

/* Array of file structs */
file_access_control *fac_table[MAX_FILES];

/* Max client which server can handle */
int maxClients;

/* Log file file descriptor */
int logFile;

/* Signal interrupt int value */
int sigInt = 0;

/* Signal child int value */
int sigChld = 0;

/* Count of client which connected to server */
int clientCount = 0;

/* Mutex for log file */
sem_t logMutex;

/* Function handles SIGINT */
void sigint_handler(int signum);

/* Function handles SIGCHLD */
void sigchld_handler(int signum);

/* Function to write message to log file */
void writeLogMessage(char* startMessage);

/* Add time and date to log message and return it */
void createMessageWithTime(char* startMessage, char* message);

/* Send server situation to client */
void sendInfoToClient(int pidOfClient, int situation);

/* Handles client commands */
void handle_client(int client_fd1, int client_fd2, int pidOfClient, DIR *serverDir, char *clientPath, char *clientName);

/* Open specific fifo client for writing */
int openClientFileWrite(int pidOfClient);
/* Open specific fifo client for reading */
int openClientFileRead(int pidOfClient);

/* Close server fifos */
void closeServerFiles(int main_fd, int dummyFd);
/* Close client fifos */
void closeClientFiles(int client_fd1, int client_fd2);

/* Initialize file struct */
int init_fac(const char *filename);

/* Find the file struct which is given filename */
file_access_control *find_fac(const char *filename);

/* Set file semaphores for reading */
void start_reading(file_access_control *fac);

/* Set file semaphores after reading */
void stop_reading(file_access_control *fac);

/* Set file write semaphore for writing */
void start_writing(file_access_control *fac);

/* Set file write semaphore after writing */
void stop_writing(file_access_control *fac);

/* Initialize each file's struct in the server directory */
int init_fac_table(DIR *serverDir);

/* Add new file to fac table */
int add_new_file(const char *filename);

/* Read current line from the file */
char *read_line(int fd);

/* Find specific line number in the file */
int find_line(int fd, int lineNum);

/* Write resp struct to client fifo */
int write_resp_to_client(int clientFd, struct response *resp);

/* Write str to current line in the file */
int write_line(int fd, char *str);

/* Upload the file from client to server directory */
int upload_file(long *totalBytesTransferred, char *fileName, char *clientPath);

/* Download the file from server to client directory */
int download_file(long *totalBytesTransferred, char *fileName, char *clientPath);

/* Get help commands message */
char *getHelpCommands(char *arr);

/* Kill all child and client processes */
void kill_childs(int childSize, int *clients);

/* Check if the str is digit */
int checkDigit(char *str);

/* Default format for time, date, message */
static const char default_format[] = "%b %d %Y %Z %H %M %S";

/* Array of client pids */
int *clients;

/* Array of child pids */
int *childPids;

int main(int argc, char* argv[]) {
    // Check if command-line argument entered correctly
    if(argc != 3) {
        printf("Usage: %s <dirName> <max.#ofClients>\n", argv[0]);
        return 1;
    }
    if (getcwd(serverPath, 100) == NULL) {
        // Make clean ups and exit
        perror("Getting current working directory failed.");
        exit(EXIT_FAILURE);
    }
    if(checkDigit(argv[2]) == -1) {
        return 1;
    }
    maxClients = atoi(argv[2]);

    struct request req; // Request struct
    int returnVal;
    DIR *serverDir; // Server directory

    //SIGINT handler
    struct sigaction sa_action={0};
    sigemptyset(&sa_action.sa_mask);
    sigaddset(&sa_action.sa_mask, SIGRTMIN);
    sa_action.sa_handler=sigint_handler;
    while(((returnVal=sigaction(SIGINT, &sa_action, NULL))==-1) && errno==EINTR);
    if(returnVal==-1)
    {
        perror("Cannot assign signal handler.\n");
        exit(EXIT_FAILURE);
    }

    //SIGCHLD HANDLER
    sa_action.sa_handler=sigchld_handler;
    while(((returnVal=sigaction(SIGCHLD, &sa_action, NULL))==-1) && errno==EINTR);
    if(returnVal==-1)
    {
        perror("Cannot assign signal handler.\n");
        exit(EXIT_FAILURE);
    }

    /* To prevent race condition while accessing log file*/
    if (sem_init(&logMutex, 1, 1) == -1) {
        perror("Couldn't init log mutex.\n");
        exit(EXIT_FAILURE);
    }

    /* Create log file */
    logFile = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(logFile == -1) {
        perror("The file cannot be opened\n");
        exit(-1);
    }

    /* Create the server folder if it doesn't exist */
    if (mkdir(argv[1], 0766) == -1 && errno != EEXIST) {
        perror("Failed to create directory");
        close(logFile);
        exit(EXIT_FAILURE);
    }
    if(chdir(argv[1]) == -1) {
        perror("Failed to change current working directory");
        close(logFile);
        exit(EXIT_FAILURE);
    }
    /* Open server folder */
    if ((serverDir = opendir(".")) == NULL) {
        perror("Failed to open server directory");
        close(logFile);
        exit(EXIT_FAILURE);
    }

    /* Initialize fac table */
    if(init_fac_table(serverDir) == -1) {
        closedir(serverDir);
        close(logFile);
        exit(-1);
    }

    // Allocate memory for each array
    clients = (int *)malloc(maxClients * sizeof(int));
    childPids = (int *)malloc(maxClients * sizeof(int));
    // Check if memory allocation failed
    if (clients == NULL || childPids == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        // Properly handle the case where one may have been allocated
        free(clients);
        free(childPids);
        close(logFile);
        closedir(serverDir);
        exit(EXIT_FAILURE);
    }

    // Initialize the arrays with -1
    for (int i = 0; i < maxClients; i++) {
        clients[i] = -1;
        childPids[i] = -1;
    }

    snprintf(serverFifo, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, (long)getpid()); // Set server fifo path
    // To avoid any trouble, delete the previous fifo if there is one with this name
    unlink(serverFifo);
    // Create server fifo
    while((returnVal = mkfifo(serverFifo, S_IWUSR | S_IRUSR)) == -1 && errno==EINTR);
    if(returnVal == -1 && errno != EEXIST) {
        writeLogMessage("FIFO can not be created.\n");
        close(logFile);
        free(clients);
        free(childPids);
        exit(EXIT_FAILURE);
    }
    
    printf("Server Started PID %d…\n", getpid());
    printf("Waiting for clients...\n");

    int main_fifo_fd;
    // Open server fifo for reading
    while((main_fifo_fd=open(serverFifo, O_RDONLY | O_NONBLOCK)) == -1 && errno==EINTR);
    if (main_fifo_fd == -1) {
        perror("Failed to open server fifo for reading");
        close(logFile);
        unlink(serverFifo);
        free(clients);
        free(childPids);
        exit(EXIT_FAILURE);
    }
    // Dummy
    int dummyFd = open(serverFifo, O_WRONLY);
    if(dummyFd == -1) {
        perror("Failed to open server fifo for dummy writing");
        close(logFile);
        close(main_fifo_fd);
        unlink(serverFifo);
        free(clients);
        free(childPids);
        exit(EXIT_FAILURE);
    }

    /* Array to hold client which will be waiting to connect */
    int waitQueue[WAIT_QUEUE_LONG];
    int waitQueueHead = 0;
    int waitQueueLast = 0;
    int waitQueueCount = 0;

    int totalClientNumber = 0; // Total number of clients has entered to server
    // Server's main loop
    while (sigInt == 0) {
        int pidOfClient;
        int canConnect = 0;
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeServerFiles(main_fifo_fd, dummyFd);
            closedir(serverDir);
            close(logFile);
            sem_destroy(&logMutex);
            unlink(serverFifo);
            kill_childs(clientCount, clients);
            free(clients);
            free(childPids);
            exit(EXIT_FAILURE);
        }
        if(clientCount < maxClients && waitQueueCount != 0) { // If server is not full and there are client(s) waitin to connect, connect them before reading new request
            pidOfClient = waitQueue[waitQueueHead]; // Get pid of the waiting client
            waitQueueHead = (waitQueueHead + 1) % WAIT_QUEUE_LONG; // Increase queue head index
            waitQueueCount--; // Decrease queue count
            canConnect = 1;
        } else {
            // If interrupt happens
            if(sigInt == 1) {
                // Make clean ups and exit
                closeServerFiles(main_fifo_fd, dummyFd);
                closedir(serverDir);
                close(logFile);
                sem_destroy(&logMutex);
                unlink(serverFifo);
                kill_childs(clientCount, clients);
                free(clients);
                free(childPids);
                exit(EXIT_FAILURE);
            }
            ssize_t bytesread;
            bytesread = read(main_fifo_fd, &req, sizeof(struct request)); // Read request from client
            if(bytesread != sizeof(struct request)) {
                // An error occurred
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Error while reading from fifo");
                    closeServerFiles(main_fifo_fd, dummyFd);
                    unlink(serverFifo);
                    close(logFile);
                    free(clients);
                    free(childPids);
                    exit(EXIT_FAILURE);
                }
                // If interrupt happens
                if(sigInt == 1) {
                    // Make clean ups and exit
                    closeServerFiles(main_fifo_fd, dummyFd);
                    closedir(serverDir);
                    close(logFile);
                    sem_destroy(&logMutex);
                    unlink(serverFifo);
                    kill_childs(clientCount, clients);
                    free(clients);
                    free(childPids);
                    exit(EXIT_FAILURE);
                }
                continue;
            }
            pidOfClient = req.pid; // Set pidOfClient
            canConnect = 1;
        }
        if (canConnect == 1) {
            // If interrupt happens
            if(sigInt == 1) {
                // Make clean ups and exit
                closeServerFiles(main_fifo_fd, dummyFd);
                closedir(serverDir);
                close(logFile);
                sem_destroy(&logMutex);
                unlink(serverFifo);
                kill_childs(clientCount, clients);
                free(clients);
                free(childPids);
                exit(EXIT_FAILURE);
            }
            if (clientCount >= maxClients) { // Server is full
                printf("Connection request PID %d... Que FULL\n", pidOfClient);
                char msg[40];
                snprintf(msg, sizeof(msg), "Connection request PID %d... Que FULL\n", pidOfClient);
                writeLogMessage(msg);
                if(req.connectMode == 0) {
                    waitQueue[waitQueueLast] = pidOfClient; // Get pid of the client for connecting with it later
                    waitQueueCount++;
                    if((waitQueueLast + 1) % WAIT_QUEUE_LONG != waitQueueHead) { // Set waitQueue
                        waitQueueLast = (waitQueueLast + 1) % WAIT_QUEUE_LONG;
                    } else {
                        printf("Wait Queue is full. Can't take more client for waiting\n");
                    }
                }
                // Send server status as -1 to indicate that the server is full
                sendInfoToClient(pidOfClient, -1);
                continue;
            }
            // If interrupt happens
            if(sigInt == 1) {
                // Make clean ups and exit
                closeServerFiles(main_fifo_fd, dummyFd);
                closedir(serverDir);
                close(logFile);
                sem_destroy(&logMutex);
                unlink(serverFifo);
                kill_childs(clientCount, clients);
                free(clients);
                free(childPids);
                exit(EXIT_FAILURE);
            }
            totalClientNumber++;
            char clientName[16];
            snprintf(clientName, 16, "client%02d", totalClientNumber); // Set client name
            char *clientPath = req.clientPath;
            pid_t pid = fork();
            if (pid == 0) {  // Child process
                // Close the main FIFO in child
                close(main_fifo_fd);
                free(clients);
                free(childPids);
                sendInfoToClient(pidOfClient, getpid()); // Send server status as -1 to indicate that connection is established
                // Open the client-specific FIFO for read
                int client_fd1 = openClientFileWrite(pidOfClient);
                // Open the client-specific FIFO for read
                int client_fd2 = openClientFileRead(pidOfClient);
                // Handle client-specific requests
                handle_client(client_fd1, client_fd2, pidOfClient, serverDir, clientPath, clientName);
                closeClientFiles(client_fd1,client_fd2);
                exit(0);
            } else if (pid > 0) {  // Parent process
                for(int i = 0; i < maxClients;++i) {
                    if(clients[i] == -1) {
                        clients[i] = pidOfClient;
                        childPids[i] = pid;
                        break;
                    }
                }
                char msg[40];
                snprintf(msg, sizeof(msg), "Client PID %d connected\n", pidOfClient);
                writeLogMessage(msg);
                clientCount++;
                printf("Client PID %d connected as \"%s\"\n", pidOfClient, clientName);
            } else {
                perror("Failed to fork");
                // Make clean ups and exit
                closeServerFiles(main_fifo_fd, dummyFd);
                closedir(serverDir);
                close(logFile);
                sem_destroy(&logMutex);
                unlink(serverFifo);
                kill_childs(clientCount, clients);
                free(clients);
                free(childPids);
                exit(EXIT_SUCCESS);
            }
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeServerFiles(main_fifo_fd, dummyFd);
            closedir(serverDir);
            close(logFile);
            sem_destroy(&logMutex);
            unlink(serverFifo);
            kill_childs(clientCount, clients);
            free(clients);
            free(childPids);
            exit(EXIT_SUCCESS);
        }
    }
    // Make clean ups and exit
    closeServerFiles(main_fifo_fd, dummyFd);
    closedir(serverDir);
    close(logFile);
    sem_destroy(&logMutex);
    unlink(serverFifo);
    kill_childs(clientCount, clients);
    free(clients);
    free(childPids);
}

/* Function handles SIGINT */
void sigint_handler(int signum) {
    sigInt = 1;
}

/* Function handles SIGCHLD */
void sigchld_handler(int signum) {
    int status;
    pid_t pid;
    // For all childs which terminated
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        char msg[20];
        for(int i = 0;i < maxClients; ++i) {
            if(childPids[i] == pid) {
                snprintf(msg, sizeof(msg), "Client %ld quit\n", (long)clients[i]);
                childPids[i] = -1;
                clients[i] = -1;
            }
        }
        writeLogMessage(msg);
        // Decrease total client count in the server
        clientCount--;
    }
}

/* Function to write message to log file */
void writeLogMessage(char* startMessage)
{
    char errorMessage[400];
    // Edit log message with adding time
    createMessageWithTime(startMessage, errorMessage);
    // Call sem wait before writing to log file
    sem_wait(&logMutex);
    // Write to log file
    while((write(logFile, errorMessage, strlen(errorMessage))==-1) && (errno==EINTR));
    // Release logMutex after wrote
    sem_post(&logMutex);
}

/* Add time and date to log message and return it */
void createMessageWithTime(char* startMessage, char* message)
{
    time_t currentTime;
    char res[32];
    struct tm *lt;
    // Get time
    time(&currentTime);
    lt=localtime(&currentTime);
    strftime(res, 32, default_format, lt);
    // Add time and startMessage to a single string 
    strcpy(message, res);
    strcat(message, ": ");
    strcat(message, startMessage);
}

/* Send server situation to client */
void sendInfoToClient(int pidOfClient, int situation) {
    // Open client fifo for writing
    int clientFd = openClientFileWrite(pidOfClient);
    int byteswritten;
    // Write situation to client fifo
    while(((byteswritten=write(clientFd, &situation, sizeof(situation)))==-1) && errno==EINTR);
    // If didn't write correctly
    if(byteswritten < 0) {
        close(clientFd);
        perror("Error while writing to fifo");
        exit(EXIT_FAILURE);
    }
    close(clientFd);
}

/* Open specific fifo client for writing */
int openClientFileWrite(int pidOfClient) {
    char clientFifo[CLIENT_FIFO_NAME_LEN];
    // Set client fifo path
    snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE, (long)pidOfClient);
    int clientFd;
    // Open client fifo for writing
    while((clientFd=open(clientFifo, O_WRONLY)) == -1 && errno==EINTR);
    // If didn't open correctly
    if (clientFd == -1) {
        perror("Failed to open client FIFO for writing");
        exit(EXIT_FAILURE);
    }
    return clientFd;
}

/* Open specific fifo client for reading */
int openClientFileRead(int pidOfClient) {
    char clientFifo[CLIENT_FIFO_NAME_LEN];
    // Set client fifo path
    snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE, (long)pidOfClient);
    int clientFd;
    // Open client fifo for reading
    while((clientFd=open(clientFifo, O_RDONLY)) == -1 && errno==EINTR);
    // If didn't open correctly
    if (clientFd == -1) {
        perror("Failed to open client FIFO for reading");
        exit(EXIT_FAILURE);
    }
    return clientFd;
}

/* Close server fifos */
void closeServerFiles(int main_fd, int dummyFd) {
    close(main_fd);
    close(dummyFd);
}

/* Close client fifos */
void closeClientFiles(int client_fd1, int client_fd2) {
    close(client_fd1);
    close(client_fd2);
}

/* Handle client commands */
void handle_client(int client_fd1, int client_fd2, int pidOfClient, DIR *serverDir, char *clientPath, char *clientName) {
    struct response resp; // Response which will be sent to client
    char clientSemName[CLIENT_SEM_NAME_LEN]; // Client Semaphore Path
    // Semaphores for synch between client and server reading, writing
    sem_t *clientA_Sem; // Semaphore A
    sem_t *clientB_Sem; // Semaphore B
    // Set Client Semaphore A Path
    snprintf(clientSemName, CLIENT_SEM_NAME_LEN, CLIENT_SEMA_TEMPLATE, (long)pidOfClient);
    //Checking if there is any other instance of this already
    while(((clientA_Sem=sem_open(clientSemName, O_CREAT, 0644, 0))==NULL) && errno==EINTR);
    if(clientA_Sem==SEM_FAILED)
    {
        perror("Cannot open semaphore");
        exit(EXIT_FAILURE);
    }
    // Set Client Semaphore B Path
    snprintf(clientSemName, CLIENT_SEM_NAME_LEN, CLIENT_SEMB_TEMPLATE, (long)pidOfClient);
    //Checking if there is any other instance of this already
    while(((clientB_Sem=sem_open(clientSemName, O_CREAT, 0644, 0))==NULL) && errno==EINTR);
    if(clientB_Sem==SEM_FAILED)
    {
        perror("Cannot open semaphore");
        exit(EXIT_FAILURE);
    }
    // Loop until interrupt happens by user or server
    while(sigInt == 0) {
        memset(resp.arr, 0, sizeof(resp.arr));
        // Wait for commands from client
        sem_wait(clientB_Sem);
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(pidOfClient, SIGTERM);
            exit(EXIT_SUCCESS);
        }
        // Use read system call to get input from stdin
        if(read(client_fd2, &resp, sizeof(struct response)) != sizeof(struct response)) {
            perror("Couldn't get input from user");
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(pidOfClient, SIGTERM);
            exit(EXIT_FAILURE);
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(pidOfClient, SIGTERM);
            exit(EXIT_SUCCESS);
        }
        // Tokenize the response to check command and arguments
        char data[6][20];
        char delimiters[] = " ";
        int count = 0;
        char *token = strtok(resp.arr, delimiters);
        while (token != NULL)
        {
            strcpy(data[count++], token);
            token = strtok(NULL, delimiters);
        }
        resp.status = RESPONSE_CON;// It will set as RESPONSE_OK at first. If there is a problem with status, resp.status will change later
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(pidOfClient, SIGTERM);
            exit(EXIT_SUCCESS);
        }
        // If command is help
        if (strcmp(data[0], "help") == 0)
        {
            // If user want to see available comments
            if (count == 1)
            {
                char *help_message = "Available comments are :\nhelp, list, readF, writeT, upload, download, archServer, quit, killServer\n";
                strcpy(resp.arr, help_message);
            }
            // If user want to see specific command usage
            else if(count == 2)
            {
                strcpy(resp.arr, getHelpCommands(data[1]));
            } 
            // If the required number of arguments has been exceeded
            else {
                char *countErrorMessage = "The required number of arguments has been exceeded.\n";
                strcpy(resp.arr, countErrorMessage);
            }
        }

        // If command is list
        else if (strcmp(data[0], "list") == 0)
        {
            char message[256];
            message[0] = '\0';
            // If the required number of arguments has been exceeded
            if (count != 1)
            {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            } else {
                /* Reset the position of the directory serverDir to the beginning of the directory */
                rewinddir(serverDir);
                
                struct dirent *file;
                while ((file = readdir(serverDir)) != NULL)
                {
                    /* Don't display . and .. files */
                    if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0) {
                        // Copy file names to message buffer
                        strcat(message, file->d_name);
                        strcat(message, "\n");
                    }
                }
                // Copy message buffer to response array
                strcpy(resp.arr, message);
            }
        }

        // If command is readF
        if (strcmp(data[0], "readF") == 0)
        {
            // If the required number of arguments has been exceeded
            if(count != 2 && count != 3) {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            } else {
                struct stat statbuf; // stat struct to hold file stats
                int fd; // The file descriptor
                file_access_control *tempFac = find_fac(data[1]); // Check if the file exist
                // If is null, file doesn't exist in server directory
                if(tempFac == NULL) {
                    strcpy(resp.arr, "There is no file with this name.\n");
                } else {
                    // Open file for reading
                    fd = open(data[1], O_RDONLY, 0555);
                    if(fd == -1) {
                        strcpy(resp.arr, "Error happened while opening file.\n");
                    }
                    // Get stats for the file
                    if (fstat(fd, &statbuf) == -1) {
                        strcpy(resp.arr, "Failed to get file statistics.\n");
                        close(fd);
                    } else {
                        // If user wants to read all file
                        if (count == 2) 
                        {
                            // Set semaphores to avoid race condition
                            start_reading(tempFac);
                            ssize_t bytesread; // Current byte read
                            long totalRead = 0; // Total read of file
                            memset(resp.arr, 0, sizeof(resp.arr));
                            // Read until eof or some error occur
                            while ((bytesread = read(fd, resp.arr, sizeof(resp.arr))) > 0) {
                                totalRead += bytesread; // Add current byte read to total read
                                // If interrupt happens
                                if(sigInt == 1) {
                                    // Make clean ups and exit
                                    closeClientFiles(client_fd1, client_fd2);
                                    closedir(serverDir);
                                    sem_close(clientA_Sem);
                                    sem_close(clientB_Sem);
                                    close(fd);
                                    kill(pidOfClient, SIGTERM);
                                    exit(EXIT_SUCCESS);
                                }
                                // Write the bytes to client which has read
                                if (write_resp_to_client(client_fd1, &resp) == -1) {
                                    strcpy(resp.arr, "Error occured while writing file lines.\n");
                                    break;
                                }
                                // If interrupt happens
                                if(sigInt == 1) {
                                    // Make clean ups and exit
                                    closedir(serverDir);
                                    sem_close(clientA_Sem);
                                    sem_close(clientB_Sem);
                                    close(fd);
                                    kill(pidOfClient, SIGTERM);
                                    exit(EXIT_SUCCESS);
                                }
                                resp.status = (totalRead < statbuf.st_size) ?  RESPONSE_CON : RESPONSE_OK; // If reading has reached to eof set status to RESPONSE_OK, otherwise RESPONSE_CON
                                // If file is ended
                                if(resp.status == RESPONSE_OK) {
                                    break;
                                }
                            }
                            strcpy(resp.arr, "\nReading file completed\n");
                            // Release semaphores
                            stop_reading(tempFac);
                        } else if(count == 3) {
                            if(checkDigit(data[2]) == -1) {
                                strcpy(resp.arr, "Please enter valid number, not string\n");
                            } else {
                                int lineNum = atoi(data[2]); // Get line number
                                if(lineNum <= 0) {
                                    strcpy(resp.arr, "Line number can't be less than 1\n");
                                } else {
                                    // Set semaphores to avoid race condition
                                    start_reading(tempFac);
                                    int res = find_line(fd, lineNum); // Move cursor to line number
                                    if (res == 0) { // If res is zero, file has finished and line couldn't find
                                        strcpy(resp.arr, "Total number of line was exceed\n");
                                    }
                                    // If res is -1, error occured while trying to find line
                                    else if (res == -1) {
                                        strcpy(resp.arr, "Error while reading from file\n");
                                    }
                                    // Line has found
                                    else {
                                        char *lineMsg = read_line(fd); // Read current line
                                        if(lineMsg == NULL) { // If line couldn't read correctly
                                            strcpy(resp.arr, "Couldn't read the line\n");
                                        } else {
                                            strcpy(resp.arr, lineMsg);
                                        }
                                    }
                                    // Release semaphores
                                    stop_reading(tempFac);
                                }
                            }
                        }
                        close(fd);
                    }
                }
            }
        }
        // If command is writeT
        else if (strcmp(data[0], "writeT") == 0)
        {
            // If the required number of arguments has been exceeded
            if(count > 4 || count < 3) {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            } else {
                int fd; // File descriptor
                int isExist = 1; // Is file already exist
                char msg[BUFF_SIZE]; // Message buffer
                
                // If file couldn't find set isExist to zero
                if(find_fac(data[1]) == NULL) {
                    isExist = 0;
                }

                // Open the file for reading and writing; create it if it doesn't exist
                fd = open(data[1], O_RDWR | O_CREAT, 0644);
                if (fd == -1) {
                    strcpy(resp.arr, "Failed to open file for writing.\n");
                    continue;
                }
                
                int op = 1;
                // If file has created now, add file to fac_table
                if(isExist == 0) {
                    op = add_new_file(data[1]);
                }
                if(op == -1) {
                    strcpy(resp.arr, "Failed while making file initializing.\n");
                    close(fd);
                } else {
                    file_access_control *tempFac = find_fac(data[1]); // Get struct of the file
                    if (count == 4) { // Write to specific line
                        if(checkDigit(data[2]) == -1) {
                            strcpy(resp.arr, "Please enter valid number, not string\n");
                        } else {
                            int lineNum = atoi(data[2]); // Get line number
                            if(lineNum < 1) {
                                strcpy(resp.arr, "Line number can not be less than 1\n");
                            } else {
                                // Set file write semaphore for writing
                                sem_wait(&tempFac->write);
                                int res = find_line(fd, lineNum); // Move cursor to line number
                                if (res == 0) { // If res is zero, file has finished and line couldn't find
                                    strcpy(resp.arr, "Total number of line was exceed\n");
                                }
                                // If res is -1, error occured while trying to find line
                                else if (res == -1) {
                                    strcpy(resp.arr, "Error while reading from file\n");
                                }
                                // Line has found
                                else {
                                    ssize_t bytesTransferred;
                                    // If interrupt happens
                                    if(sigInt == 1) {
                                        // Make clean ups and exit
                                        closeClientFiles(client_fd1, client_fd2);
                                        closedir(serverDir);
                                        sem_close(clientA_Sem);
                                        sem_close(clientB_Sem);
                                        close(fd);
                                        kill(pidOfClient, SIGTERM);
                                        exit(EXIT_SUCCESS);
                                    }
                                    bytesTransferred = write_line(fd, data[3]); // Write line to file
                                    if(bytesTransferred == -1) {
                                        strcpy(resp.arr, "Couldn't write to file\n");
                                    } else {
                                        sprintf(msg, "%ld byte(s) has been written\n", bytesTransferred); // Write message information about how many bytes transferred to client
                                        strcpy(resp.arr, msg);
                                    }
                                }
                                // Release semaphore
                                sem_post(&tempFac->write);
                            }
                        }
                    }
                    else if (count == 3) {
                        // Set file write semaphore for writing
                        sem_wait(&tempFac->write);
                        // Move to the end of the file
                        if (lseek(fd, 0, SEEK_END) == -1) {
                            strcpy(resp.arr, "An error occurred while performing file operations\n");
                        } else {
                            ssize_t bytesTransferred;
                            // If interrupt happens
                            if(sigInt == 1) {
                                // Make clean ups and exit
                                closeClientFiles(client_fd1, client_fd2);
                                closedir(serverDir);
                                sem_close(clientA_Sem);
                                sem_close(clientB_Sem);
                                close(fd);
                                kill(pidOfClient, SIGTERM);
                                exit(EXIT_SUCCESS);
                            }
                            write(fd, "\n", 1);
                            bytesTransferred = write_line(fd, data[2]); // Write line to file
                            if(bytesTransferred == -1) {
                                strcpy(resp.arr, "Couldn't write to file\n");
                            } else {
                                sprintf(msg, "%ld byte(s) has been written\n", bytesTransferred); // Write message information about how many bytes transferred to client
                                strcpy(resp.arr, msg);
                            }
                        }
                        // Release semaphore
                        sem_post(&tempFac->write);
                    }
                }
                close(fd); // Close file
            }
        }

        // If command is upload
        else if (strcmp(data[0], "upload") == 0)
        {
            char msg[512];
            if(count == 2) {
                DIR *clientDir;
                int found = 0;
                /* Open client folder */
                if ((clientDir = opendir(clientPath)) == NULL) {
                    strcpy(resp.arr, "Failed to open client directory\n");
                } else {
                    struct dirent *file;
                    // Try to find if file exist in client directory
                    while ((file = readdir(clientDir)) != NULL) {
                        // File found
                        if (strcmp(file->d_name, data[1]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    closedir(clientDir);
                    // If interrupt happens
                    if(sigInt == 1) {
                        // Make clean ups and exit
                        closeClientFiles(client_fd1, client_fd2);
                        closedir(serverDir);
                        sem_close(clientA_Sem);
                        sem_close(clientB_Sem);
                        kill(pidOfClient, SIGTERM);
                        exit(EXIT_SUCCESS);
                    }
                    if(found == 1) {
                        file_access_control *tempFac = find_fac(data[1]); // Get struct of the file
                        if(tempFac != NULL) { // If file already exist in server directory
                            strcpy(resp.arr, "There is already a file with this name.\n");
                        } else {
                            long bytesTransferred = 0;
                            // Upload file
                            if(upload_file(&bytesTransferred, data[1], clientPath) == -1) {
                                // If interrupt happens
                                if(sigInt == 1) {
                                    // Make clean ups and exit
                                    closeClientFiles(client_fd1, client_fd2);
                                    closedir(serverDir);
                                    sem_close(clientA_Sem);
                                    sem_close(clientB_Sem);
                                    kill(pidOfClient, SIGTERM);
                                    exit(EXIT_SUCCESS);
                                }
                                sprintf(msg, "Error while uploading file\n%ld byte(s) written to file %s\n", bytesTransferred, data[1]);
                                strcpy(resp.arr, msg);
                            } else {
                                sprintf(msg, "%ld byte(s) transffered\n", bytesTransferred);
                                strcpy(resp.arr, msg);
                            }
                        }
                    } else {
                        strcpy(resp.arr, "File doesn't exist in client directory.\n");
                    }
                }
            } 
            // If the required number of arguments has been exceeded
            else {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            }
        }

        else if (strcmp(data[0], "download") == 0)
        {
            char msg[512];
            if(count == 2) {
                DIR *clientDir;
                int found = 0;
                /* Open client folder */
                if ((clientDir = opendir(clientPath)) == NULL) {
                    strcpy(resp.arr, "Failed to open client directory.\n");
                    continue;
                } else {
                    // Try to find if file exist in client directory
                    struct dirent *file;
                    while ((file = readdir(clientDir)) != NULL) {
                        if (strcmp(file->d_name, data[1]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    closedir(clientDir);
                    if(found == 0) {
                        file_access_control *tempFac = find_fac(data[1]); // Get struct of the file
                        if(tempFac == NULL) { // If file has not found in server directory
                            strcpy(resp.arr, "File doesn't exist in server directory.\n");
                        } else {
                            long bytesTransferred = 0;
                            // Download file
                            if(download_file(&bytesTransferred, data[1], clientPath) == -1) {
                                // If interrupt happens
                                if(sigInt == 1) {
                                    // Make clean ups and exit
                                    closeClientFiles(client_fd1, client_fd2);
                                    closedir(serverDir);
                                    sem_close(clientA_Sem);
                                    sem_close(clientB_Sem);
                                    kill(pidOfClient, SIGTERM);
                                    exit(EXIT_SUCCESS);
                                }
                                sprintf(msg, "Error while uploading file\n%ld byte(s) written to file %s\n", bytesTransferred, data[1]);
                                strcpy(resp.arr, msg);
                            } else {
                                sprintf(msg, "%ld byte(s) transffered\n", bytesTransferred);
                                strcpy(resp.arr, msg);
                            }
                        }
                    } else {
                        strcpy(resp.arr, "File already exist in client directory.\n");
                    }
                }

            } 
            // If the required number of arguments has been exceeded
            else {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            }
        }

        else if (strcmp(data[0], "archServer") == 0)
        {
            if (count == 2)
            {
                char filename[256] = "";
                int fullLength = strlen(data[1]);;
                // Copy the filename part
                strncpy(filename, data[1], (fullLength-4));
                filename[fullLength-4] = '\0';  // Null-terminate the filename array
                char msg[512];
                char archPath[PATH_LEN];
                snprintf(archPath, sizeof(archPath), "%s/%s", clientPath, filename);
                int fileCount = 0;
                struct dirent *file; // Struct for file in the server directory
                long totalBytesTransferred = 0; // Total how many bytes transferred
                long bytesTransferred; // Current file how many bytes transferred
                /* Reset the position of the directory serverDir to the beginning of the directory */
                rewinddir(serverDir);
                while ((file = readdir(serverDir)) != NULL) {
                    // Don't include "." and ".." files
                    if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0 && strcmp(file->d_name, "server_log.txt") != 0) {
                        bytesTransferred = 0;
                        // Download file
                        if(download_file(&bytesTransferred, file->d_name, archPath) == -1) {
                            // If interrupt happens
                            if(sigInt == 1) {
                                // Make clean ups and exit
                                closeClientFiles(client_fd1, client_fd2);
                                closedir(serverDir);
                                sem_close(clientA_Sem);
                                sem_close(clientB_Sem);
                                kill(pidOfClient, SIGTERM);
                                exit(EXIT_SUCCESS);
                            }
                            sprintf(msg, "archServer error while uploading file\n"); // If error happened add this error information to head of the response message
                            break;
                        } else {
                            fileCount++;
                            totalBytesTransferred += bytesTransferred;
                        }
                    }
                }
                sprintf(msg, "%d files downloaded ..%ld byte(s) transferred ..\n", fileCount, totalBytesTransferred); // Write information about file operations
                strcpy(resp.arr, msg);
            }
            // If the required number of arguments has been exceeded
            else {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            }
        }


        else if (strcmp(data[0], "quit") == 0)
        {
            if(count == 1) {
                // Clean, set message for client and quit
                printf("%s disconnected..\n", clientName);
                strcpy(resp.arr, "quit");
                write_resp_to_client(client_fd1, &resp);
                closeClientFiles(client_fd1, client_fd2);
                closedir(serverDir);
                sem_post(clientA_Sem);
                sem_close(clientA_Sem);
                sem_close(clientB_Sem);
                exit(EXIT_SUCCESS);
            }
            // If the required number of arguments has been exceeded
            else {
                strcpy(resp.arr, "The required number of arguments has been exceeded.\n");
            }
        }

        else if (strcmp(data[0], "killServer") == 0)
        {
            printf("Kill signal from client%d.. terminating...\n", pidOfClient);
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_post(clientA_Sem);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(getppid(), SIGINT); // Send kill signal to parent to terminate server
            exit(EXIT_SUCCESS);
        }

        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            closeClientFiles(client_fd1, client_fd2);
            closedir(serverDir);
            sem_close(clientA_Sem);
            sem_close(clientB_Sem);
            kill(pidOfClient, SIGTERM);
            exit(EXIT_SUCCESS);
        }
        resp.status = RESPONSE_OK;
        write_resp_to_client(client_fd1, &resp); // Write response to client
        sem_post(clientA_Sem); // Set semaphore, so client can read now
    }
    // If interrupt happens
        if(sigInt == 1) {
        // Make clean ups and exit
        closeClientFiles(client_fd1, client_fd2);
        closedir(serverDir);
        sem_close(clientA_Sem);
        sem_close(clientB_Sem);
        kill(pidOfClient, SIGTERM);
        exit(EXIT_SUCCESS);
    }
}

/* Get help commands message */
char *getHelpCommands(char *arr) {
    if (strcmp(arr, "help") == 0)
    {
        return "help <command>\n\tdisplay the usage of <command>\n\0";
    }
    else if (strcmp(arr, "list") == 0)
    {
        return "list\n\tdisplay the list of files in Servers directory\n\t(also displays the list received from the Server)\n\0";
    }
    else if (strcmp(arr, "readF") == 0)
    {
        return "readF <file> <line #>\ndisplay the #th line of the <file>, returns with an\nerror if <file> does not exists\n\0";
    }
    else if (strcmp(arr, "writeT") == 0)
    {
        return "writeT <file> <line #> <string>\ndisplay the #th line of the <file>, returns with an\nerror if <file> does not exists  <string> write string \n\0";
    }

    else if (strcmp(arr, "upload") == 0)
    {
        return "uploads the file from the current working directory of client to the Servers directory\n\0";
    }
    else if (strcmp(arr, "download") == 0)
    {
        return "request to receive <file> from Servers directory to client side\n\0";
    }
    else if (strcmp(arr, "archServer") == 0)
    {
        return "archServer <fileName>.tar\nUsing fork, exec and tar utilities create a child process that will collect all the files currently available on the the Server side and store them in the <filename>.tar archive\n\0";
    }
    else if (strcmp(arr, "killServer") == 0)
    {
        return "Sends a kill request to the Server\n\0";
    }
    else if (strcmp(arr, "quit") == 0)
    {
        return "Send write request to Server side log file and quits\n\0";
    }
    return "Invalid command\n\0";
}

// Find a file access control by filename
file_access_control *find_fac(const char *filename) {
    // Look for all files
    for (int i = 0; i < MAX_FILES; i++) {
        // If table is not null and name is found return this table entry
        if (fac_table[i] != NULL && strcmp(fac_table[i]->filename, filename) == 0) {
            return fac_table[i];
        }
    }
    return NULL;
}

/* Set file semaphores for reading */
void start_reading(file_access_control *fac) {
    /* A reader is trying to enter */
    if (sem_wait(&fac->mutex) == -1) {
        exit(EXIT_FAILURE);
    }
    fac->reader_count++;
    if (fac->reader_count == 1) {
        sem_wait(&fac->write);
    }
    /* You are done trying to access the resource */
    if (sem_post(&fac->mutex) == -1) {
        exit(EXIT_FAILURE);
    }
}

/* Set file semaphores after reading */
void stop_reading(file_access_control *fac) {
    sem_wait(&fac->mutex);
    fac->reader_count--;
    if (fac->reader_count == 0) {
        sem_post(&fac->write);
    }
    sem_post(&fac->mutex);
}

/* Set file write semaphore for writing */
void start_writing(file_access_control *fac) {
    sem_wait(&fac->write);
}

/* Set file write semaphore after writing */
void stop_writing(file_access_control *fac) {
    sem_post(&fac->write);
}

/* Initialize each file's struct in the server directory */
int init_fac_table(DIR *serverDir) {
    // Initialization
    memset(fac_table, 0, sizeof(fac_table));
    rewinddir(serverDir);
    struct dirent *file;
    // All files in server directory
    while ((file = readdir(serverDir)) != NULL) {
        // Don't include "." and ".." files
        if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0) {
            if(init_fac(file->d_name) == -1) {
                return -1;
            }
        }
    }
    return 0;
}

/* Initialize a file control structure */
int init_fac(const char *filename) {
    // Loop for max files
    for (int i = 0; i < MAX_FILES; i++) {
        // If fac table is null, that means this slot is empty
        if (fac_table[i] == NULL) {
            // Create shared memory for the file
            int shm_fd = shm_open(filename, O_CREAT | O_RDWR, 0666);
            if (shm_fd == -1) {
                perror("asd");
                return -1;
            }
            ftruncate(shm_fd, sizeof(file_access_control));
            fac_table[i] = mmap(NULL, sizeof(file_access_control), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            close(shm_fd);

            if (fac_table[i] == MAP_FAILED) {
                perror("asd");
                return -1;
            }

            // Set file properties
            strncpy(fac_table[i]->filename, filename, 256);
            fac_table[i]->reader_count = 0;
            // Initialize write and read semaphores
            sem_init(&fac_table[i]->mutex, 1, 1);
            sem_init(&fac_table[i]->write, 1, 1);
            // Finish function after file initialize
            return i;
        }
    }
    // If fac table is full return -1
    fprintf(stderr, "Error: Maximum file limit reached.\n");
    return -1;
}

/* Add new file to fac table */
int add_new_file(const char *filename) {
    return init_fac(filename);
}

/* Read current line from the file */
char *read_line(int fd)
{
    static char buffer[2048];
    char ch;
    int currentIndex = 0;
    ssize_t bytesread;
    while(1) {
        // Read current line 
        while(((bytesread = read(fd, &ch,1)) == -1) && (errno == EINTR)) ;
        if(bytesread == -1) { // ERROR while reading file
            return NULL;
        } 
        // If it's eof and it didn't read anything return NULL
        else if(bytesread == 0) { // EOF
            if(currentIndex == 0) {
                return NULL;
            }
            break;
        }
        buffer[currentIndex] = ch;
        ++currentIndex;
        // If last char is \n it's eof, break loop
        if(ch == '\n')
            break;
    }
    buffer[currentIndex] = '\0';
    // Return read line
    return buffer;
}

/* Find specific line number in the file */
int find_line(int fd, int lineNum) {
    // If line number is zero
    if(lineNum == 0) {
        return 0;
    } else if (lineNum == 1) {
        return 1;
    }
    // Set cursor to beginning of the file
    lseek(fd, 0, SEEK_SET);
    ssize_t bytesread;
    int line_count = 1;
    char c;
    // Get each characters in line until end of line and increase line count
    for (int num_read = 0; (bytesread = read(fd, &c, 1)) == 1; ++num_read) {
        if (c == '\n') {
            ++line_count;
            // If line count is equal lineNum break the loop
            if (line_count == lineNum)
                break;
        }
    }
    // End of file
    if (bytesread == 0) {
        return 0;
    }
    // Error while reading
    else if (bytesread == -1) {
        return -1;
    } 
    // Go to specific line correctly
    else {
        return line_count;
    }
}

/* Write str to current line in the file */
int write_line(int fd, char *str) {
    ssize_t byteswritten;
    // Write str to end of this line
    while(((byteswritten=write(fd, str, strlen(str)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    if(byteswritten == -1) {
        return -1;
    }
    return byteswritten;
}

/* Write resp struct to client fifo */
int write_resp_to_client(int clientFd, struct response *resp) {
    // If write is not handled correctly
    if (write(clientFd, resp, sizeof(struct response)) != sizeof(struct response))
    {
        fprintf(stderr, " error waiting to fifo %d\n", clientFd);
        return -1;
    }
    return 0;
}

int upload_file(long *totalBytesTransferred, char *fileName, char *clientPath) {
    ssize_t bytesread;
    char buffer[BUFF_SIZE];
    int srcFd;
    int dstFd;
    char absClientPath[PATH_LEN];
    // Open file for reading
    snprintf(absClientPath, PATH_LEN, "%s/%s", clientPath, fileName);
    srcFd = open(absClientPath, O_RDONLY, 0555);
    if (srcFd == -1) {
        return -1;
    }

    // Open file for writing
    dstFd = open(fileName, O_WRONLY | O_CREAT, 0644);
    if(dstFd == -1) {
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
    // Add new file to fac_table
    if(add_new_file(fileName) == -1) {
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
    file_access_control *tempFac = find_fac(fileName); // Get struct of the file
    // Set file semaphore for writing to avoid race condition
    start_writing(tempFac);
    // Read from source file(client)
    while((bytesread=read(srcFd, buffer, BUFF_SIZE)) > 0) { // To make sure that the string is read correctly without interrupting
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        if(bytesread == -1) {
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        // Write to destination file(server)
        if (write(dstFd, buffer, bytesread) != bytesread) {
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            *totalBytesTransferred += bytesread;
            return -1;
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        *totalBytesTransferred += bytesread;
    }
    close(srcFd);
    close(dstFd);
    // Release semaphore
    stop_writing(tempFac);
    return 0;
}

int download_file(long *totalBytesTransferred, char *fileName, char *clientPath) {
    ssize_t bytesread;
    char buffer[BUFF_SIZE];
    int srcFd;
    int dstFd;

    // Open file for reading
    srcFd = open(fileName, O_RDONLY, 0555);
    if (srcFd == -1) {
        return -1;
    }

    // Open file for writing
    char absClientPath[PATH_LEN];
    snprintf(absClientPath, PATH_LEN, "%s/%s", clientPath, fileName);
    dstFd = open(absClientPath, O_WRONLY | O_CREAT, 0644);
    if(dstFd == -1) {
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
    file_access_control *tempFac = find_fac(fileName); // Get struct of the file
    // Set file semaphore for reading to avoid race condition
    start_reading(tempFac);
    // Read from source file(server)
    while(((bytesread=read(srcFd, buffer, BUFF_SIZE)) > 0) && (errno != EINTR)) { // To make sure that the string is read correctly without interrupting
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        if(bytesread == -1) {
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        // Write to destination file(client)
        if (write(dstFd, buffer, bytesread) != bytesread) {
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            *totalBytesTransferred += bytesread;
            return -1;
        }
        // If interrupt happens
        if(sigInt == 1) {
            // Make clean ups and exit
            close(srcFd);
            close(dstFd);
            stop_writing(tempFac);
            return -1;
        }
        *totalBytesTransferred += bytesread;
    }
    close(srcFd);
    close(dstFd);
    // Release semaphore
    stop_reading(tempFac);
    return 0;
}

/* Kill all child and client processes */
void kill_childs(int childSize, int *clients) {
    int i;
    /*  Send SIGTERM signal to all client processes and SIGINT to all child processes*/
    for (i = 0; i < maxClients; ++i) {
        if(clients[i] != -1) {
            kill(clients[i], SIGTERM);
        }
    }
    printf("\n");
    /*  Wait for all child processes to exit */
    for(int i = 0; i < childSize; ++i)
        waitpid(-1, NULL, 0); // Making sure all child processes are finished with their job
}

/* Check str if it's digit */
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
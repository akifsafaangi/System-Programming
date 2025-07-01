#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include "utility.h"
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

/* Manager thread function */
void* manager(void* arg);

/* Create directory structure */
void createDirectory(const char* srcDirPath, const char* destDirPath);

/* Worker thread function */
void* worker(void* arg);

/* Add item to buffer */
void add_item(Files item);

/* Remove item from buffer */
Files remove_item();

int *maxBuffer; // Maximum buffer size
int fileCount = 0; // Number of regular files
int dirCount = 0; // Number of directories
int fifoCount = 0; // Number of FIFO files
long totalBytes = 0; // Total bytes copied


int sigInt = 0;
void signalHandler(int signalNum) {
    pthread_mutex_lock(&flag);
    sigInt = 1;
    pthread_mutex_unlock(&flag);
}
// Function to convert clock ticks to milliseconds
long convertTicksToMilliseconds(clock_t ticks) {
    return (ticks * 1000) / CLOCKS_PER_SEC;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s <bufferSize> <numberOfWorkers> <sourceDirectory> <destinationDirectory>\n", argv[0]);
        return 1;
    }
    int s;

    //SIGINT handler
    struct sigaction sa_action={0};
    sigemptyset(&sa_action.sa_mask);
    sigaddset(&sa_action.sa_mask, SIGRTMIN);
    sa_action.sa_handler=signalHandler;
    while(((s=sigaction(SIGINT, &sa_action, NULL))==-1) && errno==EINTR);
    if(s==-1)
    {
        perror("Cannot assign signal handler.\n");
        exit(EXIT_FAILURE);
    }

    int bufferSize = atoi(argv[1]);
    int numberOfWorkers = atoi(argv[2]);

    maxBuffer = (int*)malloc(sizeof(int)); // Allocate memory for maxBuffer
    *maxBuffer = bufferSize;
    initBuffer(bufferSize); // Initialize buffer

    if(sigInt == 1) {
        clean();
        free(maxBuffer);
        exit(1);
    }

    pthread_t managerThread;
    pthread_t workerThreads[numberOfWorkers];

    s = pthread_mutex_init(&thread_mutex, 0);
    if (s != 0) {
        printf("Error creating mutex\n");
        return 1;
    }
    s = pthread_mutex_init(&flag, 0);
    if (s != 0) {
        printf("Error creating mutex\n");
        return 1;
    }
    s = pthread_cond_init(&condc, 0);
    if (s != 0) {
        printf("Error creating condition variable\n");
        return 1;
    }
    s = pthread_cond_init(&condp, 0);
    if (s != 0) {
        printf("Error creating condition variable\n");
        return 1;
    }

    s = pthread_create(&managerThread, NULL, manager, (void *)argv); // Create manager thread
    if (s != 0) {
        printf("Error creating manager thread\n");
        return 1;
    }

    for (int i = 0; i < numberOfWorkers; i++) { // Create worker threads
        s = pthread_create(&workerThreads[i], NULL, worker, NULL);
        if (s != 0) {
            printf("Error creating worker thread\n");
            return 1;
        }
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Wait for threads to finish
    s = pthread_join(managerThread, NULL);
    if (s != 0) {
        printf("Error joining manager thread\n");
        return 1;
    }
    for (int i = 0; i < numberOfWorkers; i++) {
        s = pthread_join(workerThreads[i], NULL);
        if (s != 0) {
            printf("Error joining worker thread\n");
            return 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    // Calculate elapsed time
    long elapsedMilliseconds = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    long minutes = elapsedMilliseconds / 60000;
    long seconds = (elapsedMilliseconds % 60000) / 1000;
    long miliseconds = elapsedMilliseconds % 1000;

    printf("\n---------------STATISTICS--------------------\n");
    printf("Consumers: %d - Buffer Size: %d\n", numberOfWorkers, bufferSize);
    printf("Number of Regular File: %d\n", fileCount);
    printf("Number of FIFO File: %d\n", fifoCount);
    printf("Number of Directory: %d\n", dirCount);
    printf("TOTAL BYTES COPIED: %ld\n", totalBytes);
    printf("TOTAL TIME: %02ld:%02ld.%03ld (min:sec.mili)\n", minutes, seconds, miliseconds);

    // Clean up
    clean();
    free(maxBuffer);
    pthread_cond_destroy(&condc);
    pthread_cond_destroy(&condp);
    pthread_mutex_destroy(&flag);
    pthread_mutex_destroy(&thread_mutex);
    return 0;
}

void* manager(void* arg) {
    char **argv = (char **)arg;
    createDirectory(argv[3], argv[4]); // Create directory structure
    // Set done flag when all files are processed
    pthread_mutex_lock(&thread_mutex);
    buffer.doneFlag = 1;
    pthread_cond_broadcast(&condc);
    pthread_mutex_unlock(&thread_mutex);
    pthread_exit(0);
}

void createDirectory(const char* srcDirPath, const char* destDirPath) {
    DIR* srcDir;
    struct dirent *entry;
    struct stat statBuf;
    char srcPath[PATH_SIZE];
    char destPath[PATH_SIZE];

    srcDir = opendir(srcDirPath); // Open source directory
    if(srcDir == NULL) {
        perror("Error opening source directory");
        exit(1);
    }

    // Iterate over directory entries
    while ((entry = readdir(srcDir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) { // Skip . and ..
            continue;
        }

        snprintf(srcPath, sizeof(srcPath), "%s/%s", srcDirPath, entry->d_name);
        snprintf(destPath, sizeof(destPath), "%s/%s", destDirPath, entry->d_name);

        if (stat(srcPath, &statBuf) == -1) {
            perror("Error getting file status");
            continue;
        }

        if(sigInt == 1) {
            break;
        }
        if (S_ISDIR(statBuf.st_mode)) { // Directory
            // Create directory
            if (mkdir(destPath, 0755) == -1 && errno != EEXIST) {
                perror("Error creating directory");
                continue;
            }
            dirCount++; // Increment directory count
            // Recursively create subdirectories and files
            createDirectory(srcPath, destPath);
        } else if (S_ISREG(statBuf.st_mode)) { // Regular file
            int srcFd;
            int destFd;

            // Open source file
            srcFd = open(srcPath, O_RDONLY, 0444);
            if (srcFd == -1) {
                perror("Error while opening source file");
                continue;
            }
            // Create an empty file
            destFd = open(destPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (destFd == -1) {
                close(srcFd);
                perror("Error while creating destination file");
            }
            fileCount++; // Increment file count
            
            Files item; // Create a new item
            item.srcFd = srcFd; // Set source file descriptor
            item.destFd = destFd; // Set destination file descriptor
            strcpy(item.srcName, srcPath); // Set source file name
            strcpy(item.destName, destPath); // Set destination file name
            if(sigInt == 1) {
                close(srcFd);
                close(destFd);
                break;
            }
            add_item(item); // Add item to buffer
        } else if (S_ISFIFO(statBuf.st_mode)) { // FIFO file
            // Create FIFO
            if (mkfifo(destPath, 0644) == -1) {
                perror("Error creating FIFO");
                continue;
            }
            fifoCount++;
        }
    }
    closedir(srcDir);
}

void* worker(void* arg) {
    while (1) {
        if(sigInt == 1) {
            break;
        }
        Files item = remove_item();
        if (item.srcFd == -1 || item.destFd == -1) {
            if (item.srcFd == -1 && item.destFd == -1) {
                break;
            }
            continue;
        }
        if(sigInt == 1) {
            close(item.srcFd);
            close(item.destFd);
            break;
        }

        // Copy file content from srcFd to destFd
        char buffer[1024];
        ssize_t bytes_read;
        ssize_t bytes_write;
        long totalWritten = 0;
        while ((bytes_read = read(item.srcFd, buffer, sizeof(buffer))) > 0) {
            bytes_write = write(item.destFd, buffer, bytes_read);
            if(sigInt == 1) {
                close(item.srcFd);
                close(item.destFd);
                break;
            }
            if (bytes_write != bytes_read) {
                perror("Error writing to destination file");
                break;
            }
            totalWritten += bytes_write;
        }
        // Close file descriptors
        close(item.srcFd);
        close(item.destFd);

        pthread_mutex_lock(&thread_mutex); // lock
        totalBytes += totalWritten; // Increment total bytes
        // Print completion status to standard output
        printf("Copied %s to %s\n", item.srcName, item.destName);
        pthread_mutex_unlock(&thread_mutex); // unlock
    }
    pthread_exit(0);
}

void add_item(Files item) {
    pthread_mutex_lock(&thread_mutex); // lock
    while(buffer.count == *maxBuffer) pthread_cond_wait(&condp, &thread_mutex); // buffer is full
    buffer.buffer[buffer.last] = item; // add item to buffer
    buffer.last = (buffer.last + 1) % buffer.bufferSize; // increment last for next item
    buffer.count++; // increment count
    pthread_cond_signal(&condc); // signal to consumer
    pthread_mutex_unlock(&thread_mutex); // unlock
}

Files remove_item() {
    pthread_mutex_lock(&thread_mutex);
    while(buffer.count == 0 && buffer.doneFlag == 0) pthread_cond_wait(&condc, &thread_mutex); // buffer is empty and producer is not done
    if(buffer.count > 0) { // buffer is not empty
        Files item = buffer.buffer[buffer.head]; // get item from buffer
        buffer.head = (buffer.head + 1) % buffer.bufferSize; // increment head for next item
        buffer.count--; // decrement count
        pthread_cond_signal(&condp); // signal to producer
        pthread_mutex_unlock(&thread_mutex); // unlock
        return item; // return item
    }
    pthread_mutex_unlock(&thread_mutex); // unlock
    return (Files){-1, -1, "", ""}; // return empty item
}
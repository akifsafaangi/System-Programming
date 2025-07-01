#include <stdio.h>
#include <unistd.h>

#define PATH_SIZE 1024

pthread_mutex_t the_mutex; // Mutex for synchronization between threads
pthread_cond_t condc, condp; // Condition variables for controlling if buffer is full or empty
pthread_mutex_t flag; // Mutex for SIGINT
pthread_barrier_t workerBarrier; // Barrier for workers


typedef struct {
    int srcFd; // File descriptor for source file
    int destFd; // File descriptor for destination file
    char srcName[PATH_SIZE]; // Source file name
    char destName[PATH_SIZE]; // Destination file name
} Files;

typedef struct {
    int bufferSize; // Buffer size
    Files *buffer; // Buffer
    int count; // Number of items in buffer
    int head; // Head of buffer
    int last; // Last of buffer
    int doneFlag; // Done flag
} Buffer;

Buffer buffer;

// Initialize buffer
void initBuffer(int size ) {
    buffer.bufferSize = size; // Set buffer size
    buffer.buffer = malloc(sizeof(Files) * size); // Allocate memory for buffer
    // Set buffer properties
    buffer.count = 0;
    buffer.head = 0;
    buffer.last = 0;
    buffer.doneFlag = 0;
}

// Clean buffer
void clean() {
    while(buffer.count > 0) {
        // Close file descriptors
        close(buffer.buffer[buffer.head].srcFd);
        close(buffer.buffer[buffer.head].destFd);
        buffer.count--;
        buffer.head = (buffer.head + 1) % buffer.bufferSize;
    }
    // Free buffer memory
    if(buffer.buffer != NULL) {
        free(buffer.buffer);
    }
}
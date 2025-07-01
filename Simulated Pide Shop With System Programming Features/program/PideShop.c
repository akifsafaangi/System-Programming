#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "utility.h"
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include "SudoInverse.h"

#define LOG_FILE "shop_log.txt" // Log file name
pthread_mutex_t deliveryMutex; // Mutex for synchronization between delivery threads and manager
pthread_cond_t deliveryCond; // Condition variable for controlling delivery threads

pthread_mutex_t cookMutex; // Mutex for synchronization between cooks and manager
pthread_mutex_t mealMutex; // Mutex for synchronization between cooked meals and manager
pthread_cond_t condCook; // Condition variable for controlling cook threads
pthread_cond_t condManager; // Condition variable for controlling manager thread
pthread_cond_t clientCond; // Condition to wait for all deliveries to be done before finishing manager

sem_t oven_space; // Semaphore for the oven spaces
sem_t door_access; // Semaphore for the oven doors
sem_t shovels; // Semaphore for the shovels

int delivery_count = 0; // Number of deliveries finished
int *delivery_order_count; // Number of orders delivered by each delivery personel
int deliverySpeed; // Speed of the delivery personel
int deliveryPoolSize; // Number of delivery personel threads

int *cook_order_count; // Number of orders cooked by each cook
int cookPoolSize; // Number of cook threads
int orderIdIndex = 0; // Index for order ids

int customers_to_serve = 0; // Number of customers to serve
int total_customers = 0; // Total number of customers
int prepared_meals = 0; // Number of prepared meals

typedef struct {
    int cookId;
    int orderId;
} cookOrderInfo; // Struct to hold cook ids for each meal
cookOrderInfo *prepared_by_cook; // Dynamic array to hold cook ids for each meal

typedef struct {
    int pid;
    int cookId;
    int p;
    int q;
    int totalTime;
    int order_id;
} order; // Struct to hold order information
int deliveryPackage = 0; // How many orders are ready to be delivered

int logFile; // File descriptor for log file
pthread_mutex_t logMutex; // Mutex for log file
/* Default format for time, date, message */
static const char default_format[] = "%b %d %Y %Z %H %M %S";


/* Manager thread function */
void* manager(void* arg);

/* Function to handle the client connection */
void handle_client();

/* Cook thread function */
void* cook(void* arg);

/* Cook handler function */
void cook_handler(int cook_id, int cook_index);

/* Deliver Personel thread function */
void* deliveryPersonel(void* arg);

/* Delivery handler function */
void delivery_handler(int delivery_id, int delivery_index);

/* Function to generate random p and q values */
void generateRandomPQ(int *p, int *q, int maxP, int maxQ);

/* Function to write log message to the log file */
void writeLogMessage(char* startMessage);

/* Function to create a message with time */
void createMessageWithTime(char* startMessage, char* message);

/* Function to clear resources */
void clear();

/* Function to find the minimum of two integers */
int min(int a, int b);

/* Function to find the index of the maximum element in an array */
int findMaxIndex(int *arr, int size);

int server_socket; // Server socket
int client_sock; // Client socket
int clientPid = 0; // Client PID

typedef struct queue_node {
    order ord;
    struct queue_node* next;
} queue_node; // Node for the order queue

typedef struct {
    queue_node* front;
    queue_node* rear;
    int size;
} order_queue; // Queue for the orders

order_queue* delivery_queue; // Queue for the delivery orders

/* Function to initialize the queue */
void init_queue(order_queue* q) {
    q->front = q->rear = NULL;
    q->size = 0;
}

/* Function to check if the queue is empty */
int is_queue_empty(order_queue* q) {
    return q->size == 0;
}

/* Function to enqueue an order */
void enqueue(order_queue* q, order ord) {
    queue_node* new_node = (queue_node*)malloc(sizeof(queue_node));
    new_node->ord = ord;
    new_node->next = NULL;
    if (q->rear == NULL) {
        q->front = q->rear = new_node;
    } else {
        q->rear->next = new_node;
        q->rear = new_node;
    }
    q->size++;
}

/* Function to dequeue an order */
order dequeue(order_queue* q) {
    if (is_queue_empty(q)) {
        order empty_order;
        empty_order.order_id = -1;
        return empty_order;
    }
    queue_node* temp = q->front;
    order ord = temp->ord;
    q->front = q->front->next;
    if (q->front == NULL) {
        q->rear = NULL;
    }
    free(temp);
    q->size--;
    return ord;
}

volatile sig_atomic_t sigInt = 0; // Signal int flag
/* Signal handler for SIGINT */
void sig_handler(int sig) {
    printf(".. Upps quiting.. writing log file\n");
    sigInt = 1;
    if(clientPid != 0) {
        kill(clientPid, SIGUSR1);
    }
    pthread_cond_broadcast(&condCook);
    pthread_cond_broadcast(&deliveryCond);
    char log_msg[100];
    sprintf(log_msg, "Shop has burned\n");
    writeLogMessage(log_msg);
    // Shutdown the server socket to unblock accept call
    if (server_socket >= 0) {
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
    }
}

volatile sig_atomic_t sigUsr1 = 0; // Signalusr1 flag
void sigusr1_handler(int sig) {

}


int main(int argc, char *argv[]) {
    if(argc != 6) {
        printf("Usage: %s <ipaddress> <portnumber> <CookthreadPoolsize> <DeliveryPoolSize> <deliverySpeed>\n", argv[0]);
        exit(0);
    }
    const char *ip_address = argv[1];
    int port = atoi(argv[2]);
    cookPoolSize = atoi(argv[3]);
    deliveryPoolSize = atoi(argv[4]);
    deliverySpeed = atoi(argv[5]);
    int a;

    delivery_order_count = (int*)malloc(deliveryPoolSize * sizeof(int)); // Allocate memory for delivery order count
    if (delivery_order_count == NULL) {
        perror("Failed to allocate memory for delivery order count");
        exit(EXIT_FAILURE);
    }
    cook_order_count = (int*)malloc(cookPoolSize * sizeof(int)); // Allocate memory for cook order count
    if (cook_order_count == NULL) {
        perror("Failed to allocate memory for cook order count");
        // Cleanups
        free(delivery_order_count);
        exit(EXIT_FAILURE);
    }
    delivery_queue = (order_queue*)malloc(sizeof(order_queue)); // Allocate memory for delivery queue
    if (delivery_queue == NULL) {
        perror("Failed to allocate memory for delivery queue");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        exit(EXIT_FAILURE);
    }
    init_queue(delivery_queue); // Initialize the delivery queue

    //SIGINT handler
    struct sigaction sa_action={0};
    sigemptyset(&sa_action.sa_mask);
    sa_action.sa_handler=sig_handler;
    sa_action.sa_flags=0;
    while(((a=sigaction(SIGINT, &sa_action, NULL))==-1) && errno==EINTR);
    if(a==-1)
    {
        perror("Cannot assign signal handler.\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
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
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        exit(1);
    }





    prepared_by_cook = (cookOrderInfo *)malloc(cookPoolSize * sizeof(cookOrderInfo)); // Allocate memory for prepared meals
    if (prepared_by_cook == NULL) {
        perror("Failed to allocate memory for cook ids");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        exit(EXIT_FAILURE);
    }

    /* Create log file */
    logFile = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(logFile == -1) {
        perror("The file cannot be opened\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        exit(-1);
    }

    pthread_t managerThread; // Manager thread
    pthread_t cookThread[cookPoolSize]; // Cook threads
    pthread_t deliveryPersonelThread[deliveryPoolSize]; // Delivery personel threads


    int opt = 1;
    struct sockaddr_in address;

    // socket create and verification
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        printf("socket creation failed...\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        exit(0);
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip_address);
    address.sin_port = htons(port);
    // Binding newly created socket to given IP and verification
    if ((bind(server_socket, (struct sockaddr*)&address, sizeof(address))) != 0)
    {
        perror("socket bind failed...\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        close(server_socket);
        exit(0);
    }


    a = pthread_cond_init(&clientCond, 0); // Initialize the condition variable
    if (a != 0) {
        printf("Error creating condition variable\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        close(server_socket);
        return 1;
    }

    a = pthread_mutex_init(&cookMutex, 0); // Initialize the mutex
    if (a != 0) {
        printf("Error creating mutex\n");
        // Cleanups
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = pthread_mutex_init(&mealMutex, 0); // Initialize the mutex
    if (a != 0) {
        printf("Error creating mutex\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }

    a = pthread_cond_init(&condCook, 0); // Initialize the condition variable
    if (a != 0) {
        printf("Error creating condition variable\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }

    a = pthread_cond_init(&condManager, 0); // Initialize the condition variable
    if (a != 0) {
        printf("Error creating condition variable\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }

    a = sem_init(&oven_space, 0, 6); // Initialize the semaphore
    if (a != 0) {
        printf("Error creating semaphore\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = sem_init(&door_access, 0, 2); // Initialize the semaphore
    if (a != 0) {
        printf("Error creating semaphore\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        sem_destroy(&oven_space);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = sem_init(&shovels, 0, 3); // Initialize the semaphore
    if (a != 0) {
        printf("Error creating semaphore\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        sem_destroy(&oven_space);
        sem_destroy(&door_access);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }

    a = pthread_mutex_init(&deliveryMutex, 0); // Initialize the mutex
    if (a != 0) {
        printf("Error creating mutex\n");
        // Cleanups
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        sem_destroy(&oven_space);
        sem_destroy(&door_access);
        sem_destroy(&shovels);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = pthread_cond_init(&deliveryCond, 0); // Initialize the condition variable
    if (a != 0) {
        printf("Error creating condition variable\n");
        // Cleanups
        pthread_mutex_destroy(&deliveryMutex);
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        sem_destroy(&oven_space);
        sem_destroy(&door_access);
        sem_destroy(&shovels);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        pthread_cond_destroy(&clientCond);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = pthread_mutex_init(&logMutex, 0); // Initialize the mutex
    if (a != 0) {
        printf("Error creating mutex\n");
        // Cleanups
        pthread_mutex_destroy(&deliveryMutex);
        pthread_cond_destroy(&deliveryCond);
        pthread_mutex_destroy(&cookMutex);
        pthread_mutex_destroy(&mealMutex);
        pthread_cond_destroy(&condCook);
        pthread_cond_destroy(&condManager);
        pthread_cond_destroy(&clientCond);
        sem_destroy(&oven_space);
        sem_destroy(&door_access);
        sem_destroy(&shovels);
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        close(server_socket);
        return 1;
    }
    a = pthread_create(&managerThread, NULL, manager, NULL); // Create manager thread
    if (a != 0) {
        printf("Error creating manager thread\n");
        clear();
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        close(logFile);
        close(server_socket);
        return 1;
    }

    int *cook_ids = malloc(cookPoolSize * sizeof(int));
    for (int i = 0; i < cookPoolSize; i++) {
        cook_ids[i] = i + 1;
    }
    for (int i = 0; i < cookPoolSize; i++) { // Create worker threads
        a = pthread_create(&cookThread[i], NULL, cook, &cook_ids[i]);
        if (a != 0) {
            printf("Error creating cook thread\n");
        // Cleanups
            clear();
            free(delivery_order_count);
            free(cook_order_count);
            free(delivery_queue);
            free(prepared_by_cook);
            free(cook_ids);
            close(logFile);
            return 1;
        }
    }

    int *delivery_ids = malloc(deliveryPoolSize * sizeof(int));
    for (int i = 0; i < deliveryPoolSize; i++) {
        delivery_ids[i] = i + 1;
    }
    for (int i = 0; i < deliveryPoolSize; i++) { // Create worker threads
        a = pthread_create(&deliveryPersonelThread[i], NULL, deliveryPersonel, &delivery_ids[i]);
        if (a != 0) {
            printf("Error creating deliveryPersonel thread\n");
        // Cleanups
            clear();
            free(delivery_order_count);
            free(cook_order_count);
            free(delivery_queue);
            free(prepared_by_cook);
            free(cook_ids);
            free(delivery_ids);
            close(logFile);
            return 1;
        }
    }

    // Wait for threads to finish
    a = pthread_join(managerThread, NULL);
    if (a != 0) {
        printf("Error joining manager thread\n");
        // Cleanups
        clear();
        free(delivery_order_count);
        free(cook_order_count);
        free(delivery_queue);
        free(prepared_by_cook);
        free(cook_ids);
        free(delivery_ids);
        close(logFile);
        return 1;
    }
    for (int i = 0; i < cookPoolSize; i++) {
        a = pthread_join(cookThread[i], NULL);
        if (a != 0) {
            printf("Error joining worker thread\n");
            // Cleanups
            clear();
            free(delivery_order_count);
            free(cook_order_count);
            free(delivery_queue);
            free(prepared_by_cook);
            free(cook_ids);
            free(delivery_ids);
            close(logFile);
            return 1;
        }
    }
    for (int i = 0; i < deliveryPoolSize; i++) {
        a = pthread_join(deliveryPersonelThread[i], NULL);
        if (a != 0) {
            printf("Error joining worker thread\n");
            // Cleanups
            clear();
            free(prepared_by_cook);
            free(cook_ids);
            free(delivery_ids);
            free(delivery_queue);
            free(delivery_order_count);
            free(cook_order_count);
            close(logFile);
            return 1;
        }
    }

    // Cleanups
    clear();
    free(prepared_by_cook);
    free(cook_ids);
    free(delivery_ids);
    free(delivery_queue);
    free(delivery_order_count);
    free(cook_order_count);
    close(logFile);
    return 0;
}

void *manager(void* arg) {
    if (listen(server_socket, 3) < 0) { // Listen for connections
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("PideShop active waiting for connection\n");
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    while ((client_sock = accept(server_socket, (struct sockaddr*)&address, &addrlen)) >= 0) {
        handle_client(); // Handle the client
        client_sock = 0; // Reset the client socket
        if(sigInt == 1) { // Check if SIGINT is received
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            pthread_exit(0);
        }
    }
    if (client_sock < 0) { // Check if the client socket is valid
        if(sigInt == 1) { // Check if SIGINT is received
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            pthread_exit(0);
        }
        perror("accept");
        exit(EXIT_FAILURE);
    }
    pthread_exit(0);
}
void *cook(void* arg) {
    int cook_id = *(int*)arg;
    int cook_index = cook_id - 1;
    while(sigInt == 0) { // Check if SIGINT is received
        cook_handler(cook_id, cook_index); // Handle the cook
    }
    pthread_exit(0);
}
void cook_handler(int cook_id, int cook_index) {
    client_server_message clientMsg;
    int oven_in_use = 0; // Check if the oven is in use
    int orderId1, orderId2; // Order ids
    int inPreparation = 0; // Check if the cook is preparing a meal
    double waitTime1;
        while (1) {
            if(sigInt == 1) { // Check if SIGINT is received
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                break;
            }
            // Wait for customers
            pthread_mutex_lock(&cookMutex);
            while (customers_to_serve == 0) {
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    pthread_mutex_unlock(&cookMutex);
                    break;
                }
                pthread_cond_wait(&condCook, &cookMutex);
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    pthread_mutex_unlock(&cookMutex);
                    break;
                }
            }
            if(sigInt == 1) {
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                pthread_mutex_unlock(&cookMutex);
                break;
            }
            customers_to_serve--; // Decrease the count of customers to serve
            orderId1 = ++orderIdIndex; // Set oder id
            inPreparation = 1; // Set in preparation
            waitTime1 = calculate_time();
            memset(&clientMsg, 0, sizeof(client_server_message));
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Order %d is preparing.\n", orderId1);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about cook is preparing
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    pthread_mutex_unlock(&cookMutex);
                    break;
                }
                perror("Failed to send message");
            }
            pthread_mutex_unlock(&cookMutex); // Unlock the mutex

            if(sigInt == 1) { // Check if SIGINT is received
                pthread_cond_signal(&condManager); // Signal the manager
                pthread_cond_signal(&clientCond); // Signal the client
                break;
            }
            usleep(waitTime1 * 1000 * 1000); // Simulating the preparation time
            if(sigInt == 1) {
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                break;
            }
            pthread_mutex_lock(&cookMutex);
            memset(&clientMsg, 0, sizeof(client_server_message));
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Order %d prepared. Will be placing in the oven\n", orderId1);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about cook preperad the meal
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    pthread_mutex_unlock(&cookMutex);
                    break;
                }
                perror("Failed to send message");
            }
            pthread_mutex_unlock(&cookMutex);

            // Place the meal in the oven
            while (1) {
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    break;
                }
                // Wait for an available shovel
                sem_wait(&shovels);

                // Try to wait for space in the oven
                if (sem_trywait(&oven_space) == 0) {
                    // Successfully reserved space in the oven
                    // Wait for an available door
                    sem_wait(&door_access);
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        sem_post(&shovels);
                        sem_post(&oven_space);
                        sem_post(&door_access);
                        break;
                    }
                    inPreparation = 0;
                    // Release the door and the shovel
                    sem_post(&door_access);
                    sem_post(&shovels);
                    oven_in_use = 1;
                    break;
                } else {
                    // Oven is full, release the shovel
                    sem_post(&shovels);
                    usleep(100000);  // Wait for some time before trying again
                }
            }
            if(sigInt == 1) {
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                break;
            }

            // Loop to continuously prepare and cook meals
            while (1) {
                // Check if the oven is in use
                if (oven_in_use) {
                    // Check if there are more customers to serve
                    pthread_mutex_lock(&cookMutex);
                    if (customers_to_serve == 0) {
                        pthread_mutex_unlock(&cookMutex);
                    } else {
                        customers_to_serve--; // Decrease the count of customers to serve
                        orderId2 = ++orderIdIndex;
                        memset(&clientMsg, 0, sizeof(client_server_message));
                        snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Order %d is preparing.\n", orderId2);
                        if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about cook is preparing
                            if(sigInt == 1) {
                                pthread_cond_signal(&condManager);
                                pthread_cond_signal(&clientCond);
                                pthread_mutex_unlock(&cookMutex);
                                break;
                            }
                            perror("Failed to send message");
                        }
                        waitTime1 = calculate_time();
                        pthread_mutex_unlock(&cookMutex);
                        inPreparation = 1;
                        // Prepare the next meal
                    }
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        break;
                    }
                    // Wait for the meal to cook
                    usleep((waitTime1 * 1000 * 1000)/2);
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        break;
                    }
                    // Wait for an available shovel to take out the meal
                    sem_wait(&shovels);

                    // Wait for an available door to take out the meal
                    sem_wait(&door_access);

                    // Take the meal out of the oven
                    sem_post(&oven_space);
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        sem_post(&shovels);
                        sem_post(&door_access);
                        break;
                    }
                    // Release the door and the shovel
                    sem_post(&door_access);
                    sem_post(&shovels);
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        break;
                    }
                        // Signal the manager if needed
                        pthread_mutex_lock(&mealMutex);
                        memset(&clientMsg, 0, sizeof(client_server_message));
                        snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Order %d cooked. Will be delivered soon\n", orderId1);
                        if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about the meal is cooked
                            if(sigInt == 1) {
                                pthread_cond_signal(&condManager);
                                pthread_cond_signal(&clientCond);
                                pthread_mutex_unlock(&mealMutex);
                                break;
                            }
                            perror("Failed to send message");
                        }
                        char log_msg[100];
                        sprintf(log_msg, "Cook %d cooked order %d\n", cook_id, orderId1);
                        writeLogMessage(log_msg); // Write the log message
                        prepared_by_cook[prepared_meals].orderId = orderId1; // Get the order id
                        orderId1 = orderId2;
                        prepared_by_cook[prepared_meals++].cookId = cook_id; // Get cook id
                        if(sigInt == 1) {
                            pthread_cond_signal(&condManager);
                            pthread_cond_signal(&clientCond);
                            break;
                        }
                        if (prepared_meals % 3 == 0 || (customers_to_serve == 0 && prepared_meals == total_customers % 3)) { // Check if the 3 meals or last meals are ready for delivery
                            pthread_cond_signal(&condManager);
                        }
                        cook_order_count[cook_index]++; // Increase the count of orders cooked by the cook
                        pthread_mutex_unlock(&mealMutex);

                        oven_in_use = 0; // Meal is out of the oven
                }

                if (inPreparation == 0) {
                    break;
                }
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    break;
                }
                usleep((waitTime1 * 1000 * 1000)/2);  // Simulating the preparation time for the next meal
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    break;
                }
                pthread_mutex_lock(&cookMutex);
                memset(&clientMsg, 0, sizeof(client_server_message));
                snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Order %d prepared. Will be placing in the oven\n", orderId2);
                if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about the meal is prepared
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        pthread_mutex_unlock(&cookMutex);
                        break;
                    }
                    perror("Failed to send message");
                }
                pthread_mutex_unlock(&cookMutex);

                // Place the next meal in the oven
                while (1) {
                    if(sigInt == 1) {
                        pthread_cond_signal(&condManager);
                        pthread_cond_signal(&clientCond);
                        break;
                    }
                    // Wait for an available shovel
                    sem_wait(&shovels);

                    // Try to wait for space in the oven
                    if (sem_trywait(&oven_space) == 0) {
                        // Successfully reserved space in the oven
                        // Wait for an available door
                        sem_wait(&door_access);
                        if(sigInt == 1) {
                            pthread_cond_signal(&condManager);
                            pthread_cond_signal(&clientCond);
                            sem_post(&shovels);
                            sem_post(&oven_space);
                            sem_post(&door_access);
                            break;
                        }
                        // Release the door and the shovel
                        sem_post(&door_access);
                        sem_post(&shovels);
                        inPreparation = 0;
                        oven_in_use = 1;
                        break;
                    } else {
                        // Oven is full, release the shovel
                        sem_post(&shovels);
                        usleep(100000);  // Wait for some time before trying again
                    }
                }
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    break;
                }
            }
        }
}
void *deliveryPersonel(void* arg) {
    int delivery_id = *(int*)arg;
    int delivery_index = delivery_id - 1;
    while(sigInt == 0) {
        delivery_handler(delivery_id, delivery_index);
    }
    pthread_exit(0);
}
void delivery_handler(int delivery_id, int delivery_index) {
    client_server_message clientMsg;
    while (1) {
        pthread_mutex_lock(&deliveryMutex); // Lock the mutex
        while (is_queue_empty(delivery_queue) && deliveryPackage == 0) { // Check if the delivery queue is empty
            pthread_cond_wait(&deliveryCond, &deliveryMutex);
            if(sigInt == 1) { // Check if SIGINT is received
                pthread_cond_signal(&condManager); // Signal the manager
                pthread_cond_signal(&clientCond); // Signal the client
                pthread_mutex_unlock(&deliveryMutex); // Unlock the mutex
                break;
            }
        }
        if(sigInt == 1) {
            pthread_cond_signal(&condManager);
            pthread_cond_signal(&clientCond);
            pthread_mutex_unlock(&deliveryMutex);
            break;
        }
        order deliveryOrders[3];
        int totalOrders = 0; // Total orders
        int currentP = deliveryOrders[0].p/2;
        int currentQ = deliveryOrders[0].q/2;
        // Get the orders from the delivery queue
        while (!is_queue_empty(delivery_queue) && totalOrders < 3) {
            if(sigInt == 1) {
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                pthread_mutex_unlock(&deliveryMutex);
                break;
            }
            deliveryOrders[totalOrders] = dequeue(delivery_queue);
            generateRandomPQ(&deliveryOrders[totalOrders].p, &deliveryOrders[totalOrders].q, deliveryOrders[totalOrders].p, deliveryOrders[totalOrders].q); // Generate random p and q values
            int pM = fabs(deliveryOrders[totalOrders].p - currentP);
            int qM = fabs(deliveryOrders[totalOrders].q - currentQ);
            deliveryOrders[totalOrders].totalTime = (pM + qM) / deliverySpeed; // Calculate the total time
            currentP = deliveryOrders[totalOrders].p;
            currentQ = deliveryOrders[totalOrders].q;
            totalOrders++; // Increase the total orders
        }

        deliveryPackage = 0; // Reset the delivery package
        if(sigInt == 1) { // Check if SIGINT is received
            pthread_cond_signal(&condManager);
            pthread_cond_signal(&clientCond);
            pthread_mutex_unlock(&deliveryMutex);
            break;
        }
        memset(&clientMsg, 0, sizeof(client_server_message));
        if(totalOrders == 3) {
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Meal %d-%d-%d is delivering by %d\n", deliveryOrders[0].order_id, deliveryOrders[1].order_id, deliveryOrders[2].order_id, delivery_id);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) {
                perror("Failed to send message");
            }
        } else if(totalOrders == 2) {
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Meal %d-%d is delivering by %d\n", deliveryOrders[0].order_id, deliveryOrders[1].order_id, delivery_id);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) {
                perror("Failed to send message");
            }
        } else if(totalOrders == 1) {
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Meal %d is delivering by %d\n", deliveryOrders[0].order_id, delivery_id);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) {
                perror("Failed to send message");
            }
        }
        pthread_mutex_unlock(&deliveryMutex); // Unlock the mutex
        if(sigInt == 1) {
            pthread_cond_signal(&condManager);
            pthread_cond_signal(&clientCond);
            break;
        }
        // Deliver the orders
        for (int i = 0; i < totalOrders; i++) {
            sleep(deliveryOrders[i].totalTime);
            if(sigInt == 1) {
                pthread_cond_signal(&condManager);
                pthread_cond_signal(&clientCond);
                break;
            }
            char msg[128];
            sprintf(msg, "Delivery %d delivered order %d to %d-%d\n", delivery_id, deliveryOrders[i].order_id, deliveryOrders[i].p, deliveryOrders[i].q);
            writeLogMessage(msg); // Write the log message

            pthread_mutex_lock(&deliveryMutex);
            memset(&clientMsg, 0, sizeof(client_server_message));
            snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Meal %d is delivered by %d to %d-%d\n", deliveryOrders[i].order_id, delivery_id, deliveryOrders[i].p, deliveryOrders[i].q);
            if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about the meal is delivered
                if(sigInt == 1) {
                    pthread_cond_signal(&condManager);
                    pthread_cond_signal(&clientCond);
                    pthread_mutex_unlock(&deliveryMutex);
                    break;
                }
                perror("Failed to send message");
            }
            pthread_mutex_unlock(&deliveryMutex);
            delivery_order_count[delivery_index]++; // Increase the count of orders delivered by the delivery personel
        }
        if(sigInt == 1) {
            pthread_cond_signal(&condManager);
            pthread_cond_signal(&clientCond);
            break;
        }
        // Notify the client thread that delivery is done
        pthread_mutex_lock(&deliveryMutex);
        delivery_count -= totalOrders;
        if (delivery_count == 0) {
            pthread_cond_signal(&clientCond);
        }
        pthread_mutex_unlock(&deliveryMutex);
    }
}
void handle_client() {
    client_information info;
    client_server_message clientMsg;
    memset(cook_order_count, 0, cookPoolSize * sizeof(int));
    memset(delivery_order_count, 0, deliveryPoolSize * sizeof(int));
    if (read(client_sock, &info, sizeof(client_information)) <= 0) { // Read the client information
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            return;
        }
        if(client_sock > 0) {
            close(client_sock);
        }
        perror("Failed to read from client");
        return;
    }
    printf("%d new customers from %d.. Serving\n", info.numberOfCustomers, info.pid);
    int sPid = getpid(); // Get the process id
    write(client_sock, &sPid, sizeof(int)); // Write the process id to the client
    char msg[128];
    snprintf(msg, sizeof(msg), "%d new customers from %d.. Serving\n", info.numberOfCustomers, info.pid);
    writeLogMessage(msg); // Write the log message
    clientPid = info.pid; // Set the client process id
    memset(&clientMsg, 0, sizeof(client_server_message));
    snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Orders received. Meals are preparing\n"); 
    if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) { // Write the message to the client about the meals are preparing
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            return;
        }
        perror("Failed to send message");
    }
    if(sigInt == 1) {
        pthread_cond_broadcast(&condCook);
        pthread_cond_broadcast(&deliveryCond);
        return;
    }
    delivery_count = info.numberOfCustomers; // Set the delivery count
    // Set default 3 orders
    order deliveryOrders[3]; 
    for (int i = 0; i < 3; i++) {
        deliveryOrders[i].pid = info.pid;
        deliveryOrders[i].cookId = 0;
        deliveryOrders[i].p = info.p;
        deliveryOrders[i].q = info.q;
    }
    if(sigInt == 1) {
        pthread_cond_broadcast(&condCook);
        pthread_cond_broadcast(&deliveryCond);
        return;
    }

    pthread_mutex_lock(&cookMutex);
    customers_to_serve = info.numberOfCustomers; // Set the customers to serve
    total_customers = info.numberOfCustomers; // Set the total customers
    pthread_cond_broadcast(&condCook);
    pthread_mutex_unlock(&cookMutex);
    if(sigInt == 1) {
        pthread_cond_broadcast(&condCook);
        pthread_cond_broadcast(&deliveryCond);
        return;
    }
    // Loop until all customers are served
    while (total_customers > 0) {
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            return;
        }
        pthread_mutex_lock(&mealMutex);
        // Wait for the meals to be prepared
        while (prepared_meals < 3 && total_customers > 3) {
            pthread_cond_wait(&condManager, &mealMutex);
            if(sigInt == 1) {
                pthread_cond_broadcast(&condCook);
                pthread_cond_broadcast(&deliveryCond);
                return;
            }
        }
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            pthread_mutex_unlock(&mealMutex);
            return;
        }
        // Check if the prepared meals are 3 or less(last meals)
        if (prepared_meals >= 3) {
            pthread_mutex_lock(&deliveryMutex);
            deliveryOrders[0].cookId = prepared_by_cook[0].cookId;
            deliveryOrders[0].order_id = prepared_by_cook[0].orderId;
            deliveryOrders[1].cookId = prepared_by_cook[1].cookId;
            deliveryOrders[1].order_id = prepared_by_cook[1].orderId;
            deliveryOrders[2].cookId = prepared_by_cook[2].cookId;
            deliveryOrders[2].order_id = prepared_by_cook[2].orderId;
            deliveryPackage = 3; // Set the delivery package
            if(sigInt == 1) {
                pthread_cond_broadcast(&condCook);
                pthread_cond_broadcast(&deliveryCond);
                pthread_mutex_unlock(&deliveryMutex);
                pthread_mutex_unlock(&mealMutex);
                return;
            }
            // Enqueue the delivery orders
            enqueue(delivery_queue, deliveryOrders[0]);
            enqueue(delivery_queue, deliveryOrders[1]);
            enqueue(delivery_queue, deliveryOrders[2]);
            total_customers -= 3; // Decrease the total customers
            prepared_meals -= 3; // Decrease the prepared meals
            pthread_cond_signal(&deliveryCond); // Signal the delivery condition
            pthread_mutex_unlock(&deliveryMutex);
        } else if (total_customers <= 3 && prepared_meals == total_customers) {
            // Wait for the last remaining meals to be prepared
            pthread_mutex_lock(&deliveryMutex);
            deliveryPackage = prepared_meals; // Set the delivery package
            for (int i = 0; i < prepared_meals; i++) {
                if(sigInt == 1) {
                    pthread_cond_broadcast(&condCook);
                    pthread_cond_broadcast(&deliveryCond);
                    pthread_mutex_unlock(&deliveryMutex);
                    pthread_mutex_unlock(&mealMutex);
                    return;
                }
                // Enqueue the delivery orders
                deliveryOrders[i].cookId = prepared_by_cook[i].cookId;
                deliveryOrders[i].order_id = prepared_by_cook[i].orderId;
                enqueue(delivery_queue, deliveryOrders[i]);
            }
            total_customers -= prepared_meals; // Decrease the total customers
            prepared_meals = 0; // Reset the prepared meals
            pthread_cond_signal(&deliveryCond); // Signal the delivery condition
            pthread_mutex_unlock(&deliveryMutex);
        }
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            pthread_mutex_unlock(&mealMutex);
            return;
        }
        pthread_mutex_unlock(&mealMutex);
    }

    if(sigInt == 1) {
        pthread_cond_broadcast(&condCook);
        pthread_cond_broadcast(&deliveryCond);
        return;
    }
    // Wait for delivery threads to finish
    pthread_mutex_lock(&deliveryMutex);
    while (delivery_count > 0) {
        pthread_cond_wait(&clientCond, &deliveryMutex);
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            return;
        }
    }
    pthread_mutex_unlock(&deliveryMutex);
    if(sigInt == 1) {
        pthread_cond_broadcast(&condCook);
        pthread_cond_broadcast(&deliveryCond);
        return;
    }
    printf("> done serving client @ XXX PID %d\n", info.pid);
    memset(&clientMsg, 0, sizeof(client_server_message));
    snprintf(clientMsg.msg, sizeof(clientMsg.msg), "Done serving meals for customers\n");
    if (write(client_sock, &clientMsg, sizeof(client_server_message)) < 0) {
        if(sigInt == 1) {
            pthread_cond_broadcast(&condCook);
            pthread_cond_broadcast(&deliveryCond);
            return;
        }
        perror("Failed to send message");
    }
    if(client_sock > 0) {
        close(client_sock);
    }
    int maxCook = findMaxIndex(cook_order_count, cookPoolSize);
    int maxDelivery = findMaxIndex(delivery_order_count, deliveryPoolSize);
    printf("> Thanks Cook %d and Moto %d\n", maxCook+1, maxDelivery+1);
}

void generateRandomPQ(int *p, int *q, int maxP, int maxQ) {
    do {
        // Generate random integers x and y between 0 and 20
        *p = rand() % (maxP + 1); // 0 to 20
        *q = rand() % (maxQ + 1); // 0 to 20
    } while (*p == maxP/2 && *q == maxQ/2); // Continue looping if the point is (10,10)
}
int min(int a, int b) {
    return (a < b) ? a : b;
}

/* Function to write message to log file */
void writeLogMessage(char* startMessage)
{
    char errorMessage[400];
    // Edit log message with adding time
    createMessageWithTime(startMessage, errorMessage);
    // Write to log file
    while((write(logFile, errorMessage, strlen(errorMessage))==-1) && (errno==EINTR));

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

void clear() {
    pthread_mutex_destroy(&deliveryMutex);
    pthread_cond_destroy(&deliveryCond);
    pthread_mutex_destroy(&cookMutex);
    pthread_mutex_destroy(&mealMutex);
    pthread_cond_destroy(&condCook);
    pthread_cond_destroy(&condManager);
    pthread_mutex_destroy(&logMutex);
    pthread_cond_destroy(&clientCond);
    sem_destroy(&oven_space);
    sem_destroy(&door_access);
    sem_destroy(&shovels);
}

int findMaxIndex(int *arr, int size) {
    // Initialize max_index to 0 (first element)
    int max_index = 0;
    
    // Iterate through the array starting from the second element
    for (int i = 1; i < size; i++) {
        if (arr[i] > arr[max_index]) {
            max_index = i;
        }
    }
    return max_index;
}
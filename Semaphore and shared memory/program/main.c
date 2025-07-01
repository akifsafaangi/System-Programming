#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define MAX_AUTOMOBILE 8
#define MAX_PICKUP 4
#define MAX_VEHICLES 30

// Semaphores
sem_t newPickup, newAutomobile; // To notify attendants that a new vehicle is parked in free parking
sem_t inChargeforPickup, inChargeforAutomobile; // Waits for the valet to park the vehicle from free spot to permament spot
sem_t controlAutomobileParking, controlPickupParking; // It prevents race condition while moving car from free parking to regular parking
sem_t parkEntrance; // To prevent race condition while waiting for entrance to free parking

// Counters for free park
volatile int mFree_automobile = MAX_AUTOMOBILE;
volatile int parkedAutomobile = MAX_AUTOMOBILE;
volatile int waitingQueueAutomobile = MAX_AUTOMOBILE;
volatile int mFree_pickup = MAX_PICKUP;
volatile int parkedPickup = MAX_PICKUP;
volatile int waitingQueuePickup = MAX_PICKUP;

// To exit from carAttendant threads after vehicle limit is reached
int finishThreads = 0;
int finish = 0;

void* carOwner(void* arg) {
    int vehicleType = rand() % 2; // 0 for automobile, 1 for pickup
    // int vehicleType = 0;
    if (vehicleType == 0) { // Automobile
        sem_wait(&parkEntrance); 
        sem_wait(&controlAutomobileParking);
        if (mFree_automobile > 0) {
            mFree_automobile--;
            printf("Automobile parked in free parking. Remaining spots in free parking: %d. Remaining Spots in regular parking: %d\n", mFree_automobile, parkedAutomobile);
            sem_post(&newAutomobile);
            sem_post(&parkEntrance);
            if(waitingQueueAutomobile == 0) {
                sem_post(&controlAutomobileParking);
                int randTime = rand() % 3;
                sleep(randTime);
                sem_wait(&controlAutomobileParking);
                mFree_automobile++;
                printf("Regular parking is still full. Automobile is leaving from free parking. Remaining spots in free parking %d\n", mFree_automobile);
                sem_post(&controlAutomobileParking);
            } else {
                waitingQueueAutomobile--;
                sem_post(&controlAutomobileParking);
                sem_wait(&inChargeforAutomobile);
            }
        } else {
            sem_post(&controlAutomobileParking);
            sem_post(&parkEntrance);
            printf("No space for another automobile in free parking.\n");
        }
    } else {
        sem_wait(&parkEntrance);
        sem_wait(&controlPickupParking);
        if (mFree_pickup > 0) {
            mFree_pickup--;
            printf("Pickup parked in free parking. Remaining spots in free parking: %d. Remaining Spots in regular parking: %d\n", mFree_pickup, parkedPickup);
            sem_post(&newPickup);
            sem_post(&parkEntrance);
            if(waitingQueuePickup == 0) {
                sem_post(&controlPickupParking);
                int randTime = rand() % 3;
                sleep(randTime);
                sem_wait(&controlPickupParking);
                mFree_pickup++;
                printf("Regular parking is still full. Pickup is leaving from free parking. Remaining spots in free parking. %d\n", mFree_pickup);
                sem_post(&controlPickupParking);
            } else {
                waitingQueuePickup--;
                sem_post(&controlPickupParking);
                sem_wait(&inChargeforPickup);
            }
        } else {
            sem_post(&controlPickupParking);
            sem_post(&parkEntrance);
            printf("No space for another pickup in free parking.\n");
        }
    }
    finishThreads++;
    if(finishThreads == MAX_VEHICLES - 2) {
        finish = 1;
        sem_post(&newAutomobile);
        sem_post(&controlAutomobileParking);
        sem_post(&newPickup);
        sem_post(&controlPickupParking);
    }
    pthread_exit(NULL);
}

void* carAttendant(void* arg) {
    intptr_t argInt = (intptr_t)arg;
    if(argInt == 0) {
        while(1) {
            sem_wait(&newAutomobile);
            sem_wait(&controlAutomobileParking);
            if(parkedAutomobile == 0 || finish == 1) {
                sem_post(&inChargeforAutomobile);
                sem_post(&controlAutomobileParking);
                break;
            }
            mFree_automobile++;
            parkedAutomobile--;
            printf("Automobile parked in regular parking. Remaining spots in free parking: %d. Remaining Spots in regular parking: %d\n", mFree_automobile, parkedAutomobile);
            sem_post(&inChargeforAutomobile);
            sem_post(&controlAutomobileParking);
        }
    } else {
        while(1) {
            sem_wait(&newPickup);
            sem_wait(&controlPickupParking);
            if(parkedPickup == 0 || finish == 1) {
                sem_post(&inChargeforPickup);
                sem_post(&controlPickupParking);
                break;
            }
            mFree_pickup++;
            parkedPickup--;
            printf("Pickup parked in regular parking. Remaining spots in free parking: %d. Remaining Spots in regular parking: %d\n", mFree_pickup, parkedPickup);
            sem_post(&inChargeforPickup);
            sem_post(&controlPickupParking);
        }
    }
    pthread_exit(NULL);
}

int main() {
    srand(time(NULL)); // Seed random number generator
    int s;
    // Initialize semaphores
    if (sem_init(&newAutomobile, 0, 0) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&newPickup, 0, 0) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&inChargeforAutomobile, 0, 0) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&inChargeforPickup, 0, 0) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&parkEntrance, 0, 1) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&controlAutomobileParking, 0, 1) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }
    if (sem_init(&controlPickupParking, 0, 1) == -1) {
        perror("Could not initialize semaphore");
        exit(1);
    }

    pthread_t threads[MAX_VEHICLES];
    s = pthread_create(&threads[0], NULL, carAttendant, (void *)0); // One thread for each attendant
    if(s != 0)
    {
        perror("Thread creation failed\n");
        exit(1);
    }
    s = pthread_create(&threads[1], NULL, carAttendant, (void *)1);
    if(s != 0)
    {
        perror("Thread creation failed\n");
        exit(1);
    }

    for (int i = 2; i < MAX_VEHICLES; i++) {
        s = pthread_create(&threads[i], NULL, carOwner, NULL); // Multiple car owners
        if(s != 0)
        {
            perror("Thread creation failed\n");
            exit(1);
        }
    }
    for (int i = 0; i < MAX_VEHICLES; i++) {
        s = pthread_join(threads[i], NULL);
        if(s != 0)
        {
            perror("Thread join failed\n");
            exit(1);
        }
    }
    // Cleanup
    sem_destroy(&newAutomobile);
    sem_destroy(&newPickup);
    sem_destroy(&inChargeforAutomobile);
    sem_destroy(&inChargeforPickup);
    sem_destroy(&parkEntrance);
    sem_destroy(&controlAutomobileParking);
    sem_destroy(&controlPickupParking);
    return 0;
}
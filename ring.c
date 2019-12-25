#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <mpi.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

//THREAD_TYPES
#define CS_TYPE 0
#define DT_TYPE 1

//TIME
#define TIMEOUT_TIME 1000
#define CS_TIME 500

//TOKEN AND PROCESSES
#define WITH_TOKEN 1
#define WITHOUT_TOKEN 0
#define PROCESS_ZERO 0
#define NOBODY_SEES_TOKEN -1

//TAG
#define TAG 22
pthread_mutex_t confirmationReceivedMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t logsMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lamportMtx = PTHREAD_MUTEX_INITIALIZER;

//SORTING HELPERS
#define NUMBER_OF_LOGS 1000
#define MAX_STRING_SIZE 100

typedef int bool;
enum { false, true };

char logs[NUMBER_OF_LOGS][MAX_STRING_SIZE];
int logID = 0;
bool finished = false;
int N;
int currentProcID;
int nextProcID;
bool confirmationReceived = false;
bool hasToken = false;
int currentTokenID = 0;
int lamportClock = 0;
MPI_Datatype mpi_data;

typedef struct msg_s {
    int initiator;
    int seen_token;
    int token_id;
    int with_token;
    int lamport;
} msg;

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

int max(int x, int y) {
    return x ^ ((x ^ y) & -(x < y));  
} 

void sendMsg(int initiator, int seen_token, int token_id, int with_token) {
    msg send;
    send.initiator = initiator;
    send.seen_token = seen_token;
    send.token_id = token_id;
    send.with_token = with_token;
    pthread_mutex_lock(&lamportMtx);
    lamportClock++;
    send.lamport = lamportClock;
    MPI_Send(&send, 1, mpi_data, nextProcID, TAG, MPI_COMM_WORLD);
    pthread_mutex_unlock(&lamportMtx);
}

void log(char* text, int lamport, int val1, int val2) {
    printf(text, lamport, val1, val2);
    pthread_mutex_lock(&logsMtx);
    sprintf(logs[logID], text, lamport, val1, val2);
    logID++;
    pthread_mutex_unlock(&logsMtx);
}

void *CriticalSectionOperations()
{
    log("[L: %d][ID: %d] Entering CS.\n", lamportClock, currentProcID, 0);
    msleep(CS_TIME);
    printf("[L: %d][ID: %d] Leaving CS.\n", lamportClock, currentProcID);
    hasToken = false;
    sendMsg(currentProcID, NOBODY_SEES_TOKEN, currentTokenID, WITH_TOKEN);
    printf("[L: %d][ID: %d] Send msg WITH_TOKEN to process %d.\n", lamportClock, currentProcID, nextProcID);
    pthread_exit(NULL);
}

void *DetectionTimeout()
{
    bool again = true;
    printf("[L: %d][ID: %d] DT activated.\n", lamportClock, currentProcID);
    while(again && !finished){
        msleep(TIMEOUT_TIME);
        pthread_mutex_lock(&confirmationReceivedMtx);
        if (confirmationReceived) {
            again = false;
        } else {
            sendMsg(currentProcID, NOBODY_SEES_TOKEN, currentTokenID, WITHOUT_TOKEN);
            printf("[L: %d][ID: %d] Confirmation not yet received, checking message sent again to process %d.\n", lamportClock, currentProcID, nextProcID);
        }
        pthread_mutex_unlock(&confirmationReceivedMtx);
    }
    if (!finished) {
        printf("[L: %d][ID: %d] Confirmation received, closing DT.\n", lamportClock, currentProcID);
    }
    pthread_exit(NULL);
}

void createThread(int type) {
    int create_result = 0;

    pthread_t thread1;

    if (type == CS_TYPE) {
        create_result = pthread_create(&thread1, NULL, CriticalSectionOperations, NULL);
    } else if (type == DT_TYPE) {
        create_result = pthread_create(&thread1, NULL, DetectionTimeout, NULL);
    }
    if (create_result){
       printf("Error when creating new thread. Error code: %d\n", create_result);
       exit(-1);
    }

}

int main (int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    MPI_Comm_size( MPI_COMM_WORLD, &N );
    MPI_Comm_rank( MPI_COMM_WORLD, &currentProcID );
    nextProcID = (currentProcID + 1) % N;

	const int nitems = 5;
	int blocklengths[5] = {1, 1, 1, 1, 1};
	MPI_Datatype types[5] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	MPI_Aint offsets[5];

	offsets[0] = offsetof(msg, initiator);
	offsets[1] = offsetof(msg, seen_token);
	offsets[2] = offsetof(msg, token_id);
	offsets[3] = offsetof(msg, with_token);
    offsets[4] = offsetof(msg, lamport);

    MPI_Type_create_struct(nitems, blocklengths, offsets, types, &mpi_data);
	MPI_Type_commit(&mpi_data);
    msg recv;

    //Init
    if (currentProcID == PROCESS_ZERO) {
        printf("[L: %d][ID: %d] INIT.\n", lamportClock, currentProcID);
        hasToken = true;
        createThread(CS_TYPE);
        createThread(DT_TYPE);
    }
    while(!finished){
        MPI_Recv(&recv, 1, mpi_data, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        pthread_mutex_lock(&lamportMtx);
        lamportClock = max(recv.lamport, lamportClock) + 1;
        pthread_mutex_unlock(&lamportMtx);

        if (recv.with_token == WITH_TOKEN) {
            printf("[L: %d][ID: %d] Receive WITH_TOKEN message.\n", lamportClock, currentProcID);
            pthread_mutex_lock(&confirmationReceivedMtx);
            confirmationReceived = true;
            pthread_mutex_unlock(&confirmationReceivedMtx);
            hasToken = true;
            if (currentProcID == PROCESS_ZERO) {
                currentTokenID = (currentTokenID + 1) % N;
            } else {
                currentTokenID = recv.token_id;
            }
            sendMsg(recv.initiator, currentProcID, currentTokenID, WITHOUT_TOKEN);
            createThread(CS_TYPE);
            pthread_mutex_lock(&confirmationReceivedMtx);
            confirmationReceived = false;
            pthread_mutex_unlock(&confirmationReceivedMtx);
            createThread(DT_TYPE);
        } else if (recv.initiator == currentProcID && recv.seen_token != NOBODY_SEES_TOKEN) {
            printf("[L: %d][ID: %d] Receive OUR checking message (token was seen). Token was received by next process. :)\n", lamportClock, currentProcID);
            pthread_mutex_lock(&confirmationReceivedMtx);
            confirmationReceived = true;
            pthread_mutex_unlock(&confirmationReceivedMtx);
        } else if (recv.initiator == currentProcID && recv.seen_token == NOBODY_SEES_TOKEN && currentTokenID == recv.token_id && !hasToken) {
            printf("[L: %d][ID: %d] Receive OUR checking message (token wasn't seen). Token WASN'T received by next process. :( Resending TOKEN.\n", lamportClock, currentProcID);
            sendMsg(currentProcID, NOBODY_SEES_TOKEN, currentTokenID, WITH_TOKEN);
        } else if (recv.seen_token == NOBODY_SEES_TOKEN) {
            if (hasToken) {
                printf("[L: %d][ID: %d] Receive SOMEONE'S (%d) checking message (token wasn't seen). We are in CS.\n", lamportClock, currentProcID, recv.initiator);
                sendMsg(recv.initiator, currentProcID, recv.token_id, WITHOUT_TOKEN);
            } else {
                printf("[L: %d][ID: %d] Receive SOMEONE'S (%d) checking message (token wasn't seen). We are NOT in CS.\n", lamportClock, currentProcID, recv.initiator);
                sendMsg(recv.initiator, recv.seen_token, recv.token_id, WITHOUT_TOKEN);
            }
        } else if (recv.seen_token != NOBODY_SEES_TOKEN) {
            printf("[L: %d][ID: %d] Receive SOMEONE'S (%d) checking message (token was seen). Just retransmit.\n", lamportClock, currentProcID, recv.initiator);
            sendMsg(recv.initiator, recv.seen_token, recv.token_id, false);
        }
    }


    
    MPI_Barrier(MPI_COMM_WORLD);
    if (currentProcID == PROCESS_ZERO) {
        printf("=================================================================================\n");
        printf("PRINTING SORTED LOGS:\n");
        for (int idx = 0; idx < logID; idx++) {
            printf("%s\n", logs[idx]);
        }
    }

	MPI_Type_free(&mpi_data);
    MPI_Finalize();
    return 0;
}

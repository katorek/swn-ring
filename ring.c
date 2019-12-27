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
#define TIMEOUT_TIME 150
#define CS_TIME 300

//TOKEN AND PROCESSES
#define WITH_TOKEN 1
#define WITHOUT_TOKEN 0
#define PROCESS_ZERO 0
#define TTL -100
#define NOBODY_SEES_TOKEN -1
#define LAMPORT_VALUE_TO_FINISH 600 //set INT_MAX for infinite

//set percentage og succesfull msg delivery
#define MSG_DELIVERY_CHANCE_PCT 70 
#define SURE_SENDING 0
#define UNSURE_SENDING 1

//TAG
#define TAG 22
pthread_mutex_t confirmationReceivedMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t logsMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lamportMtx = PTHREAD_MUTEX_INITIALIZER;

//SORTING HELPERS
#define NUMBER_OF_LOGS 1000
#define MAX_STRING_SIZE 150

typedef int bool;
enum { false, true };

char logs[NUMBER_OF_LOGS][MAX_STRING_SIZE];
int logID = 0;
int N;
int currentProcID;
int nextProcID;
bool confirmationReceived = true;
bool hasToken = false;
int lamportClock = 0;
int lastLamport = -1;
MPI_Datatype mpi_data;

typedef struct msg_s {
    int initiator;
    int with_token;
    int lamport;
    int last_id;
    int last_lamport;
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

void log(char* text, int lamport, int val1, int val2) {
    printf(text, lamport, val1, val2);
    pthread_mutex_lock(&logsMtx);
    sprintf(logs[logID], text, lamport, val1, val2);
    logID++;
    pthread_mutex_unlock(&logsMtx);
}

void sendMsg(int initiator, int with_token, int last_id, int last_lamport, int mode) {
    msg send;
    send.initiator = initiator;
    send.with_token = with_token;
    send.last_id = last_id;
    send.last_lamport = last_lamport;
    pthread_mutex_lock(&lamportMtx);
    lamportClock++;
    send.lamport = lamportClock;
    if (mode == SURE_SENDING || (rand() % 100 + 1) <= MSG_DELIVERY_CHANCE_PCT) {
        MPI_Send(&send, 1, mpi_data, nextProcID, TAG, MPI_COMM_WORLD);
    } else {
        log("[L: %d][ID: %d] Message to %d was LOST.\n", lamportClock, currentProcID, nextProcID);
    }
    pthread_mutex_unlock(&lamportMtx);
}

void *CriticalSectionOperations()
{
    log("[L: %d][ID: %d] Entering CS.\n", lamportClock, currentProcID, 0);
    msleep(CS_TIME);
    log("[L: %d][ID: %d] Leaving CS.\n", lamportClock, currentProcID, 0);
    hasToken = false;
    sendMsg(currentProcID, WITH_TOKEN, currentProcID, lastLamport, UNSURE_SENDING);
    log("[L: %d][ID: %d] Send msg WITH_TOKEN to process %d.\n", lamportClock, currentProcID, nextProcID);
    pthread_mutex_lock(&confirmationReceivedMtx);
    confirmationReceived = false;
    pthread_mutex_unlock(&confirmationReceivedMtx);
    pthread_exit(NULL);
}

void *DetectionTimeout()
{
    while(lamportClock <= LAMPORT_VALUE_TO_FINISH){
        if (!confirmationReceived) {
            msleep(TIMEOUT_TIME);
            sendMsg(currentProcID, WITHOUT_TOKEN, currentProcID, lastLamport, UNSURE_SENDING);
            log("[L: %d][ID: %d] Confirmation not yet received, checking message sent again to process %d.\n", lamportClock, currentProcID, nextProcID);
        }
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

    srand(time(NULL) + currentProcID);

	const int nitems = 5;
	int blocklengths[5] = {1, 1, 1, 1, 1};
	MPI_Datatype types[5] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	MPI_Aint offsets[5];

	offsets[0] = offsetof(msg, initiator);
	offsets[1] = offsetof(msg, with_token);
    offsets[2] = offsetof(msg, lamport);
    offsets[3] = offsetof(msg, last_id);
    offsets[4] = offsetof(msg, last_lamport);

    MPI_Type_create_struct(nitems, blocklengths, offsets, types, &mpi_data);
	MPI_Type_commit(&mpi_data);
    msg recv;

    //Init
    if (currentProcID == PROCESS_ZERO) {
        log("[L: %d][ID: %d] INIT.\n", lamportClock, currentProcID, 0);
        hasToken = true;
        lastLamport = lamportClock;
        createThread(CS_TYPE);
    }
    createThread(DT_TYPE);

    while(lamportClock <= LAMPORT_VALUE_TO_FINISH){
        MPI_Recv(&recv, 1, mpi_data, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        pthread_mutex_lock(&lamportMtx);
        lamportClock = max(recv.lamport, lamportClock) + 1;
        pthread_mutex_unlock(&lamportMtx);

        if (recv.with_token == WITH_TOKEN) {
            log("[L: %d][ID: %d] Receive WITH_TOKEN message.\n", lamportClock, currentProcID, 0);
            pthread_mutex_lock(&confirmationReceivedMtx);
            confirmationReceived = true;
            pthread_mutex_unlock(&confirmationReceivedMtx);
            hasToken = true;
            lastLamport = lamportClock;
            sendMsg(recv.initiator, WITHOUT_TOKEN, currentProcID, lastLamport, UNSURE_SENDING);
            createThread(CS_TYPE);
        } else if (recv.initiator == currentProcID) {
            if (recv.last_id != currentProcID) {
                log("[L: %d][ID: %d] Receive OUR checking message (token was seen). Token was received by next process. :)\n", lamportClock, currentProcID, 0);
                pthread_mutex_lock(&confirmationReceivedMtx);
                confirmationReceived = true;
                pthread_mutex_unlock(&confirmationReceivedMtx);
            } else {
                log("[L: %d][ID: %d] Receive OUR checking message (token wasn't seen). Token WASN'T received by next process. :( Resending TOKEN.\n", lamportClock, currentProcID, 0);
                sendMsg(currentProcID, WITH_TOKEN, currentProcID, lastLamport, UNSURE_SENDING);
            }
        } else {
            if (recv.last_lamport <= lastLamport) {
                log("[L: %d][ID: %d] Receive SOMENONE'S (%d) checking message (token was seen).\n", lamportClock, currentProcID, recv.initiator);
                sendMsg(recv.initiator, WITHOUT_TOKEN, currentProcID, lastLamport, UNSURE_SENDING);
            } else {
                log("[L: %d][ID: %d] Receive SOMENONE'S (%d) checking message (token wasn't seen).\n", lamportClock, currentProcID, recv.initiator);
                sendMsg(recv.initiator, WITHOUT_TOKEN, recv.last_id, recv.last_lamport, UNSURE_SENDING);
                pthread_mutex_lock(&confirmationReceivedMtx);
                confirmationReceived = true;
                pthread_mutex_unlock(&confirmationReceivedMtx);
            }
        }
    }

    log("[L: %d][ID: %d] Finished. Waiting for all.\n", lamportClock, currentProcID, 0);
    sendMsg(currentProcID, WITHOUT_TOKEN, currentProcID, lastLamport, SURE_SENDING);
    MPI_Barrier(MPI_COMM_WORLD);

    if (currentProcID != PROCESS_ZERO) {
        while(true) {
            MPI_Recv(&recv, 1, mpi_data, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (recv.initiator == TTL) {
                break;
            }
        }
    } else {
        remove("logsTmp.txt");
    }
    FILE *oFile;
    oFile=fopen("logsTmp.txt", "a");
    for (int idx = 0; idx < logID; idx++) {
        fprintf(oFile, logs[idx]);
    }
    fclose(oFile);

    sendMsg(TTL, WITHOUT_TOKEN, currentProcID, lastLamport, SURE_SENDING);
    MPI_Barrier(MPI_COMM_WORLD);

    if (currentProcID == PROCESS_ZERO) {
        printf("=========================================================================\n ");
        printf("LOGS SORTED BY LAMPORT CLOCK.\n");
        FILE *cmd=popen("sort -nk2 logsTmp.txt", "r");
        char result[150];
        remove("logs.txt");
        FILE *oFile;
        oFile=fopen("logs.txt", "w");
        while (fgets(result, sizeof(result), cmd) !=NULL) {
            printf("%s", result);
            fprintf(oFile, result);
        }
        fclose(oFile);
        pclose(cmd);
        remove("logsTmp.txt");
    }

	MPI_Type_free(&mpi_data);
    MPI_Finalize();
    return 0;
}
//TODO ogarnąć jak ogarnąć działanie lamporta w nieskończonośc (coby nie wywaliło nam MAX_INT)
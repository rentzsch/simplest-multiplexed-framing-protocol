// SMFPClient.c semver:1.3.0
//   Copyright (c) 2015-2016 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/mit
//   https://github.com/rentzsch/simplest-multiplexed-framing-protocol

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include "SMFPClient.h"
#include "elemental.h"

typedef void* (*pthread_start_proc)(void*);

typedef enum {
    SMFPConnectionState_Closed,
    SMFPConnectionState_Opening,
    SMFPConnectionState_Open,
} SMFPConnectionState;

struct SMFPConnection {
    SMFPConnectionState  state;
    const char           *socketPath;
    int                  socketFD;
    uint32_t             transactionID;
    pthread_mutex_t      mutex;
    ElementList          outstandingTransactions;  // of SMFPTransaction
    pthread_t            responseReaderThread;
} SMFPConnection;

typedef struct {
    Element               element;
    SMFPConnectionRef     connection;
    uint32_t              transactionID;
    SMFPResponseReceiver  responseReceiver;
    void                  *responseReceiverContext;
    SMFPErr               responseReceiverErr;
    pthread_mutex_t       transactionCompleteMutex;
    pthread_cond_t        transactionCompleteCond;
} SMFPTransaction;

static void perrorf(const char * __restrict format, ...) {
    char message[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    perror(message);
}

#define trace_errno(MSG) perrorf(MSG " errno:%d " __FILE__ ":%d", errno, __LINE__)

static const char* CStringCopy(const char* cstr) {
    size_t len = strlen(cstr) + 1;
    char *result = malloc(len);
    strncpy(result, cstr, len);
    return result;
}

SMFPConnectionRef SMFPConnectionCreate(const char *socketPath, SMFPErr *errp) {
    assert(socketPath);
    assert(strlen(socketPath));

    SMFPConnectionRef connection = (SMFPConnectionRef)malloc(sizeof(SMFPConnection));
    connection->state = SMFPConnectionState_Closed;
    connection->socketPath = CStringCopy(socketPath);
    connection->transactionID = 1;
    pthread_mutex_init(&connection->mutex, NULL);
    NewElementList(&connection->outstandingTransactions);

    //--

    SMFPErr err = SMFPErr_NoErr;

    if (err) {
        if(connection) {
            SMFPConnectionDispose(connection);
        }
        connection = NULL;
    }

    if (errp) *errp = err;
    return connection;
}

SMFPErr SMFPConnectionSwitchSocket(SMFPConnectionRef connection, const char *socketPath) {
    assert(connection);
    assert(socketPath);
    assert(strlen(socketPath));

    pthread_mutex_lock(&connection->mutex);
    shutdown(connection->socketFD, SHUT_RDWR);
    close(connection->socketFD);
    connection->state = SMFPConnectionState_Closed;
    free((void*)connection->socketPath);
    connection->socketPath = CStringCopy(socketPath);
    pthread_mutex_unlock(&connection->mutex);

    return SMFPErr_NoErr;
}

void SMFPConnectionDispose(SMFPConnectionRef connection) {
    assert(connection);
    // TODO: close sockedFD
    // TODO: join readerThread
    free(connection);
}

SMFPErr _SMFPReadConnection(SMFPConnectionRef connection, void *buf, size_t nbyte)
{
    ssize_t readResult = read(connection->socketFD, buf, nbyte);

    if (readResult == 0 || readResult != nbyte) {
        pthread_mutex_lock(&connection->mutex);
        connection->state = SMFPConnectionState_Closed;
        pthread_mutex_unlock(&connection->mutex);
        return SMFPErr_Local_ConnectionFailed;
    }
    if (readResult < 0) {
        trace_errno("read()");
        exit(EXIT_FAILURE);
    }

    return SMFPErr_NoErr;
}

void* _SMFPReaderThread(SMFPConnectionRef connection)
{
    puts("entering _SMFPReaderThread");

    typedef struct {
        int32_t   length;
        uint32_t  transactionID;
    } SMFPResponseHeader;

    SMFPErr err = SMFPErr_NoErr;

    while (!err) {
        SMFPResponseHeader responseHeader;
        err = _SMFPReadConnection(connection, &responseHeader, sizeof(responseHeader));
        if (!err) {
            responseHeader.length = ntohl(responseHeader.length);
            responseHeader.transactionID = ntohl(responseHeader.transactionID);
            //printf("RECEIVED transactionID %d\n", responseHeader.transactionID);

            pthread_mutex_lock(&connection->mutex);
            SMFPTransaction *transaction = NULL, *transactionItr = (SMFPTransaction*)connection->outstandingTransactions.first;
            while (!transaction && transactionItr) {
                if (transactionItr->transactionID == responseHeader.transactionID) {
                    transaction = transactionItr;
                }
                transactionItr = (SMFPTransaction*)transactionItr->element.next;
            }
            pthread_mutex_unlock(&connection->mutex);

            if (transaction) {
                SMFPResponseReceiverResult responseReceiverResult;
                if (responseHeader.length < 0) {
                    // It's an error.
                    transaction->responseReceiverErr = responseHeader.length;
                    responseReceiverResult = transaction->responseReceiver(responseHeader.length,
                                                                           -1,
                                                                           0,
                                                                           transaction->responseReceiverContext);
                } else {
                    // It's a response.
                    responseReceiverResult = transaction->responseReceiver(SMFPErr_NoErr,
                                                                           connection->socketFD,
                                                                           responseHeader.length,
                                                                           transaction->responseReceiverContext);
                }
                if (responseReceiverResult.transactionCompleted) {
                    transaction->responseReceiverErr = responseReceiverResult.err;

                    pthread_mutex_lock(&transaction->transactionCompleteMutex);
                    pthread_cond_signal(&transaction->transactionCompleteCond);
                    pthread_mutex_unlock(&transaction->transactionCompleteMutex);
                }
            } else {
                printf("SMFP: WARN couldn't find outstanding transactionID %d. Server probably responded to the same transaction twice, which is a protocol error. Ignoring.\n", responseHeader.transactionID);
            }
        }
    }

    printf("_SMFPReaderThread: _SMFPReadConnection failed with %s\n", SMFPErrToStr(err));

    pthread_mutex_lock(&connection->mutex);
    SMFPTransaction *transactionItr;
    for (
        transactionItr = (SMFPTransaction*)connection->outstandingTransactions.first;
        transactionItr;
        transactionItr = (SMFPTransaction*)transactionItr->element.next
    ){
        printf("_SMFPReaderThread: canceling transaction %p\n", transactionItr);

        transactionItr->responseReceiverErr = SMFPErr_Local_ConnectionFailed;

        pthread_mutex_lock(&transactionItr->transactionCompleteMutex);
        pthread_cond_signal(&transactionItr->transactionCompleteCond);
        pthread_mutex_unlock(&transactionItr->transactionCompleteMutex);
    }
    pthread_mutex_unlock(&connection->mutex);

    puts("exiting _SMFPReaderThread");
    return NULL;
}

SMFPErr SMFPSendRequestReceiveResponses(SMFPConnectionRef connection, uint8_t requestCode, uint32_t requestArgSize, const void *requestArg, SMFPResponseReceiver responseReceiver, void *responseReceiverContext)
{
    assert(connection);
    assert(responseReceiver);

    SMFPErr err = SMFPErr_NoErr;

    //
    // Set up transaction.
    //

    pthread_mutex_lock(&connection->mutex); // Because of transactionID++.
        SMFPTransaction transaction = {
            .element = {
                .next = NULL,
                .prev = NULL,
                .list = NULL,
            },
            .connection = connection,
            .transactionID = connection->transactionID++,
            .responseReceiver = responseReceiver,
            .responseReceiverContext = responseReceiverContext,
            .transactionCompleteMutex = PTHREAD_MUTEX_INITIALIZER,
            .transactionCompleteCond = PTHREAD_COND_INITIALIZER,
        };
        pthread_mutex_lock(&transaction.transactionCompleteMutex);

        PutLastElementType(&transaction, &connection->outstandingTransactions, SMFPTransaction, element);
    pthread_mutex_unlock(&connection->mutex);

    //
    // Connect/reconnect if necessary.
    //

reconnect:
    pthread_mutex_lock(&connection->mutex);
        if (!err && connection->state == SMFPConnectionState_Closed) {
            connection->state = SMFPConnectionState_Opening;

            connection->socketFD = socket(AF_UNIX, SOCK_STREAM, 0);
            if (connection->socketFD < 0) {
                trace_errno("socket()");
                err = errno;
                connection->socketFD = 0;
            }

            struct sockaddr_un addr;
            bzero(&addr, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, connection->socketPath, sizeof(addr.sun_path)-1);
            uint8_t retries = 10;
            while (!err && connection->state == SMFPConnectionState_Opening && retries--) {
                if (connect(connection->socketFD, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    if (ECONNREFUSED == errno || ENOENT == errno) {
                        printf("SMFP: ENOENT||ECONNREFUSED, sleep(1) until retrying connect() (retries left: %d, path: %s)\n", retries, connection->socketPath);
                        sleep(1);
                    } else {
                        trace_errno("connect()");
                        err = errno;
                    }
                } else {
                    connection->state = SMFPConnectionState_Open;
                }
            }
            if (!err && retries == UINT8_MAX) {
                err = SMFPErr_Local_ConnectionFailed;
            }
            if (err) {
                connection->state = SMFPConnectionState_Closed;
            } else {
                err = errno = pthread_create(&connection->responseReaderThread, NULL, (pthread_start_proc)_SMFPReaderThread, transaction.connection);
                if (err) {
                    trace_errno("pthread_create");
                }
                // TODO: better cleanup on error
            }
        }
    pthread_mutex_unlock(&connection->mutex);

    //
    // Send transaction's request.
    //

    if (!err) {
        uint32_t messageSize = htonl(1 + 4 + requestArgSize);
        uint32_t transactionID = htonl(transaction.transactionID);
        struct iovec iov[4] = {
            {&messageSize,       sizeof(messageSize)},
            {&requestCode,       sizeof(requestCode)},
            {&transactionID,     sizeof(transactionID)},
            {(void*)requestArg,  requestArgSize},
        };

        ssize_t writeResult = writev(connection->socketFD, iov, 4);
        if (writeResult < 0) {
            if (EPIPE == errno) {
                pthread_mutex_lock(&connection->mutex);
                connection->state = SMFPConnectionState_Closed;
                pthread_mutex_unlock(&connection->mutex);
                goto reconnect;
            } else {
                trace_errno("writev()");
                exit(EXIT_FAILURE);
            }
        } else {
            //printf("SENT transactionID %d\n", transaction.transactionID);
        }
    }

    //
    // Receive transaction's response(s).
    //

    if (!err) {
        pthread_cond_wait(&transaction.transactionCompleteCond, &transaction.transactionCompleteMutex);
        pthread_mutex_unlock(&transaction.transactionCompleteMutex);

        err = transaction.responseReceiverErr;
    }

    //printf("REMOVING transactionID %d\n", transaction.transactionID);
    pthread_mutex_lock(&connection->mutex);
    RemoveElementType(&transaction, &connection->outstandingTransactions, SMFPTransaction, element);
    pthread_mutex_unlock(&connection->mutex);

    return err;
}

SMFPErr SMFPRead(int fd, void *buf, size_t nbytes)
{
    ssize_t readResult = read(fd, buf, nbytes);
    if (readResult <= 0) {
        if (readResult == 0) {
            return SMFPErr_Local_ConnectionFailed;
        } else {
            trace_errno("read()");
            exit(EXIT_FAILURE);
        }
    } else {
        if (readResult != nbytes) {
            return SMFPErr_Local_ConnectionFailed;
        } else {
            return SMFPErr_NoErr;
        }
    }
}

//-----------------------------------------------------------------------------------------

int IsSMFPErr(int err) {
    return err <= SMFPErr_BeginNumberspace && err >= SMFPErr_EndNumberspace;
}

const char* SMFPErrToStr(SMFPErr err) {
    switch (err) {
        case SMFPErr_NoErr:
            return "SMFPErr_NoErr";
        case SMFPErr_Local_ConnectionFailed:
            return "SMFPErr_Local_ConnectionFailed";
        case SMFPErr_Remote_UnknownRequestCode:
            return "SMFPErr_Remote_UnknownRequestCode";
        default: {
            static char internalBuffer[100];
            snprintf(internalBuffer, sizeof(internalBuffer), "unknown SMFPErr:%d\n", err);
            return internalBuffer;
        }
    }
}
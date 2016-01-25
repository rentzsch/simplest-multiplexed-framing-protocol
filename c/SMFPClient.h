// SMFP: Simplest Multiplexed Framing Protocol

#ifndef SMFPClient_h
#define SMFPClient_h

#include <stdbool.h>
#include <stdint.h>  // for uint8_t

typedef enum {
    SMFPErr_NoErr                      = 0,
    SMFPErr_BeginNumberspace           = -100,

    SMFPErr_Local_BeginNumberspace     = -100,
    SMFPErr_Local_ConnectionFailed     = -100,
    SMFPErr_Local_EndNumberspace       = -199,

    SMFPErr_Remote_BeginNumberspace    = -200,
    SMFPErr_Remote_UnknownRequestCode  = -200,
    SMFPErr_Remote_EndNumberspace      = -299,

    SMFPErr_EndNumberspace             = -299,
} SMFPErr;

//
// Connection.
//

typedef struct SMFPConnection* SMFPConnectionRef;

SMFPConnectionRef SMFPConnectionCreate(const char *socketPath, SMFPErr *err);

SMFPErr SMFPConnectionSwitchSocket(SMFPConnectionRef connection, const char *socketPath);

void SMFPConnectionDispose(SMFPConnectionRef connection);

//
// Meat.
//

typedef struct {
    bool     transactionCompleted;
    SMFPErr  err;
} SMFPResponseReceiverResult;

// ResponseReceivers MUST SMFPRead(responseSize) unless err.
typedef SMFPResponseReceiverResult (*SMFPResponseReceiver)(SMFPErr err,
                                                           int socketFD,
                                                           uint32_t responseSize,
                                                           void *context);



SMFPErr SMFPSendRequestReceiveResponses(SMFPConnectionRef connection,
                                        uint8_t requestCode,
                                        uint32_t requestArgSize,
                                        const void *requestArg,
                                        SMFPResponseReceiver responseReceiver, // called on another thread
                                        void *responseReceiverContext);



// Simplifying read(2) wrapper for your ResponseReceiver.
SMFPErr SMFPRead(int fd, void *buf, size_t nbytes);

//
// Error.
//

const char* SMFPErrToStr(SMFPErr err);

#endif /* SMFPClient_h */

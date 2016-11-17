#include <stdio.h>
#include <stdbool.h>
#include "SMFPClient.h"

static bool sResponseReceived = false;

static SMFPResponseReceiverResult Uppercase_ResponseReceiver(SMFPErr err, int socketFD,
    uint32_t responseSize, char *output)
{
    SMFPResponseReceiverResult result = {
        .transactionCompleted = true,
        .err = err,
    };

    result.err = SMFPRead(socketFD, output, responseSize);

    sResponseReceived = true;

    return result;
}

int main(int argc, const char * argv[]) {
    SMFPErr err;
    SMFPConnectionRef connection = SMFPConnectionCreate("/tmp/smfp.sock", &err);

    const char input[] = "hello smfp";
    char output[sizeof(input)];
    if (!err) {
        printf("sending %s\n", input);
        err = SMFPSendRequestReceiveResponses(
            connection,
            0x42,
            sizeof(input),
            input,
            (SMFPResponseReceiver)Uppercase_ResponseReceiver,
            output);
    }

    if (!err) {
        printf("waiting for response\n");

        // Stupid-but-easy busy wait:
        while (!sResponseReceived) {}

        printf("received %s\n", output);
    }

    if (err) {
        printf("ERR %s\n", SMFPErrToStr(err));
        return err;
    }
    return 0;
}

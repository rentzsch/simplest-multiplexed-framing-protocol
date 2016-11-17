// Simplest Multiplexed Framing Protocol Request Format:
//
// Field                                   Offset  Length  Type  End
// -----                                   ------  ------  ----  ---
// subsequent message length (at least 5)  0       4       u32   4
// request code                            4       1       u8    5
// transactionID                           5       4       u32   9
// argument data                           9       n       u8[]

'use strict';

function parseSMFPRequest(reqBuffer) {
    const kLengthFieldLength = 4;
    const kRequestCodeFieldLength = 1;
    const kTransactionFieldLength = 4;
    const kRequestHeaderLength = kLengthFieldLength +
        kRequestCodeFieldLength +
        kTransactionFieldLength;

    let result = { err: 'incomplete' };
    if (reqBuffer.length >= kRequestHeaderLength) {
        const clientMessageLength = reqBuffer.readUInt32BE(0);
        if (clientMessageLength < 5 || clientMessageLength > 10*1024*1024) {
            return {
                err: 'invalid message length '+clientMessageLength,
            };
        }
        if (reqBuffer.length >= kLengthFieldLength + clientMessageLength) {
            // Request complete.
            const requestArg = new Buffer(
                clientMessageLength -
                kRequestCodeFieldLength -
                kTransactionFieldLength
            );
            reqBuffer.copy(
                requestArg,
                0,
                kRequestHeaderLength,
                kRequestHeaderLength+clientMessageLength
            );
            result = {
                err: null,
                code: reqBuffer.readUInt8(kLengthFieldLength),
                transactionID: reqBuffer.readUInt32BE(kLengthFieldLength+kRequestCodeFieldLength),
                arg: requestArg,
                totalLength: kLengthFieldLength + clientMessageLength,
            };
        } else {
            // Still incomplete.
        }
    }

    return result;
}

module.exports = parseSMFPRequest;
// smfp-server.js semver:1.3.0
//   Copyright (c) 2015-2016 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/mit
//   https://github.com/rentzsch/simplest-multiplexed-framing-protocol

'use strict';

const net = require('net');
const fs = require('fs');

const parseSMFPRequest = require('./parse-smfp-request');

class SMFPServer {
    constructor(unixSocketPath, requestHandler) {
        this.unixSocketPath = unixSocketPath;
        this.requestHandler = requestHandler;
        this.netServer = net.createServer(this.handleNetConnection.bind(this));
        this.connectionIDCounter = 0;

        try { fs.unlinkSync(unixSocketPath); } catch(ex){global;}
        this.netServer.listen(unixSocketPath);
        console.log('smfp server listening on '+unixSocketPath);
    }
    handleNetConnection(netConnection) {
        const connectionID = this.connectionIDCounter++;
        console.log('smfp client '+connectionID+' connected');
        new SMFPConnection(this, connectionID, netConnection);
    }
}
module.exports.SMFPServer = SMFPServer;

class SMFPConnection {
    constructor(server, connectionID, netConnection) {
        this.server = server;
        this.connectionID = connectionID;
        this.netConnection = netConnection;
        this.transactionsByIDs = Object.create(null);
        this.reqBuffer = new Buffer(0);

        netConnection.on('data', this.netConnectionOnData.bind(this));
        netConnection.on('error', this.netConnectionOnError.bind(this));
        netConnection.on('end', this.netConnectionOnEnd.bind(this));
    }
    netConnectionOnData(chunk) {
        //console.log('    reading', chunk);
        this.reqBuffer = Buffer.concat([this.reqBuffer, chunk]);
        this.processReqBuffer();
    }
    netConnectionOnError(err) {
        if (err.code === 'EPIPE' && err.syscall === 'write') {
            console.warn('ignoring EPIPE write error (smfp client probably crashed)');
        } else {
            throw err;
        }
    }
    netConnectionOnEnd() {
        console.log('smfp client '+this.connectionID+' disconnected');
    }

    processReqBuffer() {
        while (this.reqBuffer.length) {
            const smfpRequest = parseSMFPRequest(this.reqBuffer);
            if (smfpRequest.err === null) {
                //console.log('    transactionID', smfpRequest.transactionID);
                this.processRequest(smfpRequest);
            } else if (smfpRequest.err === 'incomplete') {
                // Not really an error, just need more data.
            } else {
                // TODO: report the error the client
                console.log('error', smfpRequest);
                this.reqBuffer = new Buffer(0);
            }
        }
    }
    processRequest(req) {
        // Remove this request's bytes from the request buffer.
        const smallerReqBuffer = new Buffer(this.reqBuffer.length - req.totalLength);
        this.reqBuffer.copy(smallerReqBuffer, 0, req.totalLength);
        this.reqBuffer = smallerReqBuffer;

        const transaction = new SMFPTransaction(
            this,
            req.code,
            req.transactionID,
            req.arg
        );
        this.transactionsByIDs[req.transactionID] = transaction;
        this.server.requestHandler(transaction);
    }
    write(transaction, resBuf, completedTransaction) {
        try {
            //console.log('    writing', resBuf);
            this.netConnection.write(resBuf);
            if (completedTransaction) {
                this._completeTransaction(transaction);
            }
        } catch (ex) {
            if (ex.code === 'EPIPE') {
                // Client crashed.
                this._completeTransaction(transaction);
            } else {
                throw ex;
            }
        }
    }
    _completeTransaction(transaction) {
        delete this.transactionsByIDs[transaction.transactionID];
        //console.log('    transaction '+transaction.transactionID+' completed');
    }
}
module.exports.SMFPConnection = SMFPConnection;

//
//  SMFPTransaction
//
//  NICE TODO: indicate client disconnection (crash) to requestHandler.
//

class SMFPTransaction {
    constructor(connection, requestCode, transactionID, requestArg) {
        this.connection = connection;
        this.requestCode = requestCode;
        this.transactionID = transactionID;
        this.requestArg = requestArg;
        this.markedCompleted = false;
    }

    // Response format:
    // s32    +resLength || -err
    // u32    txID
    // u8[]?  resData

    respond(responseData, transactionCompleted) {
        if (transactionCompleted && this.markedCompleted) {
            console.warn('WARN transaction marked completed more than once');
        }
        this.markedCompleted = transactionCompleted;

        const resHeadBuf = new Buffer(8);
        resHeadBuf.writeInt32BE(responseData.length, 0);
        resHeadBuf.writeUInt32BE(this.transactionID, 4);
        const resBuf = Buffer.concat([resHeadBuf, responseData]);
        this.connection.write(this, resBuf, transactionCompleted);
    }

    respondString(responseString, transactionCompleted) {
        return this.respond(new Buffer(responseString), transactionCompleted);
    }

    respondErr(negativeErrCode) {
        if (this.markedCompleted) {
            console.warn('WARN transaction marked completed more than once');
        }
        this.markedCompleted = true;

        const resBuf = new Buffer(8);
        resBuf.writeInt32BE(negativeErrCode, 0);
        resBuf.writeUInt32BE(this.transactionID, 4);
        this.connection.write(this, resBuf, true);
    }
}
module.exports.SMFPTransaction = SMFPTransaction;
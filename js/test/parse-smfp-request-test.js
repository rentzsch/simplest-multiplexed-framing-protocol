var assert = require('assert');

require('babel/register')({ optional: ['es7.asyncFunctions'] });
var parseSMFPRequest = require('../parse-smfp-request.babel');

describe('parseSMFPRequest', function(){
    it('empty buffer', function(){
        var reqBuffer = new Buffer(0);
        assert.equal(parseSMFPRequest(reqBuffer).err, 'incomplete');
    });

    it('incomplete buffer', function(){
        [1, 2, 3].forEach(function(bufferLength){
            var reqBuffer = new Buffer(bufferLength);
            reqBuffer.fill(0x00);
            assert.equal(parseSMFPRequest(reqBuffer).err, 'incomplete');
            reqBuffer.fill(0xFF);
            assert.equal(parseSMFPRequest(reqBuffer).err, 'incomplete');
        });
    });

    it('bad client message lengths', function(){
        var reqBuffer = new Buffer(9);
        reqBuffer.fill(0x11);

        [0, 1, 2, 3, 4].forEach(function(n){
            reqBuffer.writeUInt32BE(n, 0);
            assert.equal(parseSMFPRequest(reqBuffer).err, 'invalid message length '+n);
        });

        reqBuffer.fill(0xFF);
        assert.equal(parseSMFPRequest(reqBuffer).err, 'invalid message length 4294967295');

        reqBuffer.writeUInt32BE(10*1024*1024, 0);
        assert.equal(parseSMFPRequest(reqBuffer).err, 'incomplete');

        reqBuffer.writeUInt32BE(10*1024*1024+1, 0);
        assert.equal(parseSMFPRequest(reqBuffer).err, 'invalid message length 10485761');
    });

    it('empty argument data', function(){
        var reqBuffer = new Buffer(9);
        reqBuffer.fill(0x11);
        reqBuffer.writeUInt32BE(5, 0);  // length
        reqBuffer.writeUInt8(0x22, 4);  // code
        reqBuffer.writeUInt32BE(0x33445566, 5);  // transaction ID

        var req = parseSMFPRequest(reqBuffer);
        assert.equal(req.error, null);
        assert.equal(req.code, 0x22);
        assert.equal(req.transactionID, 0x33445566);
        assert.equal(req.arg.length, 0);
        assert.equal(req.totalLength, 9);
    });

    it('1 byte of argument data', function(){
        var reqBuffer = new Buffer(10);
        reqBuffer.fill(0x11);
        reqBuffer.writeUInt32BE(6, 0);  // length
        reqBuffer.writeUInt8(0x22, 4);  // code
        reqBuffer.writeUInt32BE(0x33445566, 5);  // transaction ID
        reqBuffer.writeUInt8(0x77, 9);  // arg data

        var req = parseSMFPRequest(reqBuffer);
        assert.equal(req.error, null);
        assert.equal(req.code, 0x22);
        assert.equal(req.transactionID, 0x33445566);
        assert.equal(req.arg.length, 1);
        assert.equal(req.totalLength, 10);

        var x = new Buffer(1);
        x.writeUInt8(0x77, 0);
        assert.equal(req.arg.toString(), x.toString()); // toString() instead of .equals since we're still on node 0.10
    });
});
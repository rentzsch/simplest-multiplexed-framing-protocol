## Description

Simplest Multiplexed Framing Protocol (SMFP) is a small, simple, and efficient binary protocol.

### Small and Simple:

Here's the entire protocol. The format for requests:

| Field                                   | Offset  | Length  | Type  | End
| ---                                     | ---     | ---     | ---   | ---
| subsequent message length (at least 5)  | 0       | 4       | u32   | 4
| request code                            | 4       | 1       | u8    | 5
| transactionID                           | 5       | 4       | u32   | 9
| argument data                           | 9       | n       | u8[]  | ...

The format for responses:

| Field                                                           | Offset  | Length  | Type  | End
| ---                                                             | ---     | ---     | ---   | ---
| response data length (if positive) or error code (if negative)  | 0       | 4       | s32   | 4
| transactionID                                                   | 4       | 4       | u32   | 8
| response data                                                   | 8       | n       | u8[]  | ...

Everything is fixed size except for the variably-sized request argument and response data.

Note a single request can have multiple responses. In such cases, I recommend delimiting the end of the responses with a response with an empty data.

### Multiplexed:

Clients can issue multiple outstanding requests.

## Implementations

Included is a C client implementation and a Node.js server implementation. Both operate over unix sockets. This is what's provided because this is what I needed. It should be straight-forward to extend f.x. a Node.js client implementation or add support for TCP sockets.

The C implementation is interesting in that is doesn't dynamically allocate memory in the response-receiving path.

`SMFPSendRequestReceiveResponses()` sends the request and calls the `responseReceiver` callback for each response. Warning: `responseReceiver` is always called on a different thread than `SMFPResponseReceiverResult()` was called on.

## Version History

### v1.3.0: Nov 17 2016

- NEW Write this README. Tag previous "releases".
- NEW Add example.
- NEW smfp-server.js: warn if a transaction is completed more than once.
- CHANGE SFMPClient.c: don’t exit upon receiving a response to an unknown transaction. Instead issue a (better) warning.
- CHANGE JS: `var`/`let` => `const` and `'use strict'` wherever possible.
- FIX tests: don’t actually need babel, but do need mocha (in dev).

### v1.2.0: Mar 06 2016

- NEW `IsSMFPErr()`.

### v1.1.1: Mar 06 2016

- FIX cancel all outstanding transactions upon server disconnect.

### v1.1.0: Jan 25 2016

- NEW switch connection socket path. Use-case: talk to a different backend upon HUP.
- FIX always remove current transaction from outstanding list regardless of error.

### v1.0.0: Nov 19 2015

- Initial release.
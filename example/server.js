const SMFPServer = require('../js/smfp-server').SMFPServer;

new SMFPServer('/tmp/smfp.sock', (transaction) => {
    switch (transaction.requestCode) {
        case 0x42: {
            const input = transaction.requestArg.toString();
            const output = input.toUpperCase();
            console.log(`${input} => ${output}`);
            transaction.respondString(output, true);
        } break;
        default:
            transaction.respondErr(-200); // SMFPErr_Remote_UnknownRequestCode
            console.warn('unknown requestCode', transaction.requestCode);
    }
});
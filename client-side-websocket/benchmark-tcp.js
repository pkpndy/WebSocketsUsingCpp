const stdio = require("stdio");
var net = require('net');

const ops = stdio.getopt({
  connectionCount: {
    key: "c",
    args: 1,
    description: "Number of connections to make",
  },
  useKey: { key: "u", default: false },
  endpoint: {
    key: "e",
    args: 1,
    description: "URL for the endpoint to use",
    default: "ws://localhost:8080",
  },
});

const endpoint = ops.endpoint;

console.log(`\n`);
console.log(`Benchmarking: ${endpoint}\n`);

let processed = 0;
let connected = 0;
let failed = 0;
let droppped = 0;

let reqPerSec = 0;
let minReqTime = 0;
let maxReqTime = 0;

let timeRecords = {};
let timeStarted = new Date().getTime();

const summary = function () {
  let __timeNow = new Date().getTime();
  let totalTimeTaken = __timeNow - timeRecords[0];
  let avgReqTime = (ops.connectionCount / totalTimeTaken).toFixed(4);

  console.log("\n");
  console.log("Benchmarking result:");
  console.log(` - Connected: ${connected}`);
  console.log(` - Failed: ${failed}`);
  console.log(` - Total time taken: ${totalTimeTaken} ms`);
  console.log(` - Min req time: ${minReqTime} ms`);
  console.log(` - Max req time: ${maxReqTime} ms`);
  console.log(` - Avg req time: ${avgReqTime} ms`);
  console.log(
    ` - Connections/second: ${((connected * 1000) / totalTimeTaken).toFixed(2)}`
  );

  console.log("\n");

  process.exit(0);
};

for (var i = 0; i < ops.connectionCount; i++) {
  timeRecords[i] = new Date().getTime();

  var client = new net.Socket();


  client.connect(8080, "127.0.0.1",
    function () {
      connected++;
      processed++;
      let timeNow = new Date().getTime();
      let connectionTime = timeNow - timeRecords[this];

      if (connectionTime > maxReqTime) {
        maxReqTime = connectionTime;
      }

      if (minReqTime == 0 || connectionTime < minReqTime) {
        minReqTime = connectionTime;
      }

      console.log(
        `${this} connected in ${
          timeNow - timeRecords[this]
        }ms, Total time elapsed: ${timeNow - timeRecords[0]}`
      );

      if (processed == ops.connectionCount) {
        summary();
      }
    }.bind(i)
  );

  client.on(
    "close",
    function () {
      failed++;
      processed++;
      console.log(`Failed: ${this}`);

      if (processed == ops.connectionCount) {
        summary();
      }
    }.bind(i)
  );

  let _timeNow = new Date().getTime();
  console.log(`Connecting: ${i} , Time elapsed: ${_timeNow - timeStarted}`);
}

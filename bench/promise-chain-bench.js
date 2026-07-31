// Phase 6 benchmark: sequential Promise.then() chaining.
// Promise is a standard ECMAScript global the JS engine itself provides
// (SpiderMonkey/V8/JavaScriptCore alike, via JS::InitRealmStandardClasses
// in Forge's case) -- Forge didn't have to implement Promise itself, only
// wire JS::SetJobQueue(cx, &jobQueue_) so promise reactions actually get
// scheduled (see forge.cpp's ForgeJobQueue::enqueuePromiseJob). This runs
// unmodified on forge, bun, and node alike.
//
// Unlike microtask-bench.js (which schedules every microtask up front),
// this chains ITERATIONS .then() continuations one at a time -- each only
// scheduled once the previous one resolves. That's a much closer shape to
// real async/await-style code (a sequence of dependent async steps) than
// a flat burst of independent microtasks, and stresses the one-at-a-time
// schedule/dispatch/reschedule loop rather than a queue's raw throughput
// under a single big backlog.
if (typeof print === "undefined") { var print = console.log; }

var ITERATIONS = 50000;
var count = 0;

function step() {
    count++;
    if (count >= ITERATIONS) {
        print("promise-chain-bench done, count=" + count);
        return;
    }
    Promise.resolve().then(step);
}

Promise.resolve().then(step);

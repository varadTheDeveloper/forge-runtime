// Phase 0 benchmark: raw JS execution (engine-bound).
// This one mostly measures the JS engine itself (SpiderMonkey vs
// JavaScriptCore vs V8), not anything Forge's own runtime layer controls.
// Track it anyway — it's an honest baseline, not a target Forge can move
// much on its own.
if (typeof print === "undefined") { var print = console.log; }

function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

var result = fib(34);
print("loop-bench done, fib(34)=" + result);

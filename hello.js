queueMicrotask(() => {
    print("microtask");
});

setTimeout(() => {
    print("timeout");
}, 0);

setInterval(() => {
    print("tick");
}, 1000);


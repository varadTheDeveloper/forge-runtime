# Forge

Forge is an experimental JavaScript runtime built on top of Mozilla's SpiderMonkey engine.

The goal of Forge is to learn and expose how modern JavaScript runtimes work internally by implementing runtime features from scratch instead of relying on existing implementations.

## Features

### Runtime

* JavaScript execution powered by SpiderMonkey
* Custom global object
* Native `print()` implementation

### Timers

* `setTimeout()`
* `clearTimeout()`
* `setInterval()`

### Event Loop

* Custom event loop implementation
* Timer queue (macrotasks)
* Microtask queue (FIFO)
* `queueMicrotask()`

## Current Architecture

```text
                 JavaScript
                      │
      ┌───────────────┴───────────────┐
      │                               │
      ▼                               ▼
 queueMicrotask()              setTimeout()
 Promise Jobs (WIP)            setInterval()
      │                               │
      ▼                               ▼
  Microtask Queue                Timer Queue
      │                               │
      └───────────────┬───────────────┘
                      ▼
                 Event Loop
                      │
                Execute Jobs
```

## Example

```js
queueMicrotask(() => {
    print("microtask");
});

setTimeout(() => {
    print("timeout");
}, 0);

print("sync");
```

Output:

```text
sync
microtask
timeout
```

## Roadmap

* [x] `print()`
* [x] `setTimeout()`
* [x] `clearTimeout()`
* [x] `setInterval()`
* [x] `queueMicrotask()`
* [x] Event Loop
* [ ] Promise Job Queue
* [ ] `fetch()`
* [ ] File System APIs
* [ ] TCP/HTTP Networking
* [ ] ES Modules
* [ ] Worker Threads
* [ ] WebAssembly APIs

## Why Forge?

Forge is a learning-focused runtime that explores how JavaScript engines communicate with the host environment. Every feature is implemented incrementally to better understand the architecture behind browsers and server-side runtimes.

## Status

Forge is under active development and is not intended for production use.

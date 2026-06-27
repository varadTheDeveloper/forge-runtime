# Forge Runtime

A lightweight JavaScript runtime built on top of Mozilla SpiderMonkey.

## Current Features

* Execute JavaScript files
* Native `print()` API
* Native `setTimeout()` API (work in progress)
* Custom event loop implementation (work in progress)
* Built directly on SpiderMonkey

## Example

```js
print("Hello Forge");

setTimeout(() => {
    print("Hello after timeout");
}, 1000);
```

Run:

```bash
forge hello.js
```

## Architecture

```
JavaScript
     ↓
SpiderMonkey
     ↓
Forge Runtime APIs
     ↓
Event Loop
```

## Roadmap

### v0.1

* [x] Execute JavaScript files
* [x] Native print()
* [x] JavaScript evaluation
* [x] Timer registration
* [x] Basic event loop

### v0.2

* [ ] Execute timer callbacks
* [ ] clearTimeout()
* [ ] setInterval()
* [ ] Improved error reporting

### v0.3

* [ ] File System APIs
* [ ] HTTP Server
* [ ] Worker Threads
* [ ] Built-in SQLite

## Why Forge?

Forge is an experiment in building a modern JavaScript runtime from the ground up using SpiderMonkey. The goal is to explore runtime architecture, event loops, native APIs, concurrency, and developer experience while remaining lightweight and easy to understand.

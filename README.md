# Forge Runtime

A JavaScript runtime powered by Mozilla SpiderMonkey.

## Features

- JavaScript Runtime
- Event Loop
- print()
- setTimeout()
- clearTimeout()
- setInterval()
- clearInterval()

## Installation

Download the latest Windows installer from the Releases page.

Run:

```bash
forge hello.js
```

## Example

```javascript
print("Hello Forge!");

setTimeout(() => {
    print("Timer!");
}, 1000);
```

## Current Status

Forge is currently in Preview (v0.1.0).

More APIs are coming:

- process
- fs
- fetch
- modules
- http
- net
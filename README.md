# Forge

Forge is an experimental JavaScript runtime built on Mozilla's SpiderMonkey engine.

The project focuses on building a modern runtime from the ground up with a native, high-performance architecture rather than layering new APIs over existing runtimes.

> **Status:** Early development

---

## Current Progress

### ✅ Completed

- SpiderMonkey integration
- Forge Core foundation
  - Types
  - Error
  - Result / ResultVoid
  - Assert
  - Allocator
  - UniquePtr
  - Vector
- Native Windows IOCP Event Loop
- `setTimeout`
- `setInterval`
- `clearTimeout`

---

## Current Platform

- Windows x64

Linux and macOS support are planned.

---

## Running

Create a file:

```javascript
console.log("Hello Forge!");
```

Run it:

```bash
forge.exe hello.js
```

---

## Benchmarks

| Benchmark | Forge | Bun | Node |
|-----------|------:|----:|-----:|
| Startup | 25.01 ms | 47.35 ms | 53.85 ms |
| JSON | 383.29 ms | 264.53 ms | 566.12 ms |
| Loop | 89.96 ms | 81.23 ms | 103.21 ms |

---

## Roadmap

The next major milestones are:

- Arena allocator
- Collections
- Filesystem
- Networking
- HTTP server
- Worker threads
- Runtime integration
- Startup optimization
- Module system

See `ROADMAP.md` for details.

---

## License

MIT
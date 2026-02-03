# Multithreaded HTTP Proxy Server with O(1) LRU Cache

A high-performance, multithreaded HTTP proxy server written in C, featuring a custom-built, thread-safe LRU cache.

![Proxy Server Overview](https://github.com/user-attachments/assets/cd4de2ab-c5d6-4995-87a6-f1c638c0df6e)

## 📌 Overview

This project implements a proxy server capable of handling multiple concurrent client requests. By integrating an **LRU (Least Recently Used) Cache**, the server reduces latency for repeated requests by serving content directly from memory.

### ⚡ The O(1) Optimization

Unlike basic caches that use a time-based scan O(n), this implementation uses a **Hash Map** combined with a **Doubly Linked List**.

- **Hash Map:** Allows for O(1) lookup of cached URLs.
- **Doubly Linked List:** Allows for O(1) eviction of the oldest items and promotion of recently used items.

---

## ✨ Features

- **Multithreaded Architecture:** Uses a thread-per-client model via `pthread` for high concurrency.
- **Advanced LRU Caching:** Implements a true O(1) cache using a Hash Map and Doubly Linked List.
- **Thread-Safe Design:** Uses `pthread_mutex_t` to prevent race conditions during cache access.
- **Concurrency Control:** Employs semaphores to manage the maximum number of active worker threads.
- **HTTP/1.1 Support:** Specifically handles `GET` requests, parsing hostnames, ports, and paths.
- **Robust Error Handling:** Returns standard HTTP error codes (400, 404, 500, etc.) when things go wrong.

---


### Core Components

1.  **Main Thread:** Listens for connections on the designated port and spawns worker threads.
2.  **Worker Threads (`thread_fn`):** Handles the entire lifecycle of a client request—parsing, cache checking, remote fetching, and responding.
3.  **Cache Logic:** \* **Promotion:** When a URL is accessed, it is moved to the **Head** of the list.
    - **Eviction:** When the cache is full, the **Tail** of the list is removed to make space.

---

## 🛠 Build & Run

### Prerequisites

- GCC Compiler
- Pthread library (Standard on Linux/macOS)

### Compilation

You can compile using the provided **Makefile**:

```bash
make clean
make
```

### Running the server

```bash
./proxy 8080
```

## Testing

Use curl to test the proxy's functionality and caching.

1. Initial Request (Cache Miss)
   The proxy will fetch the data from the remote server and store it.

```bash
curl -x http://localhost:8080 [http://example.com](http://example.com)
```

2. Subsequent Request (Cache Hit)
   The proxy will serve the data instantly from the $O(1)$ LRU cache.

```bash
curl -x http://localhost:8080 [http://example.com](http://example.com)
```

<img width="737" height="858" alt="proxy_server" src="https://github.com/user-attachments/assets/c219f263-9818-4a72-8957-fad7b712616b" />


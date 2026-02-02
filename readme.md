# Multithreaded Proxy Server with O(1) LRU Cache

## Overview

This repository contains a multithreaded HTTP proxy server in C with an O(1) LRU cache. The cache now uses a doubly-linked list (for recency order) plus a simple hash map (separate chaining) mapping URL → node for constant-time lookups and tail-pop eviction.

## Main changes (current code)

- Cache nodes are `cache_node` with `prev`/`next` pointers (doubly-linked list).
- A hash map (array of chains) provides O(1) average lookup from URL → node.
- `find()` performs a hashmap lookup and moves the found node to head (most recent).
- `add_cache_element()` evicts from the tail (O(1)) until enough space, inserts the new node at head and updates the hashmap.
- `remove_cache_element()` pops the tail and removes its hash map entry in O(1).
- All previous time-based LRU fields and scans were removed; eviction is based solely on list recency.

These changes are implemented in `proxy_server_with_cache.c` (cache logic) and the build is handled by the `Makefile`.

## Key Features

- Multithreaded worker-per-connection model using `pthread`s.
- O(1) LRU cache (doubly-linked list + hashmap).
- Mutex-protected cache data structure (`pthread_mutex_t lock`).
- Semaphore-based concurrency limit.
- HTTP GET parsing and forwarding via `proxy_parse.{h,c}`.

## Files of interest

- `proxy_server_with_cache.c` — main proxy + updated cache implementation
- `proxy_parse.c`, `proxy_parse.h` — request parsing helpers
- `Makefile` — build rules (produces `proxy` executable)

## How to build

The project uses the provided `Makefile`. Build with:

```bash
make
```

This produces the `proxy` binary (linking `proxy_server_with_cache.o` and `proxy_parse.o`).

## How to run

Start the proxy (default port 8080) or provide a port:

```bash
./proxy             # listens on 8080 by default (or)
./proxy 8080        # specify port
```

Limit of concurrent clients is controlled by the built-in semaphore.

## Example: curl via proxy

From another terminal, test the proxy with:

```bash
curl -x http://localhost:8080 http://example.com
```

On first request you'll see a cache miss and the proxy will fetch from the remote server and store the response. Subsequent identical requests (same host+path) will be served from cache (cache hit).

Included in this repo is `result_proxy_server.png` which shows a sample `curl -x http://localhost:8080 http://example.com` run and a cache hit on repeat requests.

## Notes and caveats

- The cache size limits remain: `MAX_SIZE` and `MAX_ELEMENT_SIZE` are defined in `proxy_server_with_cache.c`.
- The cache implementation is single-shard and protected by a single mutex — suitable for correctness and clarity; you can extend to sharded locking for higher concurrency.
- Do not modify socket/thread logic unless you need additional features — the README documents the current behavior and the cache changes only.

## Troubleshooting

- If `make` fails, ensure `gcc` and development headers for pthreads are installed.
- If the proxy cannot connect to a remote host, verify network connectivity and DNS resolution from the host running the proxy.

---

If you want, I can also:

- Add the exact terminal transcript text for `curl -x http://localhost:8080 http://example.com` into this README.
- Add a short section describing how to verify cache hits (e.g., repeat curl and watch proxy logs).

Which of these would you like me to add next?

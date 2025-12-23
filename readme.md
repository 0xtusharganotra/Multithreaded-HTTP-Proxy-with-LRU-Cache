# Multithreaded Proxy Server with LRU Cache

## ** Overview**

This project is a high-performance, multithreaded HTTP proxy server written in C. It is designed to handle multiple concurrent client requests efficiently. A key feature of this proxy is its integrated **Least Recently Used (LRU) cache**, which significantly improves response times by storing and serving frequently accessed web pages from memory, reducing the need to fetch them repeatedly from remote servers.

<img width="1023" height="558" alt="image" src="https://github.com/user-attachments/assets/cd4de2ab-c5d6-4995-87a6-f1c638c0df6e" />

## ** Key Features**

- **Multithreaded Architecture:** Uses a thread-per-client model to handle multiple connections simultaneously, ensuring high throughput and responsiveness.
- **LRU Caching Implementation:**
  - Stores HTTP responses in a linked list in memory.
  - Implements an LRU eviction policy to manage cache size. When the cache is full, the least recently accessed item is removed to make space for new content.
  - Cache hits result in immediate data delivery to the client.
- **Thread-Safe Operations:** Utilizes mutex locks (`pthread_mutex_t`) to protect the shared cache data structure from concurrent access conditions, ensuring data integrity.
- **Concurrency Control:** Employs semaphores to limit the number of active threads, preventing server overload.
- **HTTP Request Parsing:** Parses incoming HTTP GET requests to extract the hostname, port, and path.
- **Error Handling:** Generates and sends appropriate HTTP error responses (e.g., 400, 403, 404, 500, 501, 505) to the client.

## ** Technical Architecture**

### **Core Components**

- **Main Thread:** Listens for incoming client connections on a specified port. Upon accepting a connection, it spawns a new worker thread.
- **Worker Threads (`thread_fn`):** Each thread is responsible for handling a single client request. Its tasks include:
  1.  Receiving and parsing the client's HTTP request.
  2.  Checking the LRU cache for the requested URL.
  3.  If found (cache hit), serving the data directly from the cache.
  4.  If not found (cache miss), forwarding the request to the destination server, relaying the response to the client, and storing the response in the cache.
- **LRU Cache:** A linked list data structure protected by a mutex. Each node (`cache_element`) contains:
  - `data`: The cached server response.
  - `url`: The request URL (acts as the key).
  - `lru_time_track`: A timestamp indicating the last access time.
  - `next`: A pointer to the next node in the list.

### **Cache Operations**

- **`find(char* url)`:** Searches the cache for a given URL. If found, it updates the `lru_time_track` to the current time (marking it as most recently used) and returns the cache element.
- **`add_cache_element(char* data, int size, char* url)`:** Adds a new element to the cache. If the cache is full, it calls `remove_cache_element()` to free up space. The new element is always added to the head of the list.
- **`remove_cache_element()`:** Iterates through the cache to find the element with the oldest `lru_time_track` and removes it.

## ** How to Build and Run**

### **Prerequisites**

- GCC Compiler
- Pthread library (usually included with GCC on Linux)

### **Compilation**

To compile the project, run the following command in your terminal:

```bash
gcc -o proxy_server proxy.c -lpthread
```

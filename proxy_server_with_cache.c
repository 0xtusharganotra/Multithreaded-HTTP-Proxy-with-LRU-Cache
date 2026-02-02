#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096
#define MAX_CLIENTS 400
#define MAX_SIZE 200 * (1 << 20)
#define MAX_ELEMENT_SIZE 10 * (1 << 20)

/* ================= CACHE ================= */

typedef struct cache_node
{
	char *url;
	char *data;
	int len;
	struct cache_node *prev, *next;
} cache_node;

typedef struct cache_map_entry
{
	char *url;
	cache_node *node;
	struct cache_map_entry *next;
} cache_map_entry;

#define HASH_MAP_SIZE 1024
static cache_map_entry *cache_map[HASH_MAP_SIZE];
static cache_node *lru_head = NULL;
static cache_node *lru_tail = NULL;
static int cache_size = 0;

pthread_mutex_t lock;

static unsigned int hash_url(const char *url)
{
	unsigned long hash = 5381;
	int c;
	while ((c = *url++))
		hash = ((hash << 5) + hash) + c;
	return hash % HASH_MAP_SIZE;
}

static cache_node *hashmap_get(const char *url)
{
	unsigned int i = hash_url(url);
	for (cache_map_entry *e = cache_map[i]; e; e = e->next)
		if (!strcmp(e->url, url))
			return e->node;
	return NULL;
}

static void hashmap_put(const char *url, cache_node *node)
{
	unsigned int i = hash_url(url);
	for (cache_map_entry *e = cache_map[i]; e; e = e->next)
		if (!strcmp(e->url, url))
		{
			e->node = node;
			return;
		}

	cache_map_entry *e = malloc(sizeof(cache_map_entry));
	e->url = strdup(url);
	e->node = node;
	e->next = cache_map[i];
	cache_map[i] = e;
}

static void hashmap_remove(const char *url)
{
	unsigned int i = hash_url(url);
	cache_map_entry *p = NULL, *c = cache_map[i];
	while (c)
	{
		if (!strcmp(c->url, url))
		{
			if (p)
				p->next = c->next;
			else
				cache_map[i] = c->next;
			free(c->url);
			free(c);
			return;
		}
		p = c;
		c = c->next;
	}
}

static void move_to_front(cache_node *n)
{
	if (!n || n == lru_head)
		return;

	if (n->prev)
		n->prev->next = n->next;
	if (n->next)
		n->next->prev = n->prev;
	if (n == lru_tail)
		lru_tail = n->prev;

	n->prev = NULL;
	n->next = lru_head;
	if (lru_head)
		lru_head->prev = n;
	lru_head = n;
	if (!lru_tail)
		lru_tail = n;
}

cache_node *find(char *url)
{
	pthread_mutex_lock(&lock);
	printf("Looking for: %s\n", url);
	cache_node *n = hashmap_get(url);
	if (n)
	{
		printf("Cache hit for: %s\n", url);
		move_to_front(n);
	}
	pthread_mutex_unlock(&lock);
	return n;
}

void remove_cache_element()
{
	if (!lru_tail)
		return;
	cache_node *t = lru_tail;
	if (t->prev)
		t->prev->next = NULL;
	lru_tail = t->prev;
	if (!lru_tail)
		lru_head = NULL;

	hashmap_remove(t->url);
	cache_size -= (t->len + strlen(t->url) + sizeof(cache_node));
	if (cache_size < 0)
		cache_size = 0;
	free(t->data);
	free(t->url);
	free(t);
}

int add_cache_element(char *data, int size, char *url)
{
	int element_size = size + strlen(url) + sizeof(cache_node);
	if (element_size > MAX_ELEMENT_SIZE)
		return 0;

	pthread_mutex_lock(&lock);
	cache_node *existing = hashmap_get(url);
	if (existing)
	{
		move_to_front(existing);
		pthread_mutex_unlock(&lock);
		return 1;
	}

	while (cache_size + element_size > MAX_SIZE)
		remove_cache_element();

	cache_node *n = malloc(sizeof(cache_node));
	n->data = strdup(data);
	n->url = strdup(url);
	n->len = size;
	n->prev = NULL;
	n->next = lru_head;
	if (lru_head)
		lru_head->prev = n;
	lru_head = n;
	if (!lru_tail)
		lru_tail = n;

	hashmap_put(url, n);
	cache_size += element_size;

	printf("Saved in cache: %s\n", url);
	pthread_mutex_unlock(&lock);
	return 1;
}

/* ================= PROXY ================= */

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(NULL);
	struct tm data = *gmtime(&now);
	strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

	snprintf(str, sizeof(str),
			 "HTTP/1.1 %d Error\r\nContent-Length: 50\r\n\r\nError",
			 status_code);
	send(socket, str, strlen(str), 0);
	return 1;
}

int checkHTTPversion(char *msg)
{
	if (!strncmp(msg, "HTTP/1.1", 8) || !strncmp(msg, "HTTP/1.0", 8))
		return 1;
	return -1;
}

int connectRemoteServer(char *host_addr, int port_num)
{
	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
	struct hostent *host = gethostbyname(host_addr);
	if (!host)
		return -1;

	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);
	bcopy(host->h_addr, &server_addr.sin_addr.s_addr, host->h_length);

	printf("Connecting to %s:%d...\n", host_addr, port_num);
	if (connect(remoteSocket, (struct sockaddr *)&server_addr,
				sizeof(server_addr)) < 0)
		return -1;
	return remoteSocket;
}

int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq)

{

	char *buf = (char *)malloc(sizeof(char) * MAX_BYTES); // creating a buffer where data user request will go bsaically complete http request

	strcpy(buf, "GET ");

	strcat(buf, request->path);

	strcat(buf, " ");

	strcat(buf, request->version);

	strcat(buf, "\r\n");

	size_t len = strlen(buf);

	if (ParsedHeader_set(request, "Connection", "close") < 0)
	{ // this is imprtant here basically we are adding connection:close inside header of request telling the main server to close the connection as soon as you send data

		printf("set header key not work\n");
	}

	if (ParsedHeader_get(request, "Host") == NULL)

	{

		if (ParsedHeader_set(request, "Host", request->host) < 0)
		{

			printf("Set \"Host\" header key not working\n");
		}
	}

	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
	{

		printf("unparse failed\n");

		// return -1; // If this happens Still try to send request without header
	}

	int server_port = 80; // Default Remote Server Port

	if (request->port != NULL)

		server_port = atoi(request->port); // if the port is 8080 then server port will be updated from 80 to 8080

	int remoteSocketID = connectRemoteServer(request->host, server_port); // this is where magic happen we are creating a connection between our proxy and server and this connect Server function basically returns the socketid

	if (remoteSocketID < 0)

		return -1;

	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0); // we are sending the request to the main server from proxy along with the whole request stored inside buf

	bzero(buf, MAX_BYTES); // making buf back to zero cause now it will store the data coming from the server

	bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0); // with this buf will be storing the data send from the server and butes_send will have number of bytes revieved from the server

	char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES); // temp buffer is basically created to copy the whole data that we got from the server

	int temp_buffer_size = MAX_BYTES;

	int temp_buffer_index = 0;

	while (bytes_send > 0) // this loop because we dont reciever whole data from the server all at once we get it in chunks so this way we can send the data to the client in chunks as we recieve it from the server

	{

		bytes_send = send(clientSocket, buf, bytes_send, 0); // sending the data to client as much as we recived

		for (int i = 0; i < bytes_send / sizeof(char); i++)
		{ // storing the data inside temp buffer to store that in LRU CACHE

			temp_buffer[temp_buffer_index] = buf[i];

			// printf("%c",buf[i]); // Response Printing

			temp_buffer_index++;
		}

		temp_buffer_size += MAX_BYTES; // incrementing the size of the temp_buffer so that it can keep on adding the data inside it as soon as we recieve more data as its not a dynamic arry its a static one

		temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

		if (bytes_send < 0)

		{

			perror("Error in sending data to client socket.\n");

			break;
		}

		bzero(buf, MAX_BYTES); // making buff = 0 so that we can recieve next chunk of data from the server

		bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0); // recieving the next chunk from the server and storing it inside empty buff
	}

	temp_buffer[temp_buffer_index] = '\0';

	free(buf);

	char cache_key[2048];
	snprintf(cache_key, sizeof(cache_key), "%s%s", request->host, request->path);
	add_cache_element(temp_buffer, strlen(temp_buffer), cache_key);
	// storiung the data recived from server as whole inside LRU cache

	printf("Done\n");

	free(temp_buffer);

	close(remoteSocketID); // closing the socket b/w proxy and main server

	printf("Finished sending response to client.\n");

	return 0;
}

void *thread_fn(void *socketNew)
{
	sem_wait(&semaphore); // reduces the value of sem by 1 basically act as gate keeper will allow only until value is not negative once its negative will put all the users in waiting queue until some user leave the connection and make its value positive
	int p;
	sem_getvalue(&semaphore, &p); // puuting the value of sem lock in p
	printf("semaphore value:%d\n", p);
	int *t = (int *)(socketNew);
	int socket = *t;			// Socket is socket descriptor of the connected Client bsaically created a copy of socket new that we recieved as arument in thread_fn
	int bytes_send_client, len; // Bytes Transferred , data sent by the client/user or http header is get stored inside bytes_send_client

	char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for each client with initial value as 0 as its calloc not malloc

	bzero(buffer, MAX_BYTES);								// Making buffer zero
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server based on the socket id assigned to the client
	printf("\n--- New request ---\n%s\n", buffer);

	while (bytes_send_client > 0) // now thing is, tcp dont always send the completed data from clients to server so we keep asking for the data until the we see \r\n\r\n in the end
	{
		len = strlen(buffer);
		// loop until u find "\r\n\r\n" in the buffer
		if (strstr(buffer, "\r\n\r\n") == NULL)
		{
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
		}
		else
		{
			break;
		}
	}

	struct ParsedRequest *request = ParsedRequest_create();
	ParsedRequest_parse(request, buffer, strlen(buffer));

	// checking for the request in cache
	char cache_key[2048];
	snprintf(cache_key, sizeof(cache_key), "%s%s", request->host, request->path);
	cache_node *temp = find(cache_key); // we are checking if the data from this req is already there in the cache or not return null if not other wise returns whole data that we can send back to the user

	if (temp != NULL)
	{
		printf("Found in Cache! Serving from cache.\n");
		int size = temp->len / sizeof(char);
		int pos = 0;
		char response[MAX_BYTES];

		while (pos < size)
		{
			bzero(response, MAX_BYTES);

			// Calculate how much to send in THIS specific chunk
			// Is the remaining data bigger than 4KB? Take 4KB.
			// If not, just take whatever is left.
			int chunk_size = MAX_BYTES;
			if (size - pos < MAX_BYTES)
			{
				chunk_size = size - pos;
			}

			// Copy only the valid data
			for (int i = 0; i < chunk_size; i++)
			{
				response[i] = temp->data[pos];
				pos++;
			}

			// IMPORTANT: Only send 'chunk_size', not 'MAX_BYTES' this way we send larger data in chunks or basically blast data to client in chunks not all at once to be safe
			send(socket, response, chunk_size, 0);
		}

		printf("Data retrieved from the Cache\n\n");
	}

	else if (bytes_send_client > 0) // that means data is not present inside cache but we recieved some data from the user
	{
		len = strlen(buffer);
		// Parsing the request
		struct ParsedRequest *request = ParsedRequest_create(); // this will basically breaks the raw string coming from the user and converts into request->host and manly meaningful chunks

		// ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
		//  the request
		if (ParsedRequest_parse(request, buffer, len) < 0) // basically stores the parsed data inside request if valid otherwise return -1
		{
			printf("Parsing failed\n");
		}
		else
		{
			bzero(buffer, MAX_BYTES);			 // cleaned buffer
			if (!strcmp(request->method, "GET")) // our server currently only supports get request any other req like put, post , update , patch will instantly be neglected
			{

				if (request->host && request->path && (checkHTTPversion(request->version) == 1))
				{
					bytes_send_client = handle_request(socket, request, cache_key);
					// Handle GET request
					if (bytes_send_client == -1)
					{
						sendErrorMessage(socket, 500); // no data came from server that means its a issue from server end
					}
				}
				else
					sendErrorMessage(socket, 500); // 500 Internal Error that means issue from proxies end or some issue with users req
			}
			else
			{
				printf("This code doesn't support any method other than GET\n");
			}
		}
		// freeing up the request pointer
		ParsedRequest_destroy(request);
	}

	else if (bytes_send_client < 0) // return -ve when data is not recieved from client because of some error bytes_send_client basically uses recv()
	{
		perror("Error in receiving from client.\n");
	}
	else if (bytes_send_client == 0) // 0 when client is not connected or diconnected without sending and req
	{
		printf("Client disconnected!\n");
	}

	shutdown(socket, SHUT_RDWR); // shut down socket and read write
	close(socket);
	free(buffer);
	sem_post(&semaphore); // basically same as sem_signal increased the value of sem lock so that some other client can use it

	sem_getvalue(&semaphore, &p);
	printf("Semaphore post value:%d\n", p);

	return NULL;
}

int main(int argc, char *argv[])
{

	// argc = argument count
	// argv = argument vector

	int client_socketId, client_len;			 // client_socketId == to store the client socket id or basically the private socket id that spun up with every thread creation this one actually participate in communication
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned , client_addr - client ip , server_addr = combination of your ip , your wifi/network ip address

	sem_init(&semaphore, 0, MAX_CLIENTS); // Initializing seamaphore and lock, here 0 mean semaphore is shared among threads not processes
	pthread_mutex_init(&lock, NULL);	  // Initializing lock for cache

	if (argc == 2) // checking whether two arguments are received or not (./port 8080 so two arg)
	{
		port_number = atoi(argv[1]);
	}
	else // port number is not shared
	{
		printf("Too few arguments\n");
		exit(1);
	}

	printf("Setting Proxy Server Port : %d\n", port_number);

	// creating the proxy socket or basically the main socket id that acts as receptionist
	proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

	// AF_INET = ipv4
	// SOCK_STREAM = tells its a tcp connection
	// 0 - just another protocol

	if (proxy_socketId < 0) // no
	// socket id created
	{
		perror("Failed to create socket.\n");
		exit(1);
	}

	// now below code is interesting tcp basically closes wait for 2,3 minutes even after connection is closed for any remaining data packets transfer so the below statement basically tells tcp to shut up and just close it and reopen instantly when i say
	int reuse = 1;																					   // 1 = true
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) // if not working
		perror("setsockopt(SO_REUSEADDR) failed\n");

	// bzero is basically make there default value 0 as c ususally stores garbage initially

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number); // Assigning port to the Proxy htons basically reverse the bits so that network can understand its port 8080
	server_addr.sin_addr.s_addr = INADDR_ANY;  // INA..Y basically binds our network address or local host address so that request from both can be accepeted

	// Binding the socket
	if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) // if binding of main socket and server addr(port 8080) fails because of some reason like port is in use or something..
	{
		perror("Port is not free\n");
		exit(1);
	}
	printf("Binding on port: %d\n", port_number);

	// Proxy socket listening to the requests
	int listen_status = listen(proxy_socketId, MAX_CLIENTS);

	if (listen_status < 0)
	{
		perror("Error while Listening !\n");
		exit(1);
	}

	int i = 0;							 // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
	int Connected_socketId[MAX_CLIENTS]; // This array stores socket descriptors of connected clients bsaically socket ids that were created when new thread is spun after client is connected and sends request

	// Infinite Loop for accepting connections
	while (1)
	{

		bzero((char *)&client_addr, sizeof(client_addr)); // Clears struct client_addr
		client_len = sizeof(client_addr);				  // set to zero

		// Accepting the connections
		client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len); // Accepts connection and returns a new socket if which is generated from client ip address and length of client address
		if (client_socketId < 0)
		{
			fprintf(stderr, "Error in Accepting connection !\n");
			exit(1);
		}
		else
		{
			Connected_socketId[i] = client_socketId; // Storing accepted client into array
		}

		// This is just to display client socket id , client ip address
		struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
		struct in_addr ip_addr = client_pt->sin_addr;
		char str[INET_ADDRSTRLEN]; // INET_ADDRSTRLEN: Default ip address size
		inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
		printf("Client is connected with port number: %d and ip address: %s \n", ntohs(client_addr.sin_port), str);
		printf("Socket values of index %d in main function is %d\n", i, client_socketId);
		printf("Client connected.\n");

		// spinning up a new thread for every user
		pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]); // Creating a thread for each client accepted
		i++;
	}

	close(proxy_socketId); // Close Main socket after infinite loop ends or basically when server stops running
	return 0;
}

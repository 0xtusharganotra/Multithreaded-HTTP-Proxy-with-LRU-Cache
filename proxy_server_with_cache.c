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
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096					// max allowed size of request/response body
#define MAX_CLIENTS 400					// max number of client requests served at a time ( Concurrent users )
#define MAX_SIZE 200 * (1 << 20)		// size of the cache 200MB
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache 10MB

typedef struct cache_element cache_element;

struct cache_element
{
	char *data;			   // data stores response
	int len;			   // length of data i.e.. sizeof(data)...
	char *url;			   // url stores the request
	time_t lru_time_track; // lru_time_track stores the latest time the element is  accesed
	cache_element *next;   // pointer to next element
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;		// Default Port
int proxy_socketId;			// socket id for main thread
pthread_t tid[MAX_CLIENTS]; // array to store the threads for every new connections
sem_t semaphore;			// if client requests exceeds the max_clients this seamaphore puts the
							// waiting threads to sleep and wakes them when traffic on queue decreases

pthread_mutex_t lock; // lock is used for locking the cache

cache_element *head; // pointer to the cache
int cache_size;		 // cache_size denotes the current size of the cache

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

	switch (status_code)
	{
	case 400:
		snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
		printf("400 Bad Request\n");
		send(socket, str, strlen(str), 0);
		break;

	case 403:
		snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
		printf("403 Forbidden\n");
		send(socket, str, strlen(str), 0);
		break;

	case 404:
		snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
		printf("404 Not Found\n");
		send(socket, str, strlen(str), 0);
		break;

	case 500:
		snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
		// printf("500 Internal Server Error\n");
		send(socket, str, strlen(str), 0);
		break;

	case 501:
		snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
		printf("501 Not Implemented\n");
		send(socket, str, strlen(str), 0);
		break;

	case 505:
		snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
		printf("505 HTTP Version Not Supported\n");
		send(socket, str, strlen(str), 0);
		break;

	default:
		return -1;
	}
	return 1;
}

int checkHTTPversion(char *msg)
{
	int version = -1;

	if (strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if (strncmp(msg, "HTTP/1.0", 8) == 0)
	{
		version = 1; // Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}

int connectRemoteServer(char *host_addr, int port_num)

{

	// Creating Socket for remote server ---------------------------

	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (remoteSocket < 0)

	{

		printf("Error in Creating Socket.\n");

		return -1;
	}

	// Get host by the name or ip address provided

	struct hostent *host = gethostbyname(host_addr);

	if (host == NULL)

	{

		fprintf(stderr, "No such host exists.\n");

		return -1;
	}

	// inserts ip address and port number of host in struct `server_addr`

	struct sockaddr_in server_addr;

	bzero((char *)&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;

	server_addr.sin_port = htons(port_num);

	bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

	// Connect to Remote server ----------------------------------------------------

	if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)

	{

		fprintf(stderr, "Error in connecting !\n");

		return -1;
	}

	// free(host_addr);

	return remoteSocket;
}

int handle_request(int clientSocket, ParsedRequest *request, char *tempReq)

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

	add_cache_element(temp_buffer, strlen(temp_buffer), tempReq); // storiung the data recived from server as whole inside LRU cache

	printf("Done\n");

	free(temp_buffer);

	close(remoteSocketID); // closing the socket b/w proxy and main server

	return 0;
}

void *thread_fn(void *socketNew)
{
	sem_wait(&seamaphore); // reduces the value of sem by 1 basically act as gate keeper will allow only until value is not negative once its negative will put all the users in waiting queue until some user leave the connection and make its value positive
	int p;
	sem_getvalue(&seamaphore, &p); // puuting the value of sem lock in p
	printf("semaphore value:%d\n", p);
	int *t = (int *)(socketNew);
	int socket = *t;			// Socket is socket descriptor of the connected Client bsaically created a copy of socket new that we recieved as arument in thread_fn
	int bytes_send_client, len; // Bytes Transferred , data sent by the client/user or http header is get stored inside bytes_send_client

	char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for each client with initial value as 0 as its calloc not malloc

	bzero(buffer, MAX_BYTES);								// Making buffer zero
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server based on the socket id assigned to the client

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

	char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1); // basically creating a copy of buffer here because we might make changes in buffer later
																	   // tempReq, buffer both store the http request sent by client
	for (int i = 0; i < strlen(buffer); i++)
	{
		tempReq[i] = buffer[i];
	}

	// checking for the request in cache
	struct cache_element *temp = find(tempReq); // we are checking if the data from this req is already there in the cache or not return null if not other wise returns whole data that we can send back to the user

	if (temp != NULL)
	{
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
		ParsedRequest *request = ParsedRequest_create(); // this will basically breaks the raw string coming from the user and converts into request->host and manly meaningful chunks

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
					bytes_send_client = handle_request(socket, request, tempReq); // Handle GET request
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
	sem_post(&seamaphore); // basically same as sem_signal increased the value of sem lock so that some other client can use it

	sem_getvalue(&seamaphore, &p);
	printf("Semaphore post value:%d\n", p);
	free(tempReq);
	return NULL;
}

int main(int argc, char *argv[])
{

	// argc = argument count
	// argv = argument vector

	int client_socketId, client_len;			 // client_socketId == to store the client socket id or basically the private socket id that spun up with every thread creation this one actually participate in communication
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned , client_addr - client ip , server_addr = combination of your ip , your wifi/network ip address

	sem_init(&seamaphore, 0, MAX_CLIENTS); // Initializing seamaphore and lock, here 0 mean semaphore is shared among threads not processes
	pthread_mutex_init(&lock, NULL);	   // Initializing lock for cache

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
			printf(stderr, "Error in Accepting connection !\n");
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

		// spinning up a new thread for every user
		pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]); // Creating a thread for each client accepted
		i++;
	}

	close(proxy_socketId); // Close Main socket after infinite loop ends or basically when server stops running
	return 0;
}

cache_element *find(char *url)
{

	// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
	cache_element *site = NULL;
	// sem_wait(&cache_lock);
	int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
	if (head != NULL)
	{
		site = head;
		while (site != NULL)
		{
			if (!strcmp(site->url, url))
			{
				printf("LRU Time Track Before : %ld", site->lru_time_track);
				printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
			}
			site = site->next;
		}
	}
	else
	{
		printf("\nurl not found\n");
	}
	// sem_post(&cache_lock);
	temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
	return site;
}

void remove_cache_element()
{
	// If cache is not empty searches for the node which has the least lru_time_track and deletes it
	cache_element *p;	 // Cache_element Pointer (Prev. Pointer)
	cache_element *q;	 // Cache_element Pointer (Next Pointer)
	cache_element *temp; // Cache element to remove
	// sem_wait(&cache_lock);
	int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
	if (head != NULL)
	{ // Cache != empty
		for (q = head, p = head, temp = head; q->next != NULL;
			 q = q->next)
		{ // Iterate through entire cache and search for oldest time track
			if (((q->next)->lru_time_track) < (temp->lru_time_track))
			{
				temp = q->next;
				p = q;
			}
		}
		if (temp == head)
		{
			head = head->next; /*Handle the base case*/
		}
		else
		{
			p->next = temp->next;
		}
		cache_size = cache_size - (temp->len) - sizeof(cache_element) -
					 strlen(temp->url) - 1; // updating the cache size
		free(temp->data);
		free(temp->url); // Free the removed element
		free(temp);
	}
	// sem_post(&cache_lock);
	temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

int add_cache_element(char *data, int size, char *url)
{
	// Adds element to the cache
	// sem_wait(&cache_lock);
	int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);
	int element_size = size + 1 + strlen(url) + sizeof(cache_element); // Size of the new element which will be added to the cache
	if (element_size > MAX_ELEMENT_SIZE)
	{
		// sem_post(&cache_lock);
		//  If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
		temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		// free(data);
		// printf("--\n");
		// free(url);
		return 0;
	}
	else
	{
		while (cache_size + element_size > MAX_SIZE)
		{
			// We keep removing elements from cache until we get enough space to add the element
			remove_cache_element();
		}
		cache_element *element = (cache_element *)malloc(sizeof(cache_element)); // Allocating memory for the new cache element
		element->data = (char *)malloc(size + 1);								 // Allocating memory for the response to be stored in the cache element
		strcpy(element->data, data);
		element->url = (char *)malloc(1 + (strlen(url) * sizeof(char))); // Allocating memory for the request to be stored in the cache element (as a key)
		strcpy(element->url, url);
		element->lru_time_track = time(NULL); // Updating the time_track
		element->next = head;
		element->len = size;
		head = element;
		cache_size += element_size;
		temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		// sem_post(&cache_lock);
		//  free(data);
		//  printf("--\n");
		//  free(url);
		return 1;
	}
	return 0;
}
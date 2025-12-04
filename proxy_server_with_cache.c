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

#define MAX_BYTES 4096                  // max allowed size of request/response
#define MAX_CLIENTS 400                 // max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element;

struct cache_element
{
    char *data;            // data stores response
    int len;               // length of data i.e.. sizeof(data)...
    char *url;             // url stores the request
    time_t lru_time_track; // lru_time_track stores the latest time the element is  accesed
    cache_element *next;   // pointer to next element
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;     // Default Port
int proxy_socketId;         // socket id per thread 
pthread_t tid[MAX_CLIENTS]; // array to store the threads/number of new connections
sem_t semaphore;            // if client requests exceeds the max_clients this seamaphore puts the
                            // waiting threads to sleep and wakes them when traffic on queue decreases

pthread_mutex_t lock; // lock is used for locking the cache

cache_element *head; // pointer to the cache
int cache_size;      // cache_size denotes the current size of the cache 

int main(int argc, char * argv[]) {

    //argc = argument count 
    //argv = argument vector 

	int client_socketId, client_len; // client_socketId == to store the client socket id or basically the private socket id that spun up with every thread creation this one actually participate in communication 
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    sem_init(&seamaphore,0,MAX_CLIENTS); // Initializing seamaphore and lock, here 0 mean semaphore is shared among threads not processes 
    pthread_mutex_init(&lock,NULL); // Initializing lock for cache
    

	if(argc == 2)        //checking whether two arguments are received or not (./port 8080 so two arg)
	{
		port_number = atoi(argv[1]);
	}
	else // port number is not shared 
	{
		printf("Too few arguments\n");
		exit(1);
	}

	printf("Setting Proxy Server Port : %d\n",port_number);

    //creating the proxy socket or basically the main socket id that acts as receptionist 
	proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    
    //AF_INET = ipv4 
    //SOCK_STREAM = tells its a tcp connection 
    //0 - just another protocol 

	if( proxy_socketId < 0) // no socket id created 
	{
		perror("Failed to create socket.\n");
		exit(1);
	}

    //now below code is interesting tcp basically closes wait for 2,3 minutes even after connection is closed for any remaining data packets transfer so the bwlo statement basically tells tcp to shut up and just close it and reopen instantly when i say 
	int reuse =1; // 1 = true
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) // if not working
        perror("setsockopt(SO_REUSEADDR) failed\n");


    //bzero is basically make there default value 0 as c ususally stores garbage initially 

	bzero((char*)&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number); // Assigning port to the Proxy htons basically reverse the bits so that network can understand its port 8080 
	server_addr.sin_addr.s_addr = INADDR_ANY; // INA..Y basically binds our network address or local host address so that request from both can be accepeted 

    // Binding the socket
	if( bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 ) // id binding of main socket and server addr fails 
	{
		perror("Port is not free\n");
		exit(1);
	}
	printf("Binding on port: %d\n",port_number);

    // Proxy socket listening to the requests
	int listen_status = listen(proxy_socketId, MAX_CLIENTS);

	if(listen_status < 0 )
	{
		perror("Error while Listening !\n");
		exit(1);
	}
}
CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

all: proxy

proxy: proxy_server_with_cache.o proxy_parse.o
	$(CC) $(CFLAGS) -o proxy proxy_server_with_cache.o proxy_parse.o $(LDFLAGS)

proxy_server_with_cache.o: proxy_server_with_cache.c proxy_parse.h
	$(CC) $(CFLAGS) -c proxy_server_with_cache.c

proxy_parse.o: proxy_parse.c proxy_parse.h
	$(CC) $(CFLAGS) -c proxy_parse.c

clean:
	rm -f proxy *.o

CC = gcc
CFLAGS = -Wall -g -lrt -lpthread

all: server client

server: Server.c
	$(CC) Server.c -o server $(CFLAGS)

client: Client.c
	$(CC) Client.c -o client $(CFLAGS)

clean:
	find . -type f ! -name '*.c' ! -name 'FifoHeader.h' ! -name 'Makefile' -delete
	find . -type d -empty -delete

.PHONY: all clean

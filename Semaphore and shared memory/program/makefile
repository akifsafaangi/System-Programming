CC = gcc
CFLAGS = -Wall -g -lrt -lpthread

all: Main

Main: main.c
	$(CC) main.c -o main $(CFLAGS)


clean:
	find . -type f ! -name '*.c' ! -name 'makefile' -delete

run:
	./main

.PHONY: all clean

CC = gcc
CFLAGS = -Wall

all: clean main

main: main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f main
	find . -type f ! -name 'main.c' ! -name 'makefile' -delete
	rm -f /tmp/fifo1 /tmp/fifo2

run: main
	./main  $(filter-out $@,$(MAKECMDGOALS))

%:
	@:

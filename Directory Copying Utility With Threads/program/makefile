CC = gcc
CFLAGS = -Wall -g -lrt -lpthread
EXECUTABLE = MWCp

all: $(EXECUTABLE)

$(EXECUTABLE): 200104004079_main.c
	$(CC) 200104004079_main.c -o $(EXECUTABLE) $(CFLAGS)

clean:
	find . -type f ! -name '*.c' ! -name 'makefile' ! -name '*.h' -delete

run: $(EXECUTABLE)
	./$(EXECUTABLE) $(filter-out $@,$(MAKECMDGOALS))

%:
	@:

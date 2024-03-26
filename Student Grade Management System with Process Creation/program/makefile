all: program

program: main.o 
	gcc -o main main.o
	
main.o: main.c
	gcc -std=gnu99 -c main.c -o main.o

clean:
	rm -f c $(filter-out main.c makefile, $(wildcard *))

run: 
	./main
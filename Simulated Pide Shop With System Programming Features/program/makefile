CC = gcc
CFLAGS = -Wall -g -lrt -lpthread

all: rn

rn: PideShop.c HungryVeryMuch.c
	$(CC) PideShop.c -o PideShop $(CFLAGS) -lm
	$(CC) HungryVeryMuch.c -o HungryVeryMuch $(CFLAGS)

clean:
	find . -type f ! -name '*.c' ! -name 'makefile' ! -name '*.h' -delete

%:
	@:

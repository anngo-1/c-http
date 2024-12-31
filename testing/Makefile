CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2

all: server-test

server-test: test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f server-test

.PHONY: all clean
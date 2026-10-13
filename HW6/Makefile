# Makefile cho HW6: biên dịch Server và Client TCP theo nhiều module.
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -O2

.PHONY: all clean test submit

all: server client

server: server.o protocol.o md5.o
	$(CC) $(CFLAGS) -o $@ $^

client: client.o protocol.o md5.o
	$(CC) $(CFLAGS) -o $@ $^

server.o: server.c protocol.h md5.h
	$(CC) $(CFLAGS) -c $< -o $@

client.o: client.c protocol.h md5.h
	$(CC) $(CFLAGS) -c $< -o $@

protocol.o: protocol.c protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

md5.o: md5.c md5.h
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	./grade_hw6

clean:
	rm -f server client *.o

submit:
	@echo "Usage: make clean && make; sau do make zip theo yeu cau cua mon hoc."

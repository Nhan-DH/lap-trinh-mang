CC ?= gcc
CFLAGS ?= -D_FILE_OFFSET_BITS=64 -std=c11 -Wall -Wextra -Werror -O2
LDLIBS ?= -lcrypto

COMMON = net.o util.o

.PHONY: all clean asan

all: server client

server: server.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

client: client.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

server.o: server.c proto.h
client.o: client.c proto.h
net.o: net.c proto.h
util.o: util.c proto.h

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
asan: LDLIBS += -fsanitize=address,undefined
asan: clean all

clean:
	rm -f server client *.o

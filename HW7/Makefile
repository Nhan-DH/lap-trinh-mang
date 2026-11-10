## HW7 build rules: server/client d?ng chung net.c va util.c.
CC ?= gcc # Tr?nh bi?n d?ch C s? d?ng tren Linux
CFLAGS ?= -D_FILE_OFFSET_BITS=64 -std=c11 -Wall -Wextra -Werror -O2 # C? bi?n d?ch
LDLIBS ?= -lcrypto # Th? vi?n OpenSSL EVP d?ng cho MD5

COMMON = net.o util.o # Cac object d?ng chung cho server va client

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

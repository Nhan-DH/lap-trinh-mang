CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
LDLIBS  = -lcrypto -pthread

COMMON_SRC = common.h crypto.c net.c

all: server client

server: server.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -o server server.c crypto.c net.c $(LDLIBS)

client: client.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -o client client.c crypto.c net.c $(LDLIBS)

clean:
	rm -f server client

.PHONY: all clean

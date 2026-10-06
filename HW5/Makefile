# Module build của HW5.
# server: liên kết main server với account, session, protocol và crypto.
# client: liên kết main client với module I/O.
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -O2

.PHONY: all clean

all: server client

server: server.c server_account.c server_session.c server_protocol.c server_crypto.c server.h server_crypto.h
	$(CC) $(CFLAGS) -o $@ server.c server_account.c server_session.c server_protocol.c server_crypto.c

client: client.c client_io.c client_io.h
	$(CC) $(CFLAGS) -o $@ client.c client_io.c

clean:
	rm -f server client *.o

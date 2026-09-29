CC = gccCFLAGS = -Wall -Wextra -O2

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client *.o

.PHONY: all clean
-f $(SERVER) $(CLIENT) *.o

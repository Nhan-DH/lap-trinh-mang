# Makefile - HW4 UDP Chat (IT4062) - moi chuong trinh la 1 file .c doc lap
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_GNU_SOURCE -O2

all: server client

server: server.c
	$(CC) $(CFLAGS) -o $@ $<

client: client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f server client *.o *_HW4.zip

# Dong goi bai nop: make zip NAME=NguyenVanA MSSV=20161234
zip: clean
	@if [ -z "$(NAME)" ] || [ -z "$(MSSV)" ]; then \
		echo "Usage: make zip NAME=NguyenVanA MSSV=20161234"; exit 1; fi
	zip $(NAME)_$(MSSV)_HW4.zip *.c Makefile

.PHONY: all clean zip

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L \
	$(shell xml2-config --cflags)
LDLIBS = $(shell xml2-config --libs) -lcurl

TARGET = resolver
SRCS = main.c resolver.c crawler.c crawler_memory.c \
	crawler_download.c crawler_parse.c crawler_output.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.c resolver.h crawler.h crawler_internal.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) links.csv texts.csv videos.csv

.PHONY: all clean

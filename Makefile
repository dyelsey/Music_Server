TARGETS=server

CFLAGS=-Wall -g -O0

all: $(TARGETS)

server: server.c
	gcc $(CFLAGS) -o server server.c

clean:
	rm -f $(TARGETS)

CC = gcc
CFLAGS =
all: server client

server:
	$(CC) -o Server server.c socket_comms.c -lpthread -lm $(CFLAGS)

client:
	$(CC) -o Client client.c socket_comms.c $(CFLAGS)

clean:
	rm -f Client
	rm -f Server

.PHONY: all server client clean 
CC = gcc
CFLAGS = -Wall
LDLIBS = -lcrypto
LDFLAGS = -lpthread

all: client server

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

client: client.o
	$(CC) client.o $(LDLIBS) -o client

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

server: server.o
	$(CC) server.o $(LDLIBS) $(LDFLAGS) -o server

clean:
	rm -f *.o client server

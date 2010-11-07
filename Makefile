.PHONY: all clean tags

CFLAGS:=-Wall -O2

all: client server

server: server.c
	$(CC) $(CFLAGS) $^ -o $@
	strip -s $@

client: client.c
	$(CC) $(CFLAGS) $^ -o $@
	strip -s $@

tags:
	ctags -R .

clean:
	-rm *.o client server

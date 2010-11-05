.PHONY: all clean

CFLAGS:=-Wall -O2

all: client

client: client.c
	$(CC) $(CFLAGS) $^ -o $@
	strip -s $@

clean:
	-rm *.o client server

CC = gcc


CFLAGS = -Wall -g

LDFLAGS = -lpthread


all: emisor receptor


emisor: emisor.c protocolo.h
	$(CC) $(CFLAGS) -o emisor emisor.c


receptor: receptor.c protocolo.h
	$(CC) $(CFLAGS) -o receptor receptor.c $(LDFLAGS)

clean:
	rm -f emisor receptor *.o *.dat

.PHONY: all clean

## Created by Anjuta

CC = gcc
CFLAGS = -g -Wall -O3 -I/usr/include/lua5.1
OBJECTS = fakesae_queue.o
INCFLAGS = 
LDFLAGS = -Wl,-rpath,/usr/local/lib
LIBS = -lhiredis -lpthread -lcurl -llua5.1 -lm
PROG = fakesae_queue

all: fakesae_queue

fakesae_queue: $(OBJECTS)
	$(CC) -o $(PROG) $(OBJECTS) $(LDFLAGS) $(LIBS)

fakesae_queue.o: fakesae_queue.c

count:
	wc *.c *.cc *.C *.cpp *.h *.hpp

clean:
	rm -f *.o fakesae_queue *~

.PHONY: all
.PHONY: count
.PHONY: clean

CFLAGS  = -c -Wall -s -O3 -march=native -std=gnu99 -D_GNU_SOURCE -funroll-loops 
LDFLAGS = -flto -lm -s
TARGETS = pi_sieve
SOURCES = pi_sieve.c
OBJECTS = $(SOURCES:.c=.o)
CC      = gcc

all: $(SOURCES) $(TARGETS)

$(TARGETS): $(OBJECTS) Makefile
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

pi_sieve.o: pi_sieve.c Makefile
	$(CC) $(CFLAGS) pi_sieve.c -o pi_sieve.o

clean:
	rm -f pi_sieve.o
	rm -f pi_sieve
	rm -f sieve_progress.bin
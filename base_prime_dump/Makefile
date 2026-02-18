CFLAGS  = -c -Wall -s -O3 -march=native -std=gnu99 -D_GNU_SOURCE -funroll-loops
LDFLAGS = -flto -lm -s
TARGETS = base_prime_dump
SOURCES = base_prime_dump.c
OBJECTS = $(SOURCES:.c=.o)
CC      = gcc

all: $(SOURCES) $(TARGETS)

$(TARGETS): $(OBJECTS) Makefile
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

base_prime_dump.o: base_prime_dump.c
	$(CC) $(CFLAGS) base_prime_dump.c -o base_prime_dump.o

clean:
	rm -f base_prime_dump.o
	rm -f base_prime_dump

SOURCES=$(wildcard *.c) $(wildcard ds/*.c)
OBJECTS=$(SOURCES:%.c=%.o)
CFLAGS=-Wall -Werror -g -ggdb3 -O0
BINARY=pcfsh

.PHONY: clean

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

all: $(OBJDIR) $(BINARY)

clean:
	rm $(BINARY)
	rm $(OBJECTS)

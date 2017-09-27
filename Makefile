SOURCES=$(wildcard *.c) $(wildcard ds/*.c)
OBJECTS=$(SOURCES:%.c=%.o)
CFLAGS=-Wall -Werror -ggdb
BINARY=pcfsh

.PHONY: clean

%.o: %.c
	$(CC) -c $^ -o $@

$(BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

all: $(OBJDIR) $(BINARY)

clean:
	rm $(BINARY)
	rm $(OBJECTS)

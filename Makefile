SOURCES=$(wildcard *.c) $(wildcard ds/*.c)
OBJECTS=$(SOURCES:%.c=%.o)
CFLAGS=-Wall -Werror -g -ggdb3 -O0
BINARY=shell

.PHONY: clean all run run_valgrind

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

run: all
	exec ./$(BINARY)

run_valgrind: all
	mkdir -p debug
	valgrind --log-file="debug/$(BINARY).mem.%p" --leak-check=full --show-leak-kinds=all ./shell

all: $(OBJDIR) $(BINARY)

clean:
	rm $(BINARY)
	rm $(OBJECTS)

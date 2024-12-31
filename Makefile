CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
DEBUG_FLAGS = -g -DDEBUG

TARGET = server
DEBUG_TARGET = server-debug

SRC = server.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET) $(DEBUG_TARGET) *.o core

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)


uninstall:
	rm -f /usr/local/bin/$(TARGET)


run: $(TARGET)
	./$(TARGET)


memcheck: $(DEBUG_TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(DEBUG_TARGET)


compile_commands:
	bear -- make clean all

.PHONY: all debug clean install uninstall run memcheck compile_commands
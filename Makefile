CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
TARGET := build/library-management-system
SOURCE := main.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	rm -rf build

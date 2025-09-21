CC = gcc
CFLAGS = -g -Wall -Wextra
LDFLAGS = -lpthread
TARGET = huffman
SOURCES = huffman.c utf8decode.c
HEADERS = utf8decode.h

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild
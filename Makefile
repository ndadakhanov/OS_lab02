CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O3 -pthread

TARGET = batcher_sort

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
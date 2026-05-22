CC := gcc
CFLAGS := -O2 -Wall -Wextra -Wpedantic -std=c11
TARGET := parta_bench

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) *.o *.csv *.tmp

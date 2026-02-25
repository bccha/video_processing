TARGET1 = st2110_rx
TARGET2 = st2110_mmap_rx
CC = gcc
CFLAGS = -Wall -std=gnu99 -O2

all: $(TARGET1) $(TARGET2)

$(TARGET1): st2110_rx.c
	$(CC) $(CFLAGS) -o $(TARGET1) st2110_rx.c

$(TARGET2): st2110_mmap_rx.c
	$(CC) $(CFLAGS) -o $(TARGET2) st2110_mmap_rx.c

clean:
	rm -f $(TARGET1) $(TARGET2)

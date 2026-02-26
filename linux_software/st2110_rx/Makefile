TARGET_RX_RAW = st2110_rx
TARGET_RX_MMAP = st2110_mmap_rx
TARGET_TX_STATIC = st2110_static_tx

CC = gcc
CFLAGS = -Wall -std=gnu99 -O2

COMMON_OBJ = st2110_common.o

all: $(TARGET_RX_RAW) $(TARGET_RX_MMAP) $(TARGET_TX_STATIC)

$(COMMON_OBJ): st2110_common.c st2110_common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_RX_RAW): st2110_rx.c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET_RX_MMAP): st2110_mmap_rx.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

$(TARGET_TX_STATIC): st2110_static_tx.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

clean:
	rm -f $(TARGET_RX_RAW) $(TARGET_RX_MMAP) $(TARGET_TX_STATIC) *.o

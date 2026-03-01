TARGET_RX_RAW = st2110_rx
TARGET_RX_MMAP = st2110_mmap_rx
TARGET_RX_HW = st2110_hw_rx
TARGET_TX_STATIC = st2110_static_tx
TARGET_STEP1 = st2110_step1_udp
TARGET_STEP2 = st2110_step2_dma

CFLAGS = -Wall -std=gnu99 -O2 -lpthread -pthread

COMMON_OBJ = st2110_common.o

all: $(TARGET_RX_RAW) $(TARGET_RX_MMAP) $(TARGET_RX_HW) $(TARGET_TX_STATIC) $(TARGET_STEP1) $(TARGET_STEP2)

$(COMMON_OBJ): st2110_common.c st2110_common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_RX_RAW): st2110_rx.c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET_RX_MMAP): st2110_mmap_rx.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ) -lpthread

$(TARGET_RX_HW): st2110_hw_rx.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

$(TARGET_TX_STATIC): st2110_static_tx.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

$(TARGET_STEP1): st2110_step1_udp.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

$(TARGET_STEP2): st2110_step2_dma.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON_OBJ)

clean:
	rm -f $(TARGET_RX_RAW) $(TARGET_RX_MMAP) $(TARGET_RX_HW) $(TARGET_TX_STATIC) $(TARGET_STEP1) $(TARGET_STEP2) *.o

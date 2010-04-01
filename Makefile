CFLAGS=-std=c99 -O2 -Wall
LDFLAGS=-s -lfuse

bitsfs: bitsfs.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(BITSFLAGS) -o $@ $<

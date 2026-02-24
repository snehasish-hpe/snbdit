# Direct I/O Pattern Test
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -D_GNU_SOURCE
TARGET  = snb_dit
SRC     = snb_dit.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean_direct:
	rm -f $(TARGET)

.PHONY: clean_direct

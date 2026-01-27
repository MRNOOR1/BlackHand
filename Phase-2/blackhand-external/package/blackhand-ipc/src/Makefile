# Simple build for IPC library.
SRCS = ipc.c

CC ?= $(CROSS_COMPILE)gcc
AR ?= $(CROSS_COMPILE)ar
CFLAGS ?= -O2 -Wall -Wextra -fPIC

all:
	$(CC) $(CFLAGS) -c ipc.c -o ipc.o
	$(AR) rcs libblackhand-ipc.a ipc.o

clean:
	rm -f ipc.o libblackhand-ipc.a

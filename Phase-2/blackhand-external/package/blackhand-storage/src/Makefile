# Build for storage service.
SRCS = storage_service.c

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra
LIBS ?= -lsqlite3

all:
	$(CC) $(CFLAGS) -o blackhand-storage $(SRCS) $(LIBS)

clean:
	rm -f blackhand-storage

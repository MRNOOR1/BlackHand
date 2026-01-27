# Simple build for the init binary.
# Add source files to SRCS as you implement features.
SRCS = init.c service_manager.c

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra

all:
	$(CC) $(CFLAGS) -o blackhand-init $(SRCS)

clean:
	rm -f blackhand-init

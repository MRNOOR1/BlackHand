# Build for STT service.
SRCS = stt_service.c

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra

all:
	$(CC) $(CFLAGS) -o blackhand-stt $(SRCS)

clean:
	rm -f blackhand-stt

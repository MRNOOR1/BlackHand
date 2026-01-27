# Build for modem service.
SRCS = modem_service.c at_parser.c sms.c

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra

all:
	$(CC) $(CFLAGS) -o blackhand-modem $(SRCS)

clean:
	rm -f blackhand-modem

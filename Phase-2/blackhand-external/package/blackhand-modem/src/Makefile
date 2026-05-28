# ============================================================================
# blackhand-modem — Makefile (Phase 3)
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc -lpthread

SRCS = modem_service.c ipc_dispatch.c at_parser.c sms.c \
       modem_uart.c modem_core.c

all:
	$(CC) $(CFLAGS) -o blackhand-modem $(SRCS) $(LIBS)

clean:
	rm -f blackhand-modem

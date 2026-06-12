# ============================================================================
# blackhand-ipc — static library Makefile
#
# Compiles all IPC source files into object files then archives them into
# libblackhand-ipc.a. Other packages (blackhand-audio, blackhand-ui etc)
# link against this library to get send_msg, recv_msg, dispatch and all
# other IPC functions without duplicating code.
#
# CC and AR use $(CROSS_COMPILE) prefix so Buildroot can inject the correct
# cross-compiler for the Raspberry Pi target automatically.
#
# Usage:
#   make          — build libblackhand-ipc.a
#   make clean    — remove all build artifacts
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
AR     ?= $(CROSS_COMPILE)ar
CFLAGS ?= -O2 -Wall -Wextra -fPIC

# all source files that make up the library.
# NOTE: ipc_dispatch.c is NOT included here. The dispatch table is
# service-specific — each service (blackhand-audio, blackhand-storage etc)
# compiles its own ipc_dispatch.c with its own handler table. The library
# only provides the transport layer: framing, send/recv, and cJSON.
SRCS = ipc.c \
       country_codes.c \
       cJSON.c

# derive object file list from source list
# replaces .c extension with .o for each entry in SRCS
OBJS = $(SRCS:.c=.o)

# default target — build the static library
all: libblackhand-ipc.a

# archive all object files into the static library
libblackhand-ipc.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# pattern rule — compiles any .c file into a .o file
# $< is the source file, $@ is the output file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# remove all build artifacts for a clean rebuild
clean:
	rm -f $(OBJS) libblackhand-ipc.a

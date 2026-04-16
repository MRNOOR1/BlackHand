# ============================================================================
# blackhand-init — Makefile
#
# Produces TWO separate binaries from two separate source files:
#
#   blackhand-init    — compiled from init.c only.
#                       Installed to /sbin/blackhand-init by blackhand-init.mk.
#                       The kernel executes this as PID 1 on boot.
#
#   blackhand-svc-mgr — compiled from service_manager.c only.
#                       Installed to /usr/bin/blackhand-svc-mgr by blackhand-init.mk.
#                       Spawned by PID 1 as the first child process.
#
# They are separate binaries because they run as separate processes. PID 1
# (blackhand-init) forks and execs blackhand-svc-mgr — you cannot fork/exec
# yourself, so they must be distinct executables.
#
# CC uses $(CROSS_COMPILE) prefix so Buildroot can inject the ARM cross-
# compiler (aarch64-buildroot-linux-uclibc-gcc) for the Pi 4 target.
#
# Usage:
#   make          — build both binaries
#   make clean    — remove both binaries
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra

# default target — build both binaries
all: blackhand-init blackhand-svc-mgr

# PID 1 binary — compiled from init.c alone
blackhand-init: init.c
	$(CC) $(CFLAGS) -o $@ $<

# service manager binary — compiled from service_manager.c alone
blackhand-svc-mgr: service_manager.c
	$(CC) $(CFLAGS) -o $@ $<

# remove both binaries for a clean rebuild
clean:
	rm -f blackhand-init blackhand-svc-mgr

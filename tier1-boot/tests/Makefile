# Tier 1 host tests: service restart policy shared by init and svc-mgr.
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
INIT = ../blackhand-init/src
BUILD_DIR = build

.PHONY: check clean
check: $(BUILD_DIR)/restart-policy.bin
	$(BUILD_DIR)/restart-policy.bin

$(BUILD_DIR)/restart-policy.bin: $(INIT)/selftest.c $(INIT)/restart_policy.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wno-unused-function $< -o $@

clean:
	rm -rf $(BUILD_DIR)

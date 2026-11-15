# Tier 4 host tests: UI services that need no screen (PIN, notes).
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
UI = ../blackhand-ui/src
IPC = ../../tier3-plumbing/blackhand-ipc/src
CPPFLAGS += -I$(IPC) -I$(UI) -I$(UI)/services
CRYPTO_CFLAGS = $(shell pkg-config --cflags libcrypto)
CRYPTO_LIBS = $(shell pkg-config --libs libcrypto)
BUILD_DIR = build
BINS = $(addprefix $(BUILD_DIR)/,pin.bin notes.bin)

.PHONY: check clean
check: $(BINS)
	@set -e; for t in $(BINS); do "$$t"; done

$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/pin.bin: test_pin.c $(UI)/services/pin_service.c $(UI)/services/file_store.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CRYPTO_CFLAGS) '-DPIN_FILE="pin.conf"' $^ $(CRYPTO_LIBS) -o $@
$(BUILD_DIR)/notes.bin: test_notes.c $(UI)/services/notes_service.c $(UI)/services/file_store.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) '-DAPP_PATH_NOTES_DIR="notes"' $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

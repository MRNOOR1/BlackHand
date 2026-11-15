# Tier 3 host tests: IPC framing, cJSON, country codes, storage.
# Never touches a real /data.
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
IPC = ../blackhand-ipc/src
CPPFLAGS += -I$(IPC)
BUILD_DIR = build
BINS = $(addprefix $(BUILD_DIR)/,core.bin ipc-failure.bin storage.bin)

.PHONY: check clean
check: $(BINS)
	@set -e; for t in $(BINS); do "$$t"; done

$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/core.bin: test_blackhand.c $(IPC)/ipc.c $(IPC)/country_codes.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/ipc-failure.bin: test_ipc_failure.c $(IPC)/ipc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
# test_storage_wipe.c #includes storage_service.c to reach its static functions.
$(BUILD_DIR)/storage.bin: test_storage_wipe.c $(IPC)/ipc.c $(IPC)/cJSON.c ../blackhand-storage/src/storage_service.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) test_storage_wipe.c $(IPC)/ipc.c $(IPC)/cJSON.c -o $@

clean:
	rm -rf $(BUILD_DIR)

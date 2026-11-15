# Tier 5 host tests: modem history writes, SMS draft, GPS parsing, WAV writer.
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
IPC = ../../tier3-plumbing/blackhand-ipc/src
UI = ../../tier4-ui/blackhand-ui/src
MODEM = ../blackhand-modem/src
AUDIO = ../blackhand-audio/src
CPPFLAGS += -I$(IPC) -I$(UI) -I$(UI)/services -I$(MODEM) -I$(AUDIO)
BUILD_DIR = build
BINS = $(addprefix $(BUILD_DIR)/,modem-storage.bin draft.bin gps.bin wav.bin)

.PHONY: check clean
check: $(BINS)
	@set -e; for t in $(BINS); do "$$t"; done

$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/modem-storage.bin: test_modem_storage.c $(MODEM)/modem_storage.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/draft.bin: test_draft.c $(UI)/services/draft_service.c $(UI)/services/file_store.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) '-DSMS_DRAFT_PATH="draft.json"' $^ -o $@
$(BUILD_DIR)/gps.bin: test_gps.c $(MODEM)/gps_service.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/wav.bin: test_wav_file.c $(AUDIO)/wav_file.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

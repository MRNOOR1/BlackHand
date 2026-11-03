# Host regression tests against production sources; never exercise real /data.
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
BUILD_DIR ?= build
IPC = ../blackhand-external/package/blackhand-ipc/src
UI = ../blackhand-external/package/blackhand-ui/src
MODEM = ../blackhand-external/package/blackhand-modem/src
AUDIO = ../blackhand-external/package/blackhand-audio/src
CPPFLAGS += -I$(IPC) -I$(UI) -I$(UI)/services -I$(MODEM) -I$(AUDIO)
CRYPTO_CFLAGS = $(shell pkg-config --cflags libcrypto)
CRYPTO_LIBS = $(shell pkg-config --libs libcrypto)
MEDIA_CFLAGS = $(shell pkg-config --cflags notcurses libmpg123 libout123)
MEDIA_LIBS = $(shell pkg-config --libs libmpg123 libout123)
BINS = $(addprefix $(BUILD_DIR)/,core.bin notes.bin ipc-failure.bin storage.bin modem-storage.bin wav.bin draft.bin pin.bin gps.bin)
.PHONY: check check-media clean
check: $(BINS)
	@set -e; for test in $(BINS); do "$$test"; done
check-media: $(BUILD_DIR)/mp3.bin
	$(BUILD_DIR)/mp3.bin
$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/core.bin: test_blackhand.c $(IPC)/ipc.c $(IPC)/country_codes.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/notes.bin: test_notes.c $(UI)/services/notes_service.c $(UI)/services/file_store.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) '-DAPP_PATH_NOTES_DIR="notes"' $^ -o $@
$(BUILD_DIR)/ipc-failure.bin: test_ipc_failure.c $(IPC)/ipc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/storage.bin: test_storage_wipe.c $(IPC)/ipc.c $(IPC)/cJSON.c ../blackhand-external/package/blackhand-storage/src/storage_service.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) test_storage_wipe.c $(IPC)/ipc.c $(IPC)/cJSON.c -o $@
$(BUILD_DIR)/modem-storage.bin: test_modem_storage.c $(MODEM)/modem_storage.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/wav.bin: test_wav_file.c $(AUDIO)/wav_file.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/draft.bin: test_draft.c $(UI)/services/draft_service.c $(UI)/services/file_store.c $(IPC)/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) '-DSMS_DRAFT_PATH="draft.json"' $^ -o $@
$(BUILD_DIR)/pin.bin: test_pin.c $(UI)/services/pin_service.c $(UI)/services/file_store.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CRYPTO_CFLAGS) '-DPIN_FILE="pin.conf"' $^ $(CRYPTO_LIBS) -o $@
$(BUILD_DIR)/gps.bin: test_gps.c $(MODEM)/gps_service.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@
$(BUILD_DIR)/mp3.bin: test_mp3_failure.c $(UI)/services/mp3_service.c $(UI)/services/audio_policy.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MEDIA_CFLAGS) -DBLACKHAND_MP3_ENABLED $^ $(MEDIA_LIBS) -lpthread -lm -o $@
clean:
	rm -f $(BINS) $(BUILD_DIR)/mp3.bin

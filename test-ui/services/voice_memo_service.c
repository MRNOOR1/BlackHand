#include "voice_memo_service.h"
#include "../config.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "settings_service.h"

#define INITIAL_MEMO_CAPACITY 16
#define TICK_STEP_MS 33
#define WAV_SAMPLE_RATE 16000
#define WAV_CHANNELS 1
#define WAV_BITS_PER_SAMPLE 16

static VoiceMemo **memos_index = NULL;
static size_t memos_count = 0;
static size_t memos_capacity = 0;

static VMState current_state = VM_IDLE;
static VoiceMemo *current_memo = NULL;
static int current_elapsed_ms = 0;
static unsigned memo_serial = 0;
static VMPlayMode play_mode = VM_PLAYMODE_NORMAL;
static int play_speed_percent = 100;

static pid_t io_pid = -1;
static int io_is_recording = 0;
static int io_paused = 0;
static int elapsed_before_pause_ms = 0;
static int64_t active_started_ms = 0;
static int backend_record_supported = 0;
static int backend_play_supported = 0;
static char active_record_filename[160] = {0};

static const char *VOICE_MEMO_PATH = APP_PATH_VOICE_MEMOS_DIR;

static int64_t monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

static int timed_elapsed_ms(void)
{
	if (current_state != VM_RECORDING && current_state != VM_PLAYING)
		return elapsed_before_pause_ms;
	int64_t now = monotonic_ms();
	int64_t delta = now - active_started_ms;
	if (delta < 0)
		delta = 0;
	if (current_state == VM_PLAYING) {
		delta = (delta * play_speed_percent) / 100;
	}
	return elapsed_before_pause_ms + (int)delta;
}

static int ensure_capacity(void)
{
	if (memos_count < memos_capacity)
		return 0;
	size_t new_capacity = (memos_capacity == 0) ? INITIAL_MEMO_CAPACITY : memos_capacity * 2;
	VoiceMemo **new_array = realloc(memos_index, new_capacity * sizeof(VoiceMemo *));
	if (!new_array)
		return -1;
	memos_index = new_array;
	memos_capacity = new_capacity;
	return 0;
}

static void insert_at_front(VoiceMemo *memo)
{
	for (size_t i = memos_count; i > 0; i--)
	{
		memos_index[i] = memos_index[i - 1];
	}
	memos_index[0] = memo;
	memos_count++;
}

static char *make_timestamp_filename(void)
{
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);

	char name[64];
	strftime(name, sizeof(name), "memo_%Y%m%d_%H%M%S", &tmv);
	char final_name[80];
	snprintf(final_name, sizeof(final_name), "%s_%u.wav", name, memo_serial++);
	return strdup(final_name);
}

static void write_u16le(FILE *f, uint16_t v) {
    unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}

static void write_u32le(FILE *f, uint32_t v) {
    unsigned char b[4] = {
        (unsigned char)(v & 0xFF),
        (unsigned char)((v >> 8) & 0xFF),
        (unsigned char)((v >> 16) & 0xFF),
        (unsigned char)((v >> 24) & 0xFF)
    };
    fwrite(b, 1, 4, f);
}

static int write_silence_wav(const char *path, int duration_ms) {
    if (!path || duration_ms < 0) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    const uint16_t channels = WAV_CHANNELS;
    const uint32_t sample_rate = WAV_SAMPLE_RATE;
    const uint16_t bits_per_sample = WAV_BITS_PER_SAMPLE;
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = sample_rate * block_align;

    uint32_t frames = (uint32_t)(((int64_t)duration_ms * sample_rate) / 1000);
    uint32_t data_bytes = frames * block_align;
    uint32_t riff_size = 36 + data_bytes;

    fwrite("RIFF", 1, 4, f);
    write_u32le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32le(f, 16);
    write_u16le(f, 1);
    write_u16le(f, channels);
    write_u32le(f, sample_rate);
    write_u32le(f, byte_rate);
    write_u16le(f, block_align);
    write_u16le(f, bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32le(f, data_bytes);

    if (data_bytes > 0) {
        unsigned char zero[1024] = {0};
        uint32_t remaining = data_bytes;
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(zero) ? (uint32_t)sizeof(zero) : remaining;
            fwrite(zero, 1, chunk, f);
            remaining -= chunk;
        }
    }

    fclose(f);
    return 0;
}

static int read_wav_duration_ms(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned char hdr[44];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    if (memcmp(hdr + 0, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return 0;
    }

    uint32_t byte_rate = (uint32_t)hdr[28] |
                         ((uint32_t)hdr[29] << 8) |
                         ((uint32_t)hdr[30] << 16) |
                         ((uint32_t)hdr[31] << 24);

    uint32_t data_bytes = (uint32_t)hdr[40] |
                          ((uint32_t)hdr[41] << 8) |
                          ((uint32_t)hdr[42] << 16) |
                          ((uint32_t)hdr[43] << 24);

    if (byte_rate == 0) return 0;
    return (int)((data_bytes * 1000U) / byte_rate);
}

static int make_filename_from_title(const char *title, char *out, size_t out_size) {
    if (!title || !out || out_size < 8) return -1;

    size_t j = 0;
    int last_underscore = 0;
    for (size_t i = 0; title[i] != '\0' && j + 5 < out_size; i++) {
        unsigned char ch = (unsigned char)title[i];
        if (isalnum(ch)) {
            out[j++] = (char)tolower(ch);
            last_underscore = 0;
        } else if (ch == ' ' || ch == '-' || ch == '_') {
            if (!last_underscore && j > 0) {
                out[j++] = '_';
                last_underscore = 1;
            }
        }
    }

    while (j > 0 && out[j - 1] == '_') j--;
    if (j == 0) return -1;

    out[j] = '\0';
    strncat(out, ".wav", out_size - strlen(out) - 1);
    return 0;
}

static int path_exists(const char *p) {
    return access(p, F_OK) == 0;
}

static int spawn_arecord(const char *out_path)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		if (settings_service_get_bool(SETTINGS_KEY_AUX_INPUT))
			execlp("arecord", "arecord", "-q", "-D", "hw:1,0", "-f", "S16_LE", "-c", "1", "-r", "16000", out_path, (char *)NULL);
		execlp("arecord", "arecord", "-q", "-f", "S16_LE", "-c", "1", "-r", "16000", out_path, (char *)NULL);
		_exit(127);
	}
	io_pid = pid;
	io_is_recording = 1;
	return 0;
}

static int spawn_aplay(const char *in_path)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		execlp("aplay", "aplay", "-q", in_path, (char *)NULL);
		_exit(127);
	}
	io_pid = pid;
	io_is_recording = 0;
	return 0;
}

static void stop_child_process(int sig)
{
	if (io_pid <= 0)
		return;
	kill(io_pid, sig);
	waitpid(io_pid, NULL, 0);
	io_pid = -1;
	io_paused = 0;
	io_is_recording = 0;
}

void voice_memo_service_init(void)
{
	memos_index = malloc(INITIAL_MEMO_CAPACITY * sizeof(VoiceMemo *));
	if (!memos_index)
		return;

	memos_count = 0;
	memos_capacity = INITIAL_MEMO_CAPACITY;
	current_state = VM_IDLE;
	current_memo = NULL;
	current_elapsed_ms = 0;
	play_mode = VM_PLAYMODE_NORMAL;
	play_speed_percent = 100;
	io_pid = -1;
	io_is_recording = 0;
	io_paused = 0;
	elapsed_before_pause_ms = 0;
	active_started_ms = 0;
	backend_record_supported = (access("/usr/bin/arecord", X_OK) == 0 || access("/bin/arecord", X_OK) == 0);
	backend_play_supported = (access("/usr/bin/aplay", X_OK) == 0 || access("/bin/aplay", X_OK) == 0);

	struct stat st = {0};
	if (stat(VOICE_MEMO_PATH, &st) == -1)
	{
		if (mkdir(VOICE_MEMO_PATH, 0755) != 0)
		{
			free(memos_index);
			memos_index = NULL;
			memos_capacity = 0;
			memos_count = 0;
			return;
		}
	}

	DIR *dir = opendir(VOICE_MEMO_PATH);
	if (!dir)
	{
		free(memos_index);
		memos_index = NULL;
		memos_capacity = 0;
		memos_count = 0;
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_name[0] == '.')
			continue;
		const char *ext = strrchr(entry->d_name, '.');
		if (!ext || strcmp(ext, ".wav") != 0)
			continue;

		if (ensure_capacity() != 0)
			continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, entry->d_name);
		VoiceMemo *memo = malloc(sizeof(VoiceMemo));
		if (!memo) continue;

		memo->filename = strdup(entry->d_name);
		memo->duration_ms = read_wav_duration_ms(path);

		if (!memo->filename)
		{
			free(memo);
			continue;
		}
		insert_at_front(memo);
	}

	closedir(dir);
}

VMState voice_memo_service_state(void)
{
	return current_state;
}

const VoiceMemo *voice_memo_service_current(void)
{
	return current_memo;
}

const VoiceMemo **voice_memo_service_list_all(size_t *out_count)
{
	if (out_count)
		*out_count = memos_count;
	return (const VoiceMemo **)memos_index;
}

const VoiceMemo *voice_memo_service_get_by_filename(const char *filename)
{
	if (!filename)
		return NULL;
	for (size_t i = 0; i < memos_count; i++)
	{
		if (strcmp(memos_index[i]->filename, filename) == 0)
		{
			return memos_index[i];
		}
	}
	return NULL;
}

int voice_memo_service_record_start(void)
{
	if (current_state != VM_IDLE)
		return -1;
	if (io_pid > 0)
		return -1;

	char *fname = make_timestamp_filename();
	if (!fname)
		return -1;
	snprintf(active_record_filename, sizeof(active_record_filename), "%s", fname);
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, fname);
	free(fname);

	if (backend_record_supported) {
		if (spawn_arecord(path) != 0) {
			io_pid = -1;
			io_is_recording = 0;
		}
	}
	current_state = VM_RECORDING;
	current_memo = NULL;
	current_elapsed_ms = 0;
	elapsed_before_pause_ms = 0;
	active_started_ms = monotonic_ms();
	return 0;
}

int voice_memo_service_record_pause(void)
{
	if (current_state != VM_RECORDING)
		return -1;
	elapsed_before_pause_ms = timed_elapsed_ms();
	if (io_pid > 0)
	{
		kill(io_pid, SIGSTOP);
		io_paused = 1;
	}
	current_state = VM_RECORDING_PAUSED;
	current_elapsed_ms = elapsed_before_pause_ms;
	return 0;
}

int voice_memo_service_record_resume(void)
{
	if (current_state != VM_RECORDING_PAUSED)
		return -1;
	if (io_pid > 0 && io_paused)
	{
		kill(io_pid, SIGCONT);
		io_paused = 0;
	}
	active_started_ms = monotonic_ms();
	current_state = VM_RECORDING;
	return 0;
}

int voice_memo_service_record_cancel(void)
{
	if (current_state != VM_RECORDING && current_state != VM_RECORDING_PAUSED)
		return -1;
	if (io_pid > 0)
		stop_child_process(SIGTERM);

	if (active_record_filename[0] != '\0')
	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, active_record_filename);
		remove(path);
	}

	current_state = VM_IDLE;
	current_memo = NULL;
	current_elapsed_ms = 0;
	elapsed_before_pause_ms = 0;
	active_started_ms = 0;
	active_record_filename[0] = '\0';
	return 0;
}

int voice_memo_service_record_stop(const char *title_optional)
{
	if (current_state != VM_RECORDING && current_state != VM_RECORDING_PAUSED)
		return -1;
	if (ensure_capacity() != 0)
		return -1;

	VoiceMemo *memo = malloc(sizeof(VoiceMemo));
	if (!memo)
		return -1;

	if (active_record_filename[0] != '\0') memo->filename = strdup(active_record_filename);
	else memo->filename = make_timestamp_filename();
	memo->duration_ms = timed_elapsed_ms();
	if (!memo->filename)
	{
		free(memo);
		return -1;
	}

	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, memo->filename);

	if (io_pid > 0 && io_is_recording)
	{
		stop_child_process(SIGINT);
	}

	if (!path_exists(path))
	{
		if (write_silence_wav(path, memo->duration_ms) != 0)
		{
			free(memo->filename);
			free(memo);
			return -1;
		}
	}

	memo->duration_ms = read_wav_duration_ms(path);
	if (memo->duration_ms <= 0)
		memo->duration_ms = current_elapsed_ms;

	if (title_optional && title_optional[0] != '\0') {
		char base_name[128] = {0};
		if (make_filename_from_title(title_optional, base_name, sizeof(base_name)) == 0) {
			char final_name[160] = {0};
			char new_path[1024] = {0};
			int suffix = 0;
			while (1) {
				if (suffix == 0) {
					snprintf(final_name, sizeof(final_name), "%s", base_name);
				} else {
					const char *dot = strrchr(base_name, '.');
					if (!dot) break;
					int stem_len = (int)(dot - base_name);
					snprintf(final_name, sizeof(final_name), "%.*s_%d.wav", stem_len, base_name, suffix);
				}
				snprintf(new_path, sizeof(new_path), "%s/%s", VOICE_MEMO_PATH, final_name);
				if (!path_exists(new_path)) break;
				suffix++;
			}
			if (final_name[0] != '\0' && strcmp(final_name, memo->filename) != 0) {
				if (rename(path, new_path) == 0) {
					free(memo->filename);
					memo->filename = strdup(final_name);
				}
			}
		}
	}

	insert_at_front(memo);
	current_state = VM_IDLE;
	current_memo = NULL;
	current_elapsed_ms = 0;
	elapsed_before_pause_ms = 0;
	active_started_ms = 0;
	active_record_filename[0] = '\0';
	return 0;
}

int voice_memo_service_play_start(const char *filename)
{
	if (!filename || current_state == VM_RECORDING || current_state == VM_RECORDING_PAUSED)
		return -1;
	const VoiceMemo *found = voice_memo_service_get_by_filename(filename);
	if (!found)
		return -1;

	if (io_pid > 0)
		stop_child_process(SIGTERM);

	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, found->filename);
	if (backend_play_supported)
	{
		if (spawn_aplay(path) != 0)
		{
			io_pid = -1;
		}
	}

	current_memo = (VoiceMemo *)found;
	current_elapsed_ms = 0;
	current_state = VM_PLAYING;
	elapsed_before_pause_ms = 0;
	active_started_ms = monotonic_ms();
	play_speed_percent = 100;
	io_paused = 0;
	return 0;
}

int voice_memo_service_play_pause(void)
{
	if (current_state != VM_PLAYING)
		return -1;
	elapsed_before_pause_ms = timed_elapsed_ms();
	if (io_pid > 0)
	{
		kill(io_pid, SIGSTOP);
		io_paused = 1;
	}
	current_state = VM_PAUSED;
	return 0;
}

int voice_memo_service_play_resume(void)
{
	if (current_state != VM_PAUSED)
		return -1;
	if (io_pid > 0 && io_paused)
	{
		kill(io_pid, SIGCONT);
		io_paused = 0;
	}
	active_started_ms = monotonic_ms();
	current_state = VM_PLAYING;
	return 0;
}

int voice_memo_service_play_stop(void)
{
	if (current_state != VM_PLAYING && current_state != VM_PAUSED)
		return -1;
	if (io_pid > 0)
		stop_child_process(SIGTERM);
	current_state = VM_IDLE;
	current_memo = NULL;
	current_elapsed_ms = 0;
	elapsed_before_pause_ms = 0;
	active_started_ms = 0;
	play_speed_percent = 100;
	return 0;
}

int voice_memo_service_seek_relative(int delta_ms)
{
	if (current_state != VM_PLAYING && current_state != VM_PAUSED)
		return -1;
	if (!current_memo)
		return -1;

	int total = current_memo->duration_ms;
	if (total <= 0)
		return -1;

	int base = (current_state == VM_PLAYING) ? timed_elapsed_ms() : elapsed_before_pause_ms;
	int next = base + delta_ms;
	if (next < 0) next = 0;
	if (next > total) next = total;

	elapsed_before_pause_ms = next;
	current_elapsed_ms = next;
	active_started_ms = monotonic_ms();
	return 0;
}

void voice_memo_service_set_speed_percent(int speed_percent)
{
	if (speed_percent < 50) speed_percent = 50;
	if (speed_percent > 200) speed_percent = 200;

	if (current_state == VM_PLAYING) {
		elapsed_before_pause_ms = timed_elapsed_ms();
		active_started_ms = monotonic_ms();
	}
	play_speed_percent = speed_percent;
}

int voice_memo_service_get_speed_percent(void)
{
	return play_speed_percent;
}

int voice_memo_service_delete(const char *filename)
{
	if (!filename)
		return -1;

	for (size_t i = 0; i < memos_count; i++)
	{
		VoiceMemo *memo = memos_index[i];
		if (strcmp(memo->filename, filename) != 0)
			continue;

		if (current_memo == memo)
		{
			if (io_pid > 0)
				stop_child_process(SIGTERM);
			current_state = VM_IDLE;
			current_memo = NULL;
			current_elapsed_ms = 0;
		}

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", VOICE_MEMO_PATH, memo->filename);
		remove(path);

		free(memo->filename);
		free(memo);

		for (size_t j = i; j + 1 < memos_count; j++)
		{
			memos_index[j] = memos_index[j + 1];
		}
		memos_count--;
		return 0;
	}

	return -1;
}

int voice_memo_service_rename(const char *old_filename, const char *new_title)
{
    if (!old_filename || !new_title) return -1;

    VoiceMemo *target = NULL;
    for (size_t i = 0; i < memos_count; i++) {
        if (strcmp(memos_index[i]->filename, old_filename) == 0) {
            target = memos_index[i];
            break;
        }
    }
    if (!target) return -1;

    char base_name[128] = {0};
    if (make_filename_from_title(new_title, base_name, sizeof(base_name)) != 0) {
        return -1;
    }

    char final_name[160] = {0};
    char old_path[1024];
    char new_path[1024];

    int suffix = 0;
    while (1) {
        if (suffix == 0) {
            snprintf(final_name, sizeof(final_name), "%s", base_name);
        } else {
            const char *dot = strrchr(base_name, '.');
            if (!dot) return -1;
            int stem_len = (int)(dot - base_name);
            if (stem_len < 1) return -1;
            snprintf(final_name, sizeof(final_name), "%.*s_%d.wav", stem_len, base_name, suffix);
        }

        snprintf(old_path, sizeof(old_path), "%s/%s", VOICE_MEMO_PATH, old_filename);
        snprintf(new_path, sizeof(new_path), "%s/%s", VOICE_MEMO_PATH, final_name);

        if (!path_exists(new_path) || strcmp(old_filename, final_name) == 0) break;
        suffix++;
    }

    if (strcmp(old_filename, final_name) == 0) return 0;
    if (rename(old_path, new_path) != 0) return -1;

    char *new_copy = strdup(final_name);
    if (!new_copy) return -1;
    free(target->filename);
    target->filename = new_copy;
    return 0;
}

int voice_memo_service_tick(void)
{
	if (current_state == VM_RECORDING || current_state == VM_PLAYING)
		current_elapsed_ms = timed_elapsed_ms();

	if (current_state == VM_PLAYING && io_pid > 0)
	{
		int status = 0;
		pid_t done = waitpid(io_pid, &status, WNOHANG);
		if (done == io_pid)
		{
			io_pid = -1;
			io_paused = 0;
			if (play_mode == VM_PLAYMODE_REPEAT_ONE && current_memo)
				return voice_memo_service_play_start(current_memo->filename);
			return voice_memo_service_next();
		}
	}

	if (current_state == VM_PLAYING && current_memo && current_memo->duration_ms > 0 && current_elapsed_ms >= current_memo->duration_ms)
	{
		if (play_mode == VM_PLAYMODE_REPEAT_ONE)
			return voice_memo_service_play_start(current_memo->filename);
		return voice_memo_service_next();
	}

	return 0;
}

int voice_memo_service_elapsed_ms(void)
{
	if (current_state == VM_RECORDING || current_state == VM_PLAYING)
		return timed_elapsed_ms();
	return current_elapsed_ms;
}

int voice_memo_service_total_ms(void)
{
	if (!current_memo)
		return 0;
	return current_memo->duration_ms;
}

int voice_memo_service_next(void)
{
	if (memos_count == 0)
		return -1;
	if (current_memo && play_mode == VM_PLAYMODE_REPEAT_ONE)
		return voice_memo_service_play_start(current_memo->filename);

	int cur = -1;
	if (current_memo)
	{
		for (size_t i = 0; i < memos_count; i++)
		{
			if (memos_index[i] == current_memo)
			{
				cur = (int)i;
				break;
			}
		}
	}

	int next = 0;
	if (play_mode == VM_PLAYMODE_SHUFFLE)
	{
		next = rand() % (int)memos_count;
		if (memos_count > 1 && next == cur)
			next = (next + 1) % (int)memos_count;
	}
	else
	{
		next = cur + 1;
		if (next >= (int)memos_count)
		{
			if (play_mode == VM_PLAYMODE_REPEAT_ALL)
				next = 0;
			else
			{
				voice_memo_service_play_stop();
				return 0;
			}
		}
	}

	return voice_memo_service_play_start(memos_index[next]->filename);
}

int voice_memo_service_prev(void)
{
	if (memos_count == 0)
		return -1;
	if (current_memo && play_mode == VM_PLAYMODE_REPEAT_ONE)
		return voice_memo_service_play_start(current_memo->filename);

	int cur = -1;
	if (current_memo)
	{
		for (size_t i = 0; i < memos_count; i++)
		{
			if (memos_index[i] == current_memo)
			{
				cur = (int)i;
				break;
			}
		}
	}

	int prev = (cur < 0) ? 0 : (cur - 1);
	if (play_mode == VM_PLAYMODE_SHUFFLE)
	{
		prev = rand() % (int)memos_count;
		if (memos_count > 1 && prev == cur)
			prev = (prev + 1) % (int)memos_count;
	}
	else if (prev < 0)
	{
		if (play_mode == VM_PLAYMODE_REPEAT_ALL)
			prev = (int)memos_count - 1;
		else
			prev = 0;
	}

	return voice_memo_service_play_start(memos_index[prev]->filename);
}

void voice_memo_service_cycle_mode(void)
{
	play_mode = (VMPlayMode)(((int)play_mode + 1) % 4);
}

VMPlayMode voice_memo_service_mode(void)
{
	return play_mode;
}

const char *voice_memo_service_mode_label(void)
{
	switch (play_mode)
	{
		case VM_PLAYMODE_REPEAT_ONE: return "REPEAT 1";
		case VM_PLAYMODE_REPEAT_ALL: return "REPEAT ALL";
		case VM_PLAYMODE_SHUFFLE: return "SHUFFLE";
		default: return "NORMAL";
	}
}

void voice_memo_service_shutdown(void)
{
	if (io_pid > 0)
		stop_child_process(SIGTERM);

	if (!memos_index)
		return;

	for (size_t i = 0; i < memos_count; i++)
	{
		free(memos_index[i]->filename);
		free(memos_index[i]);
	}
	free(memos_index);

	memos_index = NULL;
	memos_count = 0;
	memos_capacity = 0;
	current_state = VM_IDLE;
	current_memo = NULL;
	current_elapsed_ms = 0;
	active_record_filename[0] = '\0';
}

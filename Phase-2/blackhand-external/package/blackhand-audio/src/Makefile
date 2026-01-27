# Build for audio service.
SRCS = audio_service.c alsa_wrapper.c mp3_player.c

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -Wextra
LIBS ?= -lasound

all:
	$(CC) $(CFLAGS) -o blackhand-audio $(SRCS) $(LIBS)

clean:
	rm -f blackhand-audio

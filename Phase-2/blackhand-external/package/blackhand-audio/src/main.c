/*
 * Audio service entry point.
 * Expose JSON-RPC methods to set routing, volume, and playback.
 * Use the IPC library for socket messaging.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "cJSON.h"
#include "ipc_framing.h"
#include "ipc_dispatch.h"
#include "audio_alsa.h"
#define SOCKET_PATH "/run/bh-audio.sock"
#define BUFFER_SIZE 1024

int main()
{
	#You must initialise the ALSA (audio library before continuing)
	if (audio_init() != 0) {
        fprintf(stderr, "Failed to initialise ALSA\n");
        exit(EXIT_FAILURE);
    }

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}

	unlink(SOCKET_PATH);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, SOCKET_PATH);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(fd, 5) == -1)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("Server listening on %s\n", SOCKET_PATH);

	while (1)
	{
		int client_fd = accept(fd, NULL, NULL);
		if (client_fd == -1)
		{
			perror("accept");
			continue;
		}

		printf("Client connected\n");

		while (1)
		{
			char buffer[BUFFER_SIZE];
			int n = recv_msg(client_fd, buffer, BUFFER_SIZE);

			if (n == -1)
			{
				printf("Client disconnected\n");
				break;
			}

			cJSON *req = cJSON_Parse(buffer);
			if (!req)
			{
				printf("Invalid JSON\n");
				break;
			}

			dispatch(client_fd, req);
			cJSON_Delete(req);
		}

		close(client_fd);
	}

	return 0;
}
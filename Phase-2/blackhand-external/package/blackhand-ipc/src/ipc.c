/*
 * IPC library implementation.
 * High-level helpers wrapping socket setup for servers and clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ipc.h"

/*
 * ipc_server_listen - create a Unix domain socket, bind and listen on path.
 * Returns the listening fd on success, -1 on failure.
 */
int ipc_server_listen(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
	{
		perror("socket");
		return -1;
	}

	unlink(path);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("bind");
		close(fd);
		return -1;
	}

	if (listen(fd, 5) == -1)
	{
		perror("listen");
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * ipc_client_connect - connect to a Unix domain socket at path.
 * Returns the connected fd on success, -1 on failure.
 */
int ipc_client_connect(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
	{
		perror("socket");
		return -1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("connect");
		close(fd);
		return -1;
	}

	return fd;
}

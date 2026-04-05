#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdint.h>
#include "ipc_framing.h"

int send_msg(int fd, const char *msg)
{
	uint32_t len = strlen(msg);
	uint32_t net_len = htonl(len);

	if (send(fd, &net_len, sizeof(net_len), 0) == -1)
	{
		perror("send length");
		return -1;
	}

	if (send(fd, msg, len, 0) == -1)
	{
		perror("send message");
		return -1;
	}

	return 0;
}

int recv_msg(int fd, char *buffer, int buffer_size)
{
	uint32_t net_len;

	if (recv(fd, &net_len, sizeof(net_len), MSG_WAITALL) <= 0)
	{
		perror("recv length");
		return -1;
	}

	uint32_t len = ntohl(net_len);

	if (len >= (uint32_t)buffer_size)
	{
		fprintf(stderr, "message too large\n");
		return -1;
	}

	int n = recv(fd, buffer, len, MSG_WAITALL);
	if (n <= 0)
	{
		perror("recv message");
		return -1;
	}

	buffer[n] = '\0';
	return n;
}

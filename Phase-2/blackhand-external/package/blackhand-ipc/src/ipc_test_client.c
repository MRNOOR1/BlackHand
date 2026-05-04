/*
 * ipc_test_client.c — minimal BlackHand IPC test client
 *
 * Usage:  ipc_test_client <socket_path> <json_command>
 * Example: ipc_test_client /run/bh-audio.sock '{"cmd":"status"}'
 *
 * Wire format (matches ipc_framing.c):
 *   [4 bytes big-endian length][payload bytes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdint.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <socket_path> <json>\n", argv[0]);
        fprintf(stderr, "  e.g. %s /run/bh-audio.sock '{\"cmd\":\"status\"}'\n", argv[0]);
        return 1;
    }

    const char *sock_path = argv[1];
    const char *msg       = argv[2];

    /* Connect to Unix domain socket */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return 1;
    }

    /* Send: 4-byte big-endian length + payload */
    uint32_t len     = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);

    if (send(fd, &net_len, sizeof(net_len), 0) < 0) { perror("send len"); close(fd); return 1; }
    if (send(fd, msg, len, 0)                 < 0) { perror("send msg"); close(fd); return 1; }

    /* Receive response */
    uint32_t resp_net_len;
    if (recv(fd, &resp_net_len, sizeof(resp_net_len), MSG_WAITALL) <= 0) {
        perror("recv len"); close(fd); return 1;
    }

    uint32_t resp_len = ntohl(resp_net_len);
    if (resp_len >= BUF_SIZE) {
        fprintf(stderr, "response too large (%u bytes)\n", resp_len);
        close(fd); return 1;
    }

    char buf[BUF_SIZE];
    int n = recv(fd, buf, resp_len, MSG_WAITALL);
    if (n <= 0) { perror("recv body"); close(fd); return 1; }
    buf[n] = '\0';

    printf("%s\n", buf);
    close(fd);
    return 0;
}

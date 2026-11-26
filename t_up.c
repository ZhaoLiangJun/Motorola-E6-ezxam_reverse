/* t_up.c - 把设备上的文件通过 TCP 上传到 PC（配合 nc_listen.py）
 * 用法: t_up <host> <port> <file>
 * 交叉编译: arm-linux-gcc -O2 -o t_up t_up.c
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: t_up <host> <port> <file>\n");
        return 2;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    FILE* f = fopen(argv[3], "rb");
    if (!f) {
        fprintf(stderr, "open %s failed\n", argv[3]);
        return 3;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 4; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_aton(host, &a.sin_addr) == 0) {
        struct hostent* he = gethostbyname(host);
        if (!he) { fprintf(stderr, "bad host %s\n", host); return 5; }
        memcpy(&a.sin_addr, he->h_addr, 4);
    }
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
        perror("connect");
        return 6;
    }
    char buf[8192];
    size_t n, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t off = 0;
        while (off < n) {
            ssize_t w = send(s, buf + off, n - off, 0);
            if (w <= 0) { perror("send"); fclose(f); close(s); return 7; }
            off += (size_t)w;
        }
        total += n;
    }
    fclose(f);
    shutdown(s, SHUT_WR);
    close(s);
    printf("sent %u bytes\n", (unsigned)total);
    return 0;
}

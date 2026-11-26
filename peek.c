/* peek.c - 读/搜目标进程内存（root 下走 /proc/<pid>/mem）
 *   peek <pid> dump <addr> <len>
 *   peek <pid> find <addr> <len> <value>     # 找 4 字节小端等于 value 的位置
 *   peek <pid> poke <addr> <value>           # 写 4 字节（谨慎）
 * 地址/数值均支持 0x 前缀
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int open_mem(int pid)
{
    char p[64];
    sprintf(p, "/proc/%d/mem", pid);
    int fd = open(p, O_RDWR);
    if (fd < 0) { perror(p); }
    return fd;
}

static void hexdump(unsigned long addr, unsigned char *b, int n)
{
    int i, j;
    for (i = 0; i < n; i += 16) {
        printf("%08lx ", addr + i);
        for (j = 0; j < 16; j++) {
            if (i + j < n) printf("%02x", b[i + j]);
            else printf("  ");
            printf(" ");
        }
        printf(" |");
        for (j = 0; j < 16 && i + j < n; j++) {
            unsigned char c = b[i + j];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

int main(int argc, char **argv)
{
    int pid, fd, n;
    unsigned long addr, len, val;
    static unsigned char buf[256 * 1024];

    if (argc < 4) {
        fprintf(stderr,
                "usage:\n  peek <pid> dump <addr> <len>\n"
                "  peek <pid> find <addr> <len> <val>\n"
                "  peek <pid> poke <addr> <val>\n");
        return 1;
    }
    pid = atoi(argv[1]);
    addr = strtoul(argv[3], 0, 0);
    fd = open_mem(pid);
    if (fd < 0) return 1;

    if (!strcmp(argv[2], "dump")) {
        len = strtoul(argv[4], 0, 0);
        while (len) {
            int chunk = len > sizeof(buf) ? (int)sizeof(buf) : (int)len;
            if (lseek(fd, (off_t)addr, SEEK_SET) < 0) { perror("lseek"); return 1; }
            n = read(fd, buf, chunk);
            if (n <= 0) { perror("read"); return 1; }
            hexdump(addr, buf, n);
            addr += n; len -= n;
        }
        return 0;
    }

    if (!strcmp(argv[2], "find")) {
        unsigned char *p;
        unsigned long i, base;
        int chunk;
        len = strtoul(argv[4], 0, 0);
        val = strtoul(argv[5], 0, 0);
        base = addr;
        while (len) {
            chunk = len > sizeof(buf) ? (int)sizeof(buf) : (int)len;
            if (lseek(fd, (off_t)addr, SEEK_SET) < 0) { perror("lseek"); return 1; }
            n = read(fd, buf, chunk);
            if (n <= 0) break;
            p = buf;
            for (i = 0; i + 4 <= (unsigned long)n; i += 4) {
                unsigned long w = p[i] | (p[i+1] << 8) | (p[i+2] << 16) |
                                  ((unsigned long)p[i+3] << 24);
                if (w == val) printf("%08lx\n", base + i);
            }
            base += n; addr += n; len -= n;
            if (n < chunk) break;
        }
        return 0;
    }

    if (!strcmp(argv[2], "poke")) {
        unsigned long w;
        unsigned char b[4];
        val = strtoul(argv[4], 0, 0);
        b[0] = val & 0xff; b[1] = (val >> 8) & 0xff;
        b[2] = (val >> 16) & 0xff; b[3] = (val >> 24) & 0xff;
        if (lseek(fd, (off_t)addr, SEEK_SET) < 0) { perror("lseek"); return 1; }
        n = write(fd, b, 4);
        if (n != 4) { perror("write"); return 1; }
        w = val;
        printf("poke %08lx = %08lx ok\n", addr, w);
        return 0;
    }

    fprintf(stderr, "unknown mode %s\n", argv[2]);
    return 1;
}

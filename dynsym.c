/* dynsym.c - 设备端解析 ELF32 的 .dynsym（替代 readelf）
 *   dynsym <file> [子串]
 * 输出: 地址 类型 绑定 定义/UND 名字
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned short u16;

static u16 rd16(unsigned char *p) { return p[0] | (p[1] << 8); }
static u32 rd32(unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *d;
    long sz;
    u32 shoff;
    u16 shentsize, shnum, i;
    const char *want = argc > 2 ? argv[2] : 0;
    int n = 0;

    if (argc < 2) { fprintf(stderr, "usage: dynsym <file> [substr]\n"); return 1; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(sz);
    if (fread(d, 1, sz, f) != (size_t)sz) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    if (sz < 52 || memcmp(d, "\177ELF", 4)) { fprintf(stderr, "not ELF\n"); return 1; }
    shoff = rd32(d + 0x20);
    shentsize = rd16(d + 0x2E);
    shnum = rd16(d + 0x30);

    for (i = 0; i < shnum; i++) {
        unsigned char *s = d + shoff + i * shentsize;
        u32 type = rd32(s + 4), off = rd32(s + 16), size = rd32(s + 20), link = rd32(s + 24);
        u32 entsize = rd32(s + 36);
        unsigned char *str;
        u32 j, cnt;
        if (type != 11) continue;                   /* 只看 SHT_DYNSYM */
        if (!size || !link || link >= shnum) continue;
        if (!entsize) entsize = 16;
        cnt = size / entsize;
        str = d + rd32(d + shoff + link * shentsize + 16);
        for (j = 0; j < cnt; j++) {
            unsigned char *e = d + off + j * entsize;
            u32 nm = rd32(e), val = rd32(e + 4);
            unsigned char info = e[12];
            u16 shndx = rd16(e + 14);
            const char *name;
            if (!nm) continue;
            if ((unsigned char *)(str + nm) >= (unsigned char *)d + sz) continue;
            name = (const char *)(str + nm);
            if (want && !strstr(name, want)) continue;
            printf("%08x %-6s %-6s %-5s %s\n", val,
                   info & 0xf ? (info & 0xf) == 2 ? "FUNC" : "OBJ" : "NOTYPE",
                   (info >> 4) == 1 ? "GLOBAL" : (info >> 4) == 2 ? "WEAK" : "LOCAL",
                   shndx == 0 ? "UND" : "DEF", name);
            n++;
        }
    }
    if (!n) printf("(no match)\n");
    return 0;
}

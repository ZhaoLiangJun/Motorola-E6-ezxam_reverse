#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""elfrd.py - 读 orig_local.so：按 ELF 节表把 vaddr 映射到文件偏移，解 GOT 槽 -> C 字符串。

用法：
  from elfrd import Elf
  e = Elf("orig_local.so")
  e.word(0x41af7200)          # 读字面池字
  e.got_str(0xd04)            # GOT 基址(0x41b229f0)+0xd04 -> 指向的字符串
  e.str(0x41b18d80)
"""
import struct
import sys


class Elf:
    GOTBASE = 0x41b229f0

    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        e_shoff = struct.unpack_from("<I", d, 0x20)[0]
        e_shentsize = struct.unpack_from("<H", d, 0x2E)[0]
        e_shnum = struct.unpack_from("<H", d, 0x30)[0]
        self.secs = []
        for i in range(e_shnum):
            o = e_shoff + i * e_shentsize
            name, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", d, o)
            self.secs.append((name, typ, addr, off, size))

    def off(self, addr):
        for _n, _t, a, o, s in self.secs:
            if s and a <= addr < a + s:
                return o + (addr - a)
        return None

    def word(self, addr):
        f = self.off(addr)
        if f is None or f + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, f)[0]

    def bytes_at(self, addr, n):
        f = self.off(addr)
        if f is None:
            return None
        return self.data[f:f + n]

    def str(self, addr):
        f = self.off(addr)
        if f is None:
            return None
        e = self.data.find(b"\x00", f)
        if e < 0 or e - f > 200:
            return None
        s = self.data[f:e]
        try:
            return s.decode("utf-8")
        except UnicodeDecodeError:
            return repr(s)

    def got_str(self, off, gotbase=None):
        gb = self.GOTBASE if gotbase is None else gotbase
        slot = gb + off
        v = self.word(slot)
        if v is None:
            return None, slot, None
        return v, slot, self.str(v)

    def got_word(self, off, gotbase=None):
        gb = self.GOTBASE if gotbase is None else gotbase
        return self.word(gb + off)


if __name__ == "__main__":
    e = Elf(sys.argv[1] if len(sys.argv) > 1 else "orig_local.so")
    # 自检：getName 的 AM_ROOT_FOLDER / Items
    for off in (0xd00, 0xd70, 0xd78, 0xd7c):
        print(hex(off), e.got_str(off))

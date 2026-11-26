# -*- coding: utf-8 -*-
"""fb2png.py - 帧缓冲(RGB565) -> PNG（不依赖 PIL）
用法: python fb2png.py in.raw out.png [w] [h] [offset]
"""
import sys, struct, zlib

def main():
    src = sys.argv[1]; out = sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 320
    off = int(sys.argv[5], 0) if len(sys.argv) > 5 else 0
    d = open(src, 'rb').read()
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        base = off + y*w*2
        for x in range(w):
            lo = d[base+x*2]; hi = d[base+x*2+1]
            v = lo | (hi << 8)
            r = ((v >> 11) & 0x1f) * 255 // 31
            g = ((v >> 5) & 0x3f) * 255 // 63
            b = (v & 0x1f) * 255 // 31
            raw += bytes((r, g, b))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    open(out, 'wb').write(png)
    print('wrote %s %dx%d' % (out, w, h))

if __name__ == '__main__':
    main()

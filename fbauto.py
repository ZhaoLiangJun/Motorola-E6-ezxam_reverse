# -*- coding: utf-8 -*-
"""fbauto.py - 试探帧缓冲真实几何（pitch/bpp），输出若干 PNG 供肉眼挑选
用法: python fbauto.py in.raw [前缀]
"""
import sys, struct, zlib

def png_write(path, w, h, raw):
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)

def render(d, w, h, pitch, bpp, off=0):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        base = off + y * pitch
        if base + w * bpp > len(d):
            return None
        for x in range(w):
            p = base + x * bpp
            if bpp == 2:
                v = d[p] | (d[p + 1] << 8)
                r = ((v >> 11) & 0x1f) * 255 // 31
                g = ((v >> 5) & 0x3f) * 255 // 63
                b = (v & 0x1f) * 255 // 31
            elif bpp == 4:
                b, g, r = d[p], d[p + 1], d[p + 2]
            else:
                b, g, r = d[p], d[p + 1], d[p + 2]
            raw += bytes((r, g, b))
    return raw

src = sys.argv[1]
pre = sys.argv[2] if len(sys.argv) > 2 else '_fbtry'
d = open(src, 'rb').read()
n = len(d)
cands = []
for bpp in (2, 4):
    for pitch in (240 * bpp, 256 * bpp, 320 * bpp, 512, 1024, 1024 if bpp == 2 else 1280):
        h = n // pitch
        if h < 100 or h > 800:
            continue
        cands.append((240, min(h, 400), pitch, bpp))
seen = set()
for i, (w, h, pitch, bpp) in enumerate(cands):
    key = (w, h, pitch, bpp)
    if key in seen:
        continue
    seen.add(key)
    raw = render(d, w, h, pitch, bpp)
    if raw is None:
        continue
    p = '%s_%d_%dx%d_p%d_b%d.png' % (pre, i, w, h, pitch, bpp)
    png_write(p, w, h, raw)
    print(p)

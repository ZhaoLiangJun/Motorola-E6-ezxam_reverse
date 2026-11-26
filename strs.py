# -*- coding: utf-8 -*-
"""strs.py - 从 ELF 里提取可打印字符串（含偏移），用于核对字面量
用法: python strs.py <elf> <子串> [最小长度]
"""
import sys, re

path = sys.argv[1]
pat = sys.argv[2] if len(sys.argv) > 2 else ''
minlen = int(sys.argv[3]) if len(sys.argv) > 3 else 4

d = open(path, 'rb').read()
for m in re.finditer(rb'[ -~]{%d,}' % minlen, d):
    s = m.group().decode('ascii')
    if pat in s:
        print('0x%06x  %r' % (m.start(), s))

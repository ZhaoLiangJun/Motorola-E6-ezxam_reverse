# -*- coding: utf-8 -*-
"""qm_dump.py —— 解析 Qt 2.3 .qm（按 qtranslator.h 的 Tag 枚举）

Tag_End=1, Tag_SourceText16=2, Tag_Translation=3, Tag_Context16=4,
Tag_Hash=5, Tag_SourceText=6, Tag_Context=7, Tag_Comment=8, Tag_Obsolete1=9
字符串统一是 u32BE 字节长度 + 内容（utf16 用 BE）。
Tag_Hash 例外：后面直接跟 4 字节 hash，没有长度前缀。

用法: python qm_dump.py <file.qm> [过滤子串]
"""
import sys, struct


def sections(d):
    p = 16
    out = {}
    while p < len(d):
        tag = d[p]
        if tag == 0:
            break
        ln = struct.unpack_from('>I', d, p + 1)[0]
        out[tag] = (p + 5, p + 5 + ln)
        p = p + 5 + ln
    return out


def parse_messages(d, a, b):
    msgs = []
    p = a
    cur = {'t': '', 'ctx': '', 'src': '', 'cm': '', 'h': None}
    while p < b:
        tag = d[p]
        p += 1
        if tag == 0:
            break
        if tag == 1:                       # Tag_End：一条消息结束
            msgs.append(cur)
            cur = {'t': '', 'ctx': '', 'src': '', 'cm': '', 'h': None}
            continue
        if tag == 5:                       # Tag_Hash：裸 4 字节
            cur['h'] = struct.unpack_from('>I', d, p)[0]
            p += 4
            continue
        ln = struct.unpack_from('>I', d, p)[0]
        p += 4
        raw = d[p:p + ln]
        p += ln
        if tag == 3:
            try:
                cur['t'] = raw.decode('utf-16-be')
            except Exception:
                cur['t'] = repr(raw)
        elif tag == 4:
            try:
                cur['ctx'] = raw.decode('utf-16-be')
            except Exception:
                cur['ctx'] = repr(raw)
        elif tag == 2:
            try:
                cur['src'] = raw.decode('utf-16-be')
            except Exception:
                cur['src'] = repr(raw)
        elif tag == 6:
            cur['src'] = raw.rstrip(b'\0').decode('latin1')
        elif tag == 7:
            cur['ctx'] = raw.rstrip(b'\0').decode('latin1')
        elif tag == 8:
            cur['cm'] = raw.rstrip(b'\0').decode('latin1')
    return msgs


def main():
    path = sys.argv[1]
    filt = sys.argv[2] if len(sys.argv) > 2 else ''
    d = open(path, 'rb').read()
    s = sections(d)
    a, b = s[0x69]
    msgs = parse_messages(d, a, b)
    print('# %s  消息数=%d' % (path, len(msgs)))
    for m in msgs:
        line = '%-22s | h=%08x | %s' % (m['ctx'] or '(无)', m['h'] or 0,
                                        m['t'].replace('\n', '\\n'))
        if m['src']:
            line += '  <<src=%r>>' % m['src']
        if m['cm']:
            line += '  <<cm=%r>>' % m['cm']
        if filt and filt not in line:
            continue
        print(line)


if __name__ == '__main__':
    main()

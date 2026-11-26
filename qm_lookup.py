# -*- coding: utf-8 -*-
"""qm_lookup.py —— 按 (sourceText, comment) 反查 .qm 里的译文

hash 算法取自 libqte-mt.so.2 的 0x415b2d18（QTranslatorMessage 构造里调用）：
    elfHash(sourceText + comment)，空串或结果 0 时返回 1
查找顺序与 QTranslator::findMessage 一致：先按 hash 取候选，再比对 context。
"""
import sys, struct
import qm_dump


def elf_hash(s):
    if not s:
        return 1
    h = 0
    for ch in s.encode('latin1', 'replace'):
        h = (h << 4) + ch
        h &= 0xffffffff
        g = h & 0xf0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xffffffff
    return h if h else 1


def index_qm(path):
    d = open(path, 'rb').read()
    s = qm_dump.sections(d)
    a, b = s[0x69]
    msgs = qm_dump.parse_messages(d, a, b)
    idx = {}
    for m in msgs:
        idx.setdefault(m['h'], []).append(m)
    return idx


def main():
    qms = ['_zhcn/zh-cn/lib_zh-cn.qm', '_zhcn/zh-cn/am_zh-cn.qm']
    idxs = [(q, index_qm(q)) for q in qms]

    pairs = [
        ('User2',                 'TXT_RID_CBUTTON_USER2'),
        ('User1',                 'TXT_RID_CBUTTON_USER1'),
        ('Office',                'TXT_RID_CBUTTON_OFFICE'),
        ('All',                   'TXT_RID_CBUTTON_ALL'),
        ('Multimedia',            'TXT_RID_CBUTTON_MULTIMEDIA'),
        ('Games',                 'TXT_RID_CBUTTON_GAMES'),
        ('Application',           'TXT_RID_CBUTTON_APPLICATION'),
        ('Extra',                 'TXT_RID_CBUTTON_EXTRA'),
        ('CMCC',                  'TXT_RID_CBUTTON_CMCC'),
        ('Antenna Off',           'TXT_RID_CONTENT_SLINE_ANTENNA_OFF'),
        ('No Service',            'TXT_RID_LABEL_NO_SERVICE'),
        ('Searching...',          'TXT_RID_CONTENT_SLINE_SEARCHING'),
        ('Registering...',        'TXT_RID_CONTENT_SLINE_REGISTERING'),
        ('NetworkBusy',           'TXT_RID_CBUTTON_NETWORKBUSY'),
        ('Emergency Only',        'TXT_RID_LABEL_EMERGENCY_ONLY'),
    ]
    for src, cm in pairs:
        h = elf_hash(src + cm)
        got = []
        for qname, idx in idxs:
            for m in idx.get(h, []):
                got.append('%s[%s]=%r' % (qname.split('/')[-1],
                                          m['ctx'] or '-', m['t']))
        print('%-18s %-36s h=%08x  ->  %s' %
              (src, cm, h, ' ; '.join(got) if got else '(无)'))


if __name__ == '__main__':
    main()

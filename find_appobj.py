# -*- coding: utf-8 -*-
# 找 am 中调用 getAppObjById PLT (0x233b4) 的位置（含 ldr pool + blx r3 模式）
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amxref
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

f, segs = amxref.parse()
TARGETS = {0x233b4: 'getAppObjById', 0x221cc: 'getExecFullPath',
           0x21e6c: 'doLaunchAppWithUidGid', 0x25cf8: 'Connector.appStart',
           0x23c60: 'LauncherInterface.launchApp'}
md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.skipdata = True
# .text 大概范围
text = None
for sv, so, sf in segs:
    if sv <= 0x316e0 < sv + sf:
        text = (sv, so, sf)
        break
sv, so, sf = text
code = f[so:so+sf]
# 扫描字面量池引用: 找所有 ldr rX,[pc,#imm] 指向值==TARGET 的，及 bl 目标
hits = []
for i in md.disasm(code, sv):
    if i.mnemonic in ('ldr',) and '[pc' in i.op_str and '#' in i.op_str:
        import re
        mo = re.search(r'#(-?0x[0-9a-f]+|-?\d+)\]', i.op_str)
        if mo:
            imm = int(mo.group(1), 0)
            pc = i.address + 8
            lit = amxref.read_at(f, segs, pc + imm, 4)
            if lit and len(lit) == 4:
                v = struct.unpack('<I', lit)[0]
                if v in TARGETS:
                    hits.append((i.address, 'ldr <- ' + TARGETS[v]))
    elif i.mnemonic in ('bl', 'blx'):
        try:
            tgt = int(i.op_str.lstrip('#'), 16)
            if tgt in TARGETS:
                hits.append((i.address, 'call ' + TARGETS[tgt]))
        except Exception:
            pass
for a, k in hits:
    print('%08x  %s' % (a, k))

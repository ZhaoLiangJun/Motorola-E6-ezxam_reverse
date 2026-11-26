# -*- coding: utf-8 -*-
"""adis.py - 带符号注释的 ARM 反汇编
用法: python adis.py <elf> <addr_hex> <len> [t]
"""
import sys, struct
import pltmap
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

def main():
    path = sys.argv[1]
    addr = int(sys.argv[2], 16)
    size = int(sys.argv[3], 0)
    thumb = len(sys.argv) > 4 and sys.argv[4] == 't'
    f, segs, byname, stubmap = pltmap.build(path)
    code = pltmap.read_at(f, segs, addr, size)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    md.skipdata = True
    # 收集整个 ELF 的函数符号（dynsym STT_FUNC 已定义）用于函数边界注释
    for i in md.disasm(code, addr):
        ann = ''
        if i.mnemonic == 'bl':
            try:
                tgt = int(i.op_str.lstrip('#'), 16)
                if tgt in stubmap:
                    ann = '  ; <%s>' % pltmap.demangle(stubmap[tgt])
            except Exception:
                pass
        # 字面量池: ldr rX, [pc, #imm] 输出所加载的值
        extra = ''
        if i.mnemonic == 'ldr' and '[pc' in i.op_str:
            try:
                import re
                mo = re.search(r'#(-?0x[0-9a-f]+|-?\d+)\]', i.op_str)
                if mo:
                    imm = int(mo.group(1), 0)
                    pc = i.address + 8
                    lit = pltmap.read_at(f, segs, pc+imm, 4)
                    if lit:
                        v = struct.unpack('<I', lit)[0]
                        extra = '  ; =0x%08x' % v
                        if v in stubmap:
                            extra += ' <%s>' % pltmap.demangle(stubmap[v])
            except Exception:
                pass
        print('%08x  %-8s %-38s%s%s' % (i.address, i.mnemonic, i.op_str, extra, ann))

main()

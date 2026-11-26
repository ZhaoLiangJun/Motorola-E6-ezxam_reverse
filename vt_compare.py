#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按名字对齐比较两个库的 vtable 槽位（槽位值解析为符号名后比较）"""
import struct, sys


class Elf:
    def __init__(self, path):
        self.data = bytearray(open(path, 'rb').read())
        d = self.data
        e_shoff = struct.unpack_from('<I', d, 32)[0]
        es = struct.unpack_from('<H', d, 46)[0]
        n = struct.unpack_from('<H', d, 48)[0]
        snd = struct.unpack_from('<H', d, 50)[0]
        self.sh = []
        for i in range(n):
            o = e_shoff + i * es
            s = struct.unpack_from('<10I', d, o)
            self.sh.append(dict(name=s[0], type=s[1], flags=s[2], addr=s[3],
                                off=s[4], size=s[5], link=s[6], info=s[7],
                                entsize=s[9]))
        ss = d[self.sh[snd]['off']:self.sh[snd]['off'] + self.sh[snd]['size']]

        def cn(i):
            e = ss.find(b'\0', i)
            return ss[i:e].decode('ascii', 'replace')
        for s in self.sh:
            s['name'] = cn(s['name'])
        # 收集所有定义的符号 defsyms: {addr: name}  （取 .symtab 优先）
        self.defsyms = {}
        for tab_name, str_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
            tab = self.sec(tab_name)
            st = self.sec(str_name)
            if not tab or not st:
                continue
            sd = d[st['off']:st['off'] + st['size']]
            esz = tab['entsize'] or 16
            for i in range(tab['size'] // esz):
                o = tab['off'] + i * esz
                st_name, st_val, st_size, st_info, st_other, st_shndx = \
                    struct.unpack_from('<IIIBBH', d, o)
                if st_shndx == 0 or st_val == 0:
                    continue
                e = sd.find(b'\0', st_name)
                nm = sd[st_name:e].decode('ascii', 'replace') if st_name else ''
                # 跳过 ARM 映射符号 $a/$d/$t（与真实符号同地址，且排在前）
                if not nm or nm.startswith('$'):
                    continue
                # 优先保留 GLOBAL/WEAK 且非 section 符号
                old = self.defsyms.get(st_val)
                if old is None or (old.startswith('$')):
                    self.defsyms[st_val] = nm
        # 符号名 -> 地址（用于按名查找）
        self.byname = {}
        for a, nm in self.defsyms.items():
            self.byname.setdefault(nm, a)
        # 重定位表：r_offset -> (sym_name, type)
        self.reloc = {}
        for tab_name in ('.rel.dyn', '.rel.plt'):
            rel = self.sec(tab_name)
            if not rel:
                continue
            for i in range(rel['size'] // 8):
                o = rel['off'] + i * 8
                r_off, r_info = struct.unpack_from('<II', d, o)
                sym = r_info >> 8
                typ = r_info & 0xFF
                nm = self.dynsym_name(sym)
                self.reloc.setdefault(r_off, (nm, typ))
        # 记录每个 vtable 名 -> (地址, 大小)
        self.vt = {}
        for tab_name, str_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
            tab = self.sec(tab_name)
            st = self.sec(str_name)
            if not tab or not st:
                continue
            sd = d[st['off']:st['off'] + st['size']]
            esz = tab['entsize'] or 16
            for i in range(tab['size'] // esz):
                o = tab['off'] + i * esz
                st_name, st_val, st_size, st_info, st_other, st_shndx = \
                    struct.unpack_from('<IIIBBH', d, o)
                if st_shndx == 0 or st_val == 0 or st_name == 0:
                    continue
                e = sd.find(b'\0', st_name)
                nm = sd[st_name:e].decode('ascii', 'replace')
                if nm.startswith('_ZTV') and st_size >= 8:
                    if nm not in self.vt or st_size > self.vt[nm][1]:
                        self.vt[nm] = (st_val, st_size)

    def dynsym_name(self, idx):
        d = self.data
        dyn = self.sec('.dynsym')
        dst = self.sec('.dynstr')
        if not dyn or not dst:
            return None
        esz = dyn['entsize'] or 16
        if idx * esz + 16 > dyn['size']:
            return None
        sd = d[dst['off']:dst['off'] + dst['size']]
        o = dyn['off'] + idx * esz
        st_name = struct.unpack_from('<I', d, o)[0]
        if st_name == 0:
            return None
        e = sd.find(b'\0', st_name)
        return sd[st_name:e].decode('ascii', 'replace')

    def sec(self, name):
        for s in self.sh:
            if s['name'] == name:
                return s
        return None

    def off_of(self, vaddr):
        for s in self.sh:
            if s['type'] == 1 and s['addr'] <= vaddr < s['addr'] + s['size']:
                return s['off'] + (vaddr - s['addr'])
        # .bss 无文件内容
        return None

    def u32(self, vaddr, init=None):
        """读 vaddr 处的 4 字节；若不可读返回 init"""
        o = self.off_of(vaddr)
        if o is None or o + 4 > len(self.data):
            return init
        return struct.unpack_from('<I', self.data, o)[0]

    def name_at(self, addr):
        """地址对应符号名，找不到返回 None"""
        if addr == 0:
            return '<0>'
        if addr in self.defsyms:
            return self.defsyms[addr]
        # 找最近的（地址落在函数中间）
        best = None
        for a in self.defsyms:
            if a <= addr < a + 0x400:
                if best is None or a > best:
                    best = a
        if best is not None:
            return self.defsyms[best] + f"+{addr-best:#x}"
        return f"<{addr:#x}>"

    def vt_slots(self, vt_addr, count):
        """ARM Itanium ABI: vt_addr 处 = offset-to-top, vt_addr+4 = typeinfo, vt_addr+8 起为函数指针"""
        out = []
        for k in range(count):
            out.append(self.slot_resolve(vt_addr + 8 + k * 4))
        return out

    def slot_resolve(self, addr):
        """优先用重定位指向的符号名（重建库槽位文件内为0），否则用槽位实际值"""
        r = self.reloc.get(addr)
        if r and r[0]:
            return r[0]
        v = self.u32(addr, 0)
        return self.name_at(v)


def main():
    orig = Elf(sys.argv[1] if len(sys.argv) > 1 else 'orig_local.so')
    reco = Elf(sys.argv[2] if len(sys.argv) > 2 else 'libezxam_reconstructed.so.1.0.0')

    print("=" * 76)
    print("原库 vtable 数: %d   重建库 vtable 数: %d" % (len(orig.vt), len(reco.vt)))
    print("=" * 76)

    only_orig = sorted(set(orig.vt) - set(reco.vt))
    only_reco = sorted(set(reco.vt) - set(orig.vt))
    common = sorted(set(orig.vt) & set(reco.vt))
    print("\n[仅原库有] %d:" % len(only_orig))
    for n in only_orig:
        print("   %s" % n)
    print("\n[仅重建库有] %d:" % len(only_reco))
    for n in only_reco:
        print("   %s" % n)

    print("\n" + "=" * 76)
    print("共有 vtable 逐槽位对比")
    print("=" * 76)
    diff_vts = []
    for name in common:
        oa, osz = orig.vt[name]
        ra, rsz = reco.vt[name]
        o_cnt = max(0, (osz - 8) // 4)
        r_cnt = max(0, (rsz - 8) // 4)
        cnt = max(o_cnt, r_cnt)
        os_ = orig.vt_slots(oa, cnt)
        rs_ = reco.vt_slots(ra, cnt)
        onames = os_
        rnames = rs_
        # 剪掉共同尾部空槽
        while onames and rnames and onames[-1] == '<0>' and rnames[-1] == '<0>':
            onames.pop(); rnames.pop()
        mism = [i for i in range(min(len(onames), len(rnames))) if onames[i] != rnames[i]]
        status = "OK" if (not mism and len(onames) == len(rnames)) else "DIFF"
        print("\n--- %s  orig=0x%08x(%d槽) reco=0x%08x(%d槽)  %s" %
              (name, oa, len(onames), ra, len(rnames), status))
        if status == "DIFF":
            diff_vts.append((name, onames, rnames))
            for i in range(max(len(onames), len(rnames))):
                o = onames[i] if i < len(onames) else '--'
                r = rnames[i] if i < len(rnames) else '--'
                mark = '  ' if o == r else '!!'
                print("   %s [%2d] orig=%-52s reco=%s" % (mark, i, o, r))

    print("\n\n>>> 有差异的 vtable: %d / %d" % (len(diff_vts), len(common)))
    for n, _, _ in diff_vts:
        print("    %s" % n)


if __name__ == '__main__':
    main()

# -*- coding: utf-8 -*-
"""edis.py - 通用 ELF(ARM) 反汇编视图：capstone + dynsym + PLT + 字面池注释

用法:
  python edis.py <elf> <addr>                # 反汇编从 addr 开始，直到函数返回/上限
  python edis.py <elf> <start>-<end>         # 反汇编区间
  python edis.py <elf> <addr> --n 200        # 指令条数上限
  python edis.py <elf> --sym <name>          # 按符号名定位后反汇编
  python edis.py <elf> --syms PackageManager # 列出匹配的动态符号

实现要点（踩过的坑）:
  * ARM PLT stub 三指令: add ip,pc,#imm0 / add ip,ip,#imm1 / ldr pc,[ip,#imm2]!
      - 前两条 add 的 imm 是**循环立即数**，必须按 rot 解码，不能直接取低 8 位
      - 第三条 ldr 的 imm 是**普通 12 位无符号数**
  * 导入符号顺序由 .rel.plt(SHT_REL=9) 的 sym 索引决定，不是 .rel.dyn
  * prelink 后的库 .data 里可能有 ABS32，字面池 ldr 取到的值要按 vaddr 再映射字符串
"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfrd import Elf
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_LITTLE_ENDIAN
from capstone.arm import ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM


class ArmElf:
    def __init__(self, path):
        self.path = path
        self.e = Elf(path)
        d = self.e.data
        self.d = d
        e_shoff = struct.unpack_from("<I", d, 0x20)[0]
        e_shentsize = struct.unpack_from("<H", d, 0x2E)[0]
        e_shnum = struct.unpack_from("<H", d, 0x30)[0]
        e_shstrndx = struct.unpack_from("<H", d, 0x32)[0]
        # 取节名
        o = e_shoff + e_shstrndx * e_shentsize
        strtab_off = struct.unpack_from("<IIIIII", d, o)[4]
        self.by_name = {}
        for i in range(e_shnum):
            o = e_shoff + i * e_shentsize
            nm, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", d, o)
            p = strtab_off + nm
            q = d.find(b"\x00", p)
            name = d[p:q].decode("latin-1")
            self.by_name[name] = (typ, addr, off, size)
        self._syms()
        self._relplt()

    # ---------- 动态符号 ----------
    def _syms(self):
        self.syms = []          # (value, name, size)
        if ".dynsym" not in self.by_name or ".dynstr" not in self.by_name:
            return
        _t, sa, so, ss = self.by_name[".dynsym"]
        _t2, da, do, ds = self.by_name[".dynstr"]
        n = ss // 16
        for i in range(n):
            p = so + i * 16
            nameoff, value, size, info, other, shndx = struct.unpack_from("<IIIIHH", self.d, p)
            if not value:
                continue
            q = self.d.find(b"\x00", do + nameoff)
            nm = self.d[do + nameoff:q].decode("latin-1")
            if nm:
                self.syms.append((value, nm, size))
        self.syms.sort()
        self._val = [s[0] for s in self.syms]

    def sym_at(self, addr):
        """返回 (name, base, size) 或 None"""
        import bisect
        i = bisect.bisect_right(self._val, addr) - 1
        if i < 0:
            return None
        v, nm, sz = self.syms[i]
        if sz and addr < v + sz:
            return (nm, v, sz)
        if sz == 0 and addr == v:
            return (nm, v, sz)
        return None

    # ---------- PLT ----------
    def _relplt(self):
        """stub vaddr -> 导入符号名。
        正确做法（eplt.py 验证过）：从 stub 三条指令算出 GOT 槽地址，
        再用 .rel.plt 里 r_offset == 该槽 的记录反查符号；不能按顺序数。"""
        self.plt = {}
        if ".rel.plt" not in self.by_name or ".plt" not in self.by_name:
            return
        typ, ra, ro, rs = self.by_name[".rel.plt"]
        if typ != 9:            # SHT_REL
            return
        slots = {}
        dyn = self.syms_dyn()
        for i in range(rs // 8):
            off, info = struct.unpack_from("<II", self.d, ro + i * 8)
            sym = info >> 8
            rt = info & 0xFF
            if rt == 22 and sym < len(dyn):
                slots[off] = dyn[sym]
        ptyp, pa, po, psz = self.by_name[".plt"]
        for a in range(pa, pa + psz - 11, 4):
            o = po + (a - pa)
            w0, w1, w2 = struct.unpack_from("<III", self.d, o)
            if (w0 & 0xFFFFF000) != 0xE28FC000:
                continue
            if (w1 & 0xFFFFF000) != 0xE28CC000:
                continue
            if (w2 & 0xFFFFF000) != 0xE5BCF000:
                continue
            ip = (a + 8) + rot_imm(w0)
            ip = (ip + rot_imm(w1)) & 0xFFFFFFFF
            slot = (ip + (w2 & 0xFFF)) & 0xFFFFFFFF
            if slot in slots:
                self.plt[a] = slots[slot]

    def syms_dyn(self):
        if not hasattr(self, "_dyn"):
            self._dyn = []
            if ".dynsym" in self.by_name and ".dynstr" in self.by_name:
                _t, sa, so, ss = self.by_name[".dynsym"]
                _t2, da, do, ds = self.by_name[".dynstr"]
                for i in range(ss // 16):
                    nameoff, value, size, info, other, shndx = struct.unpack_from(
                        "<IIIIHH", self.d, so + i * 16)
                    q = self.d.find(b"\x00", do + nameoff)
                    self._dyn.append(self.d[do + nameoff:q].decode("latin-1"))
        return self._dyn

    # ---------- 工具 ----------
    def cstr(self, addr, maxlen=120):
        f = self.e.off(addr)
        if f is None:
            return None
        z = self.d.find(b"\x00", f)
        if z < 0 or not (2 <= z - f <= maxlen):
            return None
        s = self.d[f:z]
        if not all(32 <= b < 0x7F or b >= 0x80 for b in s):
            return None
        try:
            return s.decode("utf-8")
        except UnicodeDecodeError:
            try:
                return s.decode("gb18030")
            except Exception:
                return None

    def resolve(self, v):
        if v is None or v < 0x1000:
            return None
        s = self.cstr(v)
        if s:
            return repr(s)
        n = self.sym_at(v)
        if n:
            return n[0] + ("+0x%x" % (v - n[1]) if v != n[1] else "")
        return None


def rot_imm(imm12):
    imm8 = imm12 & 0xFF
    rot = ((imm12 >> 8) & 0xF) * 2
    if rot == 0:
        return imm8
    return ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF


def dis(path, start, end=None, maxn=400):
    E = ArmElf(path)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    if end is None:
        end = start + maxn * 4
    lines = []
    n = 0
    for ins in md.disasm(E.e.bytes_at(start, end - start) or b"", start):
        if ins.address >= end:
            break
        n += 1
        if n > maxn:
            break
        ann = ""
        m = ins.mnemonic
        ops = ins.operands
        if m in ("bl", "blx", "b") and ops and ops[0].type == ARM_OP_IMM:
            t = ops[0].imm
            if t in E.plt:
                ann = "  ; PLT-> " + E.plt[t]
            else:
                s = E.sym_at(t)
                if s:
                    ann = "  ; " + s[0]
                elif m in ("bl", "blx"):
                    ann = "  ; sub_%X" % t
        elif m.startswith("ldr") and len(ops) >= 2 and ops[1].type == ARM_OP_MEM:
            mem = ops[1].mem
            base = ins.reg_name(mem.base) if mem.base else ""
            if base == "pc" and mem.disp >= 0:
                lit = ins.address + 8 + mem.disp
                v = E.e.word(lit)
                if v is not None:
                    r = E.resolve(v)
                    ann = "  ; [%X]=%08X" % (lit, v) + (("  " + r) if r else "")
            elif base == "pc":
                lit = ins.address + 8 + mem.disp
                v = E.e.word(lit)
                if v is not None:
                    r = E.resolve(v)
                    ann = "  ; [%X]=%08X" % (lit, v) + (("  " + r) if r else "")
        lines.append("  %08X: %-8s %-28s%s" % (ins.address,
                                               ins.bytes.hex() if hasattr(ins.bytes, 'hex') else "",
                                               m + " " + ins.op_str, ann))
    return "\n".join(lines)


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        sys.exit(0)
    path = a[0]
    rest = a[1:]
    maxn = 400
    if "--n" in rest:
        i = rest.index("--n"); maxn = int(rest[i + 1]); del rest[i:i + 2]
    if "--syms" in rest:
        i = rest.index("--syms"); kw = rest[i + 1]
        E = ArmElf(path)
        for v, nm, sz in E.syms:
            if kw.lower() in nm.lower():
                print("%08X %6d  %s" % (v, sz, nm))
        sys.exit(0)
    if "--sym" in rest:
        i = rest.index("--sym"); kw = rest[i + 1]
        E = ArmElf(path)
        for v, nm, sz in E.syms:
            if kw in nm:
                print("== %s @ %08X size=%d" % (nm, v, sz))
                print(dis(path, v, v + max(sz, 4) if sz else v + maxn * 4, maxn))
                break
        sys.exit(0)
    if "-" in rest[0] and len(rest[0].split("-")) == 2:
        s, e = rest[0].split("-")
        print(dis(path, int(s, 16), int(e, 16), maxn))
    else:
        print(dis(path, int(rest[0], 16), None, maxn))

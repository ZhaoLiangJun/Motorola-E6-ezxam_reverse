# -*- coding: utf-8 -*-
"""audit_all.py —— 全量函数级对比审计：重建库 vs 原厂库

对两个库里**同名导出函数**逐个对比四类指纹：

  1) 调用序列   BL 目标解析成符号名后的有序列表 → 找「原库有、我们没有」的调用
  2) this 写入  函数体内 `str rX,[r0,#imm]` 的偏移集合 → 暴露成员布局差异
                 也用来判 static / 非 static（有 this 写入 = 有 this）
  3) 分配大小   `mov r0,#N` 紧跟 `bl operator new` 的 N → 暴露空壳类 sizeof 不符
  4) 体量比     ours_size / orig_size

输出按风险排序的表格，供人工逐条判定。
用法: python audit_all.py [ours.so] [orig.so]
"""
import os
import re
import struct
import sys
from collections import Counter

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
OURS = sys.argv[1] if len(sys.argv) > 1 else 'libezxam_reconstructed.so.1.0.0'
ORIG = sys.argv[2] if len(sys.argv) > 2 else 'orig_local.so'


# ---------------------------------------------------------------- ELF 解析
ADDR2SIZE = {}          # 当前分析库的 函数地址 -> 体量（供别名跟随用）
NAME2ADDR = {}          # 当前分析库的 符号名 -> 地址


def load_elf(path):
    d = open(path, 'rb').read()
    phoff = struct.unpack_from('<I', d, 0x1c)[0]
    phes = struct.unpack_from('<H', d, 0x2a)[0]
    phn = struct.unpack_from('<H', d, 0x2c)[0]
    segs = []
    for i in range(phn):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<IIIIIIII', d, phoff + i * phes)
        if t == 1 and fsz:
            segs.append((va, off, fsz))
    shoff = struct.unpack_from('<I', d, 0x20)[0]
    shent = struct.unpack_from('<H', d, 0x2E)[0]
    shn = struct.unpack_from('<H', d, 0x30)[0]
    shs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shent) for i in range(shn)]
    shstr = shs[struct.unpack_from('<H', d, 0x32)[0]]
    nb = d[shstr[4]:shstr[4] + shstr[5]]
    by = {}
    for s in shs:
        nm = nb[s[0]:nb.find(b'\0', s[0])].decode('latin1')
        by.setdefault(nm, s)
    return d, segs, by


def cstr(d, segs, va, n=96):
    for v, o, s in segs:
        if v <= va < v + s:
            b = d[o + va - v:o + va - v + n]
            return b[:b.find(b'\0')].decode('latin1', 'replace')
    return None


def exports(d, segs, by):
    """返回 {name: (addr, size)}，只取已定义函数"""
    ds, st = by['.dynsym'], by['.dynstr']
    strb = d[st[4]:st[4] + st[5]]
    out = {}
    for i in range(ds[5] // ds[9]):
        o = ds[4] + i * ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        if not name or shndx == 0:
            continue
        typ = info & 0xf
        if typ != 2:                      # STT_FUNC
            continue
        e = strb.find(b'\0', name)
        nm = strb[name:e].decode('latin1')
        out[nm] = (val, size)
    return out


def pltmap(d, by):
    plt = by.get('.plt')
    rel = by.get('.rel.plt') or by.get('.rel.dyn')
    if not plt or not rel:
        return {}
    ds, st = by['.dynsym'], by['.dynstr']
    strb = d[st[4]:st[4] + st[5]]
    base = plt[3]
    out = {}
    for i in range(rel[5] // rel[9]):
        o = rel[4] + i * rel[9]
        off, info = struct.unpack_from('<II', d, o)
        if (info & 0xff) != 22:
            continue
        idx = info >> 8
        no = ds[4] + idx * ds[9]
        name = struct.unpack_from('<I', d, no)[0]
        e = strb.find(b'\0', name)
        out[base + 20 + i * 12] = strb[name:e].decode('latin1')
    return out


# ---------------------------------------------------------------- 指纹提取
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
md.detail = True


def fingerprint(d, segs, addr, size, pmap, addr2name):
    """返回 (calls, this_offsets, new_sizes)

    GCC 会把 C1/C2/C4（或 D0/D1/D2/D4）中的一个建成"真身"，其余建成
    4~8 字节的 `b <真身>` 跳转别名（alias thunk）。原库与我们的**别名方向可能相反**，
    所以遇到别名要先跟随到真身再比，否则会把"4B thunk"误判成"空壳"。
    """
    seen = set()
    for _ in range(4):
        calls, offs, news, alias = _fp1(d, segs, addr, size, pmap, addr2name)
        if alias is None or alias in seen:
            break
        seen.add(alias)
        addr, size = alias, _size_at(d, segs, addr2name, pmap, alias)
        if not size:
            break
    return calls, offs, news


def _size_at(d, segs, addr2name, pmap, addr):
    return ADDR2SIZE.get(addr, 0)


def _fp1(d, segs, addr, size, pmap, addr2name):
    for v, o, s in segs:
        if v <= addr < v + s:
            code = d[o + addr - v:o + addr - v + size]
            break
    else:
        return [], set(), [], None

    calls, offs, news = [], [], []
    prev_mov_r0 = None
    insns = list(md.disasm(code, addr))
    # 别名判定：整段只有一条 b / bx（没有任何 bl/str）
    # 典型形态有两种：
    #   ① b <同一函数族另一个别名>      —— GCC 内部直接跳
    #   ② b <自身另一别名的 PLT 桩>      —— -fPIC 下 C4 → PLT(C2) → 真身
    # 两种都要跟随，否则"4B thunk"会被当成空壳，误报满天飞。
    if len(insns) == 1 and insns[0].mnemonic in ('b', 'bx') and insns[0].operands \
            and insns[0].operands[0].type == capstone.arm.ARM_OP_IMM:
        tgt = insns[0].operands[0].imm
        nm = pmap.get(tgt)
        if nm and nm in NAME2ADDR:
            return [], set(), [], NAME2ADDR[nm]
        return [], set(), [], tgt

    for ins in insns:
        w = ins.mnemonic
        ops = ins.op_str
        if w == 'bl':
            tgt = ins.operands[0].imm if ins.operands and ins.operands[0].type == capstone.arm.ARM_OP_IMM else 0
            nm = pmap.get(tgt) or addr2name.get(tgt)
            if nm is None and tgt:
                nm = '<0x%x>' % tgt
            if nm:
                calls.append(nm)
                if 'operator new' in nm or nm == '_Znwj':
                    if prev_mov_r0 is not None:
                        news.append(prev_mov_r0)
            prev_mov_r0 = None
            continue
        if w == 'mov' and ops.startswith('r0, #'):
            try:
                prev_mov_r0 = int(ops.split('#')[1], 0)
            except Exception:
                prev_mov_r0 = None
            continue
        if w == 'str' and re.match(r'^r\d+, \[r0, #\d+\]$', ops):
            m = re.match(r'^r\d+, \[r0, #(\d+)\]$', ops)
            if m:
                offs.append(int(m.group(1)))
        prev_mov_r0 = None
    return calls, set(offs), news, None


def build(path):
    d, segs, by = load_elf(path)
    ex = exports(d, segs, by)
    pmap = pltmap(d, by)
    addr2name = {}
    for nm, (a, s) in ex.items():
        addr2name.setdefault(a, nm)
    ADDR2SIZE.clear()
    NAME2ADDR.clear()
    for nm, (a, s) in ex.items():
        NAME2ADDR[nm] = a
        if s:
            ADDR2SIZE[a] = s
    fps = {}
    for nm, (a, s) in ex.items():
        if s == 0:
            continue
        c, o, n = fingerprint(d, segs, a, s, pmap, addr2name)
        # 别名跟随后按真身体量计，避免"4B thunk"被误判成空壳
        eff, cur, seen = s, a, set()
        while True:
            tgt = _fp1(d, segs, cur, ADDR2SIZE.get(cur, eff), pmap, addr2name)[3]
            if tgt is None or tgt in seen or tgt not in ADDR2SIZE:
                break
            seen.add(tgt)
            cur = tgt
            eff = ADDR2SIZE[cur]
        fps[nm] = (eff, c, o, n)
    return fps


def shorten(nm):
    """把修饰名压成人能读的形式"""
    if nm.startswith('_Z'):
        try:
            sys.path.insert(0, HERE)
            from demangle import demangle as dm
            r = dm(nm)
            if r:
                return r
        except Exception:
            pass
    return nm


def main():
    A = build(OURS)      # ours
    B = build(ORIG)      # orig
    common = sorted(set(A) & set(B))

    rows = []
    for nm in common:
        sA, cA, oA, nA = A[nm]
        sB, cB, oB, nB = B[nm]
        miss = [c for c in cB if c not in cA]
        extra = [c for c in cA if c not in cB]
        miss_c = Counter(miss)
        extra_c = Counter(extra)
        # 去掉"次数差异"里我们更多的那类噪声：只保留原库独有调用
        ratio = sA / float(sB) if sB else 99
        sev = 0
        if miss_c:
            sev += 40 + 10 * len(miss_c)
        if nB and nA != nB:
            sev += 35
        if oB and not oA:
            sev += 30                      # 原库写 this 我们不写 → 可能是 static 误判
        if oA and not oB:
            sev += 20
        if ratio < 0.15:
            sev += 12
        if extra_c:
            sev += min(6, len(extra_c))
        rows.append((sev, nm, sA, sB, ratio, miss_c, extra_c, nA, nB, oA, oB))

    rows.sort(key=lambda r: (-r[0], r[1]))

    print('对比: %s (%d 函数)  vs  %s (%d 函数)' % (OURS, len(A), ORIG, len(B)))
    print('共有函数 %d 个\n' % len(common))

    flag = [r for r in rows if r[0] >= 20]
    print('=== 需人工判定 %d 项（按风险降序）===' % len(flag))
    for sev, nm, sA, sB, ratio, miss, extra, nA, nB, oA, oB in flag:
        print('\n[sev %d] %s' % (sev, shorten(nm)))
        print('    体量 ours=%dB orig=%dB (%.0f%%)' % (sA, sB, ratio * 100))
        if miss:
            print('    原库有、我们无的调用: ' + ', '.join(
                '%s%s' % (shorten(k), '' if v == 1 else 'x%d' % v) for k, v in miss.most_common()))
        if extra:
            print('    我们多出的调用: ' + ', '.join(
                '%s%s' % (shorten(k), '' if v == 1 else 'x%d' % v) for k, v in extra.most_common()))
        if nB and nA != nB:
            print('    operator new 尺寸 ours=%s orig=%s  <<< 尺寸不符' % (nA, nB))
        if oB and not oA:
            print('    this 写入 ours=无 orig=%s  <<< 疑似 static/非static 判错' % sorted(oB))
        elif oA and not oB:
            print('    this 写入 ours=%s orig=无' % sorted(oA))

    clean = [r for r in rows if r[0] < 20]
    print('\n=== 无风险信号 %d 项 ===' % len(clean))
    for sev, nm, sA, sB, ratio, miss, extra, nA, nB, oA, oB in clean[:40]:
        print('  %-70s ours=%dB orig=%dB (%.0f%%)' % (shorten(nm)[:70], sA, sB, ratio * 100))


if __name__ == '__main__':
    main()

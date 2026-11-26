# -*- coding: utf-8 -*-
"""where.py - 把崩溃现场的地址反查到具体模块+函数

用法:
  python where.py _am_crash.log                 # 解析日志里所有 CRASH 段
  python where.py 0x41af27fc 0x42326268         # 直接查若干地址
  python where.py --regs "pc=41af27fc lr=41ae1234"

背景（EZX/E6 这套库全是 prelink 过的固定基址加载）：
  * libezxam 固定装在 0x41ad8000 起（/proc/<pid>/maps 实测），
    且 ELF 里的符号 vaddr 本身就是 0x41axxxxx → **运行地址 == 符号 vaddr**，不用减基址。
  * am 是 0x8000 起的非 PIE 可执行文件，同样 运行地址 == vaddr。
  * ezxaid.so 的 vaddr 是 0x4231xxxx，也是 prelink 绝对地址，同理。
  所以这里只要"判断地址落在哪个模块的 .text 区间"，再在 dynsym 里找最近的符号即可。
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edis import ArmElf

HERE = os.path.dirname(os.path.abspath(__file__))

# 候选模块：文件 -> 显示名
MODULES = [
    ('libezxam_reconstructed.so.1.0.0', 'libezxam(我们)'),
    ('orig_local.so',                   'libezxam(原库)'),
    ('am_dev',                          'am'),
    ('ezxaid.so',                       'ezxaid'),
    ('appbase_local.so',                'libezxappbase'),
    ('libc_dev.so',                     'libc'),
]

_cache = {}


def mods():
    if _cache:
        return _cache
    for fn, disp in MODULES:
        p = os.path.join(HERE, fn)
        if not os.path.exists(p):
            continue
        try:
            E = ArmElf(p)
        except Exception as e:
            print('# skip %s: %s' % (fn, e))
            continue
        # 取 .text 区间
        t = E.by_name.get('.text')
        if not t:
            continue
        _cache[fn] = (disp, t[1], t[1] + t[3], E)
    return _cache


def resolve(addr):
    addr &= 0xFFFFFFFF
    for fn, (disp, lo, hi, E) in mods().items():
        if lo <= addr < hi:
            s = E.sym_at(addr)
            if s:
                nm, base, sz = s
                return '%s + 0x%X (%s, size=%d)' % (disp, addr - lo, nm, sz)
            return '%s + 0x%X (无导出符号；绝对地址 0x%X，按 sub_%X 查反汇编)' % (
                disp, addr - lo, addr, addr)
    return None


RELOC_BASES = {
    # 运行时基址 -> 名字（模块不在 text 区间时用来提示"这是数据段/栈"）
}


def looks_code(a):
    return (a >> 24) in (0x41, 0x42, 0x40, 0x00) and a > 0x1000


def parse_crash(text):
    """从崩溃日志里取出 CRASH 段"""
    blocks = []
    cur = None
    for line in text.splitlines():
        if '==== CRASH' in line:
            cur = [line]
        elif cur is not None:
            cur.append(line)
            if 'CRASH_END' in line:
                blocks.append('\n'.join(cur))
                cur = None
    if cur:
        blocks.append('\n'.join(cur))
    return blocks


def dump_block(b):
    print(b)
    print('-' * 70)
    # 重点寄存器
    for name in ('pc', 'lr', 'sp', 'r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r12', 'ip'):
        m = re.search(r'\b%s\b\s*[=:]\s*(?:0x)?([0-9a-fA-F]{6,8})' % name, b)
        if not m:
            continue
        v = int(m.group(1), 16)
        r = resolve(v)
        if r:
            print('  %-3s = %08X  -> %s' % (name.upper(), v, r))
    # 栈里所有像代码地址的字
    print('  --- 栈中的代码地址 ---')
    seen = set()
    for m in re.finditer(r'(?:0x|00)?([0-9a-fA-F]{8})', b):
        v = int(m.group(1), 16)
        if v in seen:
            continue
        seen.add(v)
        r = resolve(v)
        if r:
            print('    %08X -> %s' % (v, r))


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        sys.exit(0)
    if a[0] == '--regs':
        for m in re.finditer(r'([a-z]{2,3})\s*[=:]\s*(?:0x)?([0-9a-fA-F]{6,8})', a[1]):
            v = int(m.group(2), 16)
            print('%-3s %08X -> %s' % (m.group(1), v, resolve(v)))
        sys.exit(0)
    if len(a) == 1 and os.path.exists(a[0]):
        text = open(a[0], 'r', errors='replace').read()
        bs = parse_crash(text)
        print('# 找到 %d 个 CRASH 段' % len(bs))
        for b in bs:
            dump_block(b)
        sys.exit(0)
    for x in a:
        v = int(x, 16) if not x.lower().startswith('0x') else int(x, 16)
        print('%08X -> %s' % (v, resolve(v)))

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""audit_sig.py - 提取原库导出的类方法符号并简易 demangle，
   用于与头文件声明对照（mangled 含参数类型，不含返回类型）"""
import struct, sys
import vt_compare as V

# ---- 简易 Itanium demangle（只处理本项目用到的形态）----
BUILTIN = {'i': 'int', 'j': 'unsigned', 's': 'short', 't': 'unsigned short',
           'b': 'bool', 'c': 'char', 'd': 'double', 'f': 'float', 'v': 'void',
           'l': 'long', 'm': 'unsigned long', 'w': 'wchar_t', 'a': 'signed char',
           'h': 'unsigned char', 'x': 'long long', 'y': 'unsigned long long'}
KNOWN = {
    '7QString': 'QString', '9QUuid': 'QUuid', '7QPixma': None,  # 占位
}


def demangle(m):
    """解析 _ZN...E 形式，返回 (类名, 方法名, 参数列表字符串)"""
    if not (m.startswith('_ZN') and m.endswith('E')):
        return None
    body = m[3:-1]
    names = []
    i = 0
    try:
        while i < len(body):
            if body[i].isdigit():
                j = i
                while j < len(body) and body[j].isdigit():
                    j += 1
                ln = int(body[i:j])
                if ln <= 0 or i + j + ln > len(body):
                    return None
                names.append(body[i + j:i + j + ln])
                i = i + j + ln
            else:
                break
        if len(names) < 2:
            return None
        args = body[i:]
        cls = '::'.join(names[:-1])
        meth = names[-1]
        # 解析参数
        params = []
        k = 0
        prev_map = {}  # S_ substitution
        subs = []
        while k < len(args):
            c = args[k]
            if c == 'R':
                k += 1
                continue  # 引用（在下一 token 前）
            if c == 'K':
                k += 1
                continue  # const
            if c == 'P':
                k += 1
                continue  # 指针
            if c == 'S':
                # S_ / S0_ ... 引用先前子项
                j = k + 1
                while j < len(args) and args[j].isdigit():
                    j += 1
                if j < len(args) and args[j] == '_':
                    idx = 0 if j == k + 1 else int(args[k + 1:j]) + 1
                    if idx < len(subs):
                        params.append(subs[idx] + ('&' if False else ''))
                    k = j + 1
                    continue
                # St=std 等，简单处理
                k = j
                continue
            if c == 'A':
                # 数组 A<n>_，跳到 _
                j = args.find('_', k)
                if j < 0:
                    break
                k = j + 1
                continue
            if c == 'N':
                # 嵌套名 N...E
                j = args.find('E', k)
                if j < 0:
                    break
                nm = args[k + 1:j]
                # 解析嵌套
                parts = []
                p = 0
                while p < len(nm):
                    if nm[p].isdigit():
                        q = p
                        while q < len(nm) and nm[q].isdigit():
                            q += 1
                        ln = int(nm[p:q])
                        parts.append(nm[p + q:p + q + ln])
                        p = p + q + ln
                    else:
                        p += 1
                nm2 = '::'.join(parts)
                params.append(nm2)
                subs.append(nm2)
                k = j + 1
                continue
            if c in BUILTIN:
                params.append(BUILTIN[c])
                subs.append(BUILTIN[c])
                k += 1
                continue
            if c.isdigit():
                j = k
                while j < len(args) and args[j].isdigit():
                    j += 1
                ln = int(args[k:j])
                if ln <= 0 or j + ln > len(args):
                    break
                nm = args[j:j + ln]
                params.append(nm)
                subs.append(nm)
                k = j + ln
                continue
            if c == 'e':  # varargs
                params.append('...')
                k += 1
                continue
            if c == 'v' and not params:
                params.append('()')
                k += 1
                continue
            break
        return cls, meth, params
    except Exception:
        return None


def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else 'orig_local.so'
    e = V.Elf(lib)
    rows = []
    seen = set()
    for a, nm in e.defsyms.items():
        if not nm.startswith('_ZN'):
            continue
        d = demangle(nm)
        if not d:
            continue
        cls, meth, params = d
        key = (cls, meth, tuple(params))
        if key in seen:
            continue
        seen.add(key)
        rows.append((cls, meth, params, a))
    rows.sort()
    cur = None
    for cls, meth, params, a in rows:
        if cls != cur:
            print("\n===== %s =====" % cls)
            cur = cls
        print("  %-45s (%s)   @0x%08x" % (meth, ', '.join(params) if params else '', a))
    print("\n共 %d 个方法" % len(rows))


if __name__ == '__main__':
    main()

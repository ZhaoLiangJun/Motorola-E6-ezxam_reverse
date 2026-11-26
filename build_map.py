import struct, json, re

p = r'C:/Users/ZhaoLiangJun/Documents/E6/libezxam.so.1.0.0'
with open(p,'rb') as f:
    data = f.read()

# 提取完整符号表
dynsym_off = 0x1748; dynsym_size = 0x3a20; dynstr_off = 0x5168; dynstr_size = 0x7add; entsize = 16
dynstr = data[dynstr_off:dynstr_off+dynstr_size]
def get_str(off):
    end = dynstr.find(b'\x00', off)
    if end == -1: end = len(dynstr)
    return dynstr[off:end].decode('utf-8','replace')

# 完整demangler - 处理所有C++符号
def demangle_full(mangled):
    if not mangled.startswith('_Z'):
        return mangled
    s = mangled[2:]
    parts = []
    i = 0
    while i < len(s) and s[i].isdigit():
        j = i
        while j < len(s) and s[j].isdigit():
            j += 1
        length = int(s[i:j])
        name = s[j:j+length]
        parts.append(name)
        i = j + length
    if not parts:
        return mangled
    
    # 处理运算符/特殊
    if len(parts) >= 2:
        cls = parts[0]
        method = parts[1]
        if method == 'C1' or method == 'C2' or method == 'C3':
            method = cls
        elif method == 'D0' or method == 'D1' or method == 'D2' or method == 'D3':
            method = '~' + cls
        return f"{cls}::{method}"
    return parts[0]

# 解析参数简化（提取类名）
def extract_args_types(args):
    """返回参数类型列表（简化）"""
    result = []
    i = 0
    while i < len(args):
        c = args[i]
        if c == 'E':
            break
        if c == 'R': result.append('&'); i += 1; continue
        if c == 'P': result.append('*'); i += 1; continue
        if c == 'K': i += 1; continue
        if c.isdigit():
            j = i
            while j < len(args) and args[j].isdigit():
                j += 1
            length = int(args[i:j])
            name = args[j:j+length]
            result.append(name)
            i = j + length
            continue
        types = {'v':'void','b':'bool','c':'char','s':'short','i':'int','l':'long','f':'float','d':'double','j':'unsigned int','m':'unsigned long'}
        if c in types:
            result.append(types[c])
            i += 1
            continue
        i += 1
    return result

# 提取符号
symbols = {}
for i in range(dynsym_size // entsize):
    off = dynsym_off + i*entsize
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, off)
    name = get_str(st_name) if st_name else ''
    bind = (st_info >> 4) & 0xf
    type_ = st_info & 0xf
    if name and st_value != 0:
        symbols[st_value] = {
            'mangled': name,
            'demangled': demangle_full(name),
            'size': st_size,
            'bind': bind,
            'type': type_,
            'defined': st_shndx != 0,
            'shndx': st_shndx
        }

with open('func_addr_map.json', 'w') as f:
    json.dump({str(k): v for k, v in symbols.items()}, f, indent=2, ensure_ascii=False)

# 读取all_decompiled.c
content = open('ghidra/decompiled/all_decompiled.c', encoding='utf-8', errors='replace').read()
parts = re.split(r'/\* ==== ', content)
funcs = []
for p in parts:
    m = re.match(r'(.+?) @ (\w+) ==== \*/\n(.*)', p, re.DOTALL)
    if m:
        funcs.append((int(m.group(2),16), m.group(1), m.group(3)))
funcs.sort(key=lambda x:x[0])

# 匹配
text_start = 0x41aea320
text_end = 0x41b17f08
real_funcs = [(a,n,b) for a,n,b in funcs if text_start <= a < text_end]

# 建立最终映射
final_map = {}
for addr, name, body in real_funcs:
    entry = {'addr': addr, 'ghidra_name': name, 'body': body}
    if addr in symbols:
        entry.update(symbols[addr])
        # 从mangled名提取完整方法信息
        if symbols[addr]['demangled'].startswith('_Z'):
            pass  # 用mangled的解析
        dm = symbols[addr]['demangled']
        if '::' in dm:
            entry['class'] = dm.split('::')[0]
            entry['method'] = dm.split('::')[1]
        else:
            entry['class'] = dm
            entry['method'] = ''
    final_map[addr] = entry

with open('func_map.json', 'w') as f:
    json.dump({str(k): v for k, v in final_map.items()}, f, indent=2, ensure_ascii=False)

print(f"真实函数数: {len(real_funcs)}")
print(f"有符号的: {sum(1 for v in final_map.values() if 'class' in v)}")
print(f"无符号的: {sum(1 for v in final_map.values() if 'class' not in v)}")

# 按类分组输出
classes = {}
for addr, e in final_map.items():
    if 'class' in e:
        classes.setdefault(e['class'], []).append(e)
print(f"\n类数: {len(classes)}")
for cls in sorted(classes.keys()):
    print(f"  {cls}: {len(classes[cls])}")

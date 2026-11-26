import struct, json, sys

p = r'C:/Users/ZhaoLiangJun/Documents/E6/libezxam.so.1.0.0'
with open(p,'rb') as f:
    data = f.read()

# 用 c++filt 风格的demangle（完整实现 Itanium ABI 关键部分）
def demangle(mangled):
    if not mangled.startswith('_Z'):
        return mangled
    s = mangled[2:]
    
    # 解析名字段
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
    
    # 处理构造函数/析构函数
    def parse_args(args):
        """解析参数类型，返回参数字符串"""
        result = []
        i = 0
        while i < len(args):
            c = args[i]
            if c == 'E':
                break
            if c == 'v' and i+1 < len(args) and args[i+1] == 'E':
                break
            if c in 'R':
                result.append('&')
                i += 1
                continue
            if c in 'P':
                result.append('*')
                i += 1
                continue
            if c in 'K':
                i += 1
                continue
            if c.isdigit():
                j = i
                while j < len(args) and args[j].isdigit():
                    j += 1
                length = int(args[i:j])
                name = args[j:j+length]
                result.append(name)
                i = j + length
                continue
            # 内置类型
            types = {'v':'void','b':'bool','c':'char','s':'short','i':'int','l':'long','f':'float','d':'double','j':'unsigned int','m':'unsigned long','t':'unsigned short'}
            if c in types:
                result.append(types[c])
                i += 1
                continue
            # 未知，跳到下一个
            result.append('?')
            i += 1
        return result

    if len(parts) == 1:
        return parts[0]
    
    cls = parts[0]
    method = parts[1]
    
    # 特殊处理构造函数/析构函数
    if method == 'C1' or method == 'C2' or method == 'C3':
        method = cls  # 构造函数
    elif method == 'D0' or method == 'D1' or method == 'D2' or method == 'D3':
        method = '~' + cls  # 析构函数
    
    args_str = s[i:] if i < len(s) else ''
    
    # 解析参数
    arg_types = parse_args(args_str)
    
    if args_str == 'Ev' or not arg_types:
        return f"{cls}::{method}()"
    else:
        return f"{cls}::{method}({', '.join(arg_types)})"

# 提取符号表
dynsym_off = 0x1748; dynsym_size = 0x3a20; dynstr_off = 0x5168; dynstr_size = 0x7add; entsize = 16
dynstr = data[dynstr_off:dynstr_off+dynstr_size]
def get_str(off):
    end = dynstr.find(b'\x00', off)
    if end == -1: end = len(dynstr)
    return dynstr[off:end].decode('utf-8','replace')

symbols = {}
for i in range(dynsym_size // entsize):
    off = dynsym_off + i*entsize
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, off)
    name = get_str(st_name) if st_name else ''
    bind = (st_info >> 4) & 0xf
    type_ = st_info & 0xf
    if name and type_ == 2 and st_shndx != 0 and st_value != 0:
        symbols[st_value] = {'mangled': name, 'demangled': demangle(name), 'size': st_size, 'bind': bind}

with open('func_addr_map.json', 'w') as f:
    json.dump({str(k): v for k, v in symbols.items()}, f, indent=2, ensure_ascii=False)

# 按类分组
classes = {}
for addr, info in symbols.items():
    dm = info['demangled']
    if '::' in dm:
        cls = dm.split('::')[0]
        classes.setdefault(cls, []).append((addr, info))

print(f"定义函数: {len(symbols)}")
print(f"类数: {len(classes)}")
for cls in sorted(classes.keys()):
    print(f"\n[{cls}] ({len(classes[cls])})")
    for addr, info in sorted(classes[cls]):
        print(f"  0x{addr:08x}  {info['demangled']}")

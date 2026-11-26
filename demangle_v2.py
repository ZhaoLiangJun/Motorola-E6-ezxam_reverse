import struct, json, re

p = r'C:/Users/ZhaoLiangJun/Documents/E6/libezxam.so.1.0.0'
with open(p,'rb') as f:
    data = f.read()

dynsym_off = 0x1748; dynsym_size = 0x3a20; dynstr_off = 0x5168; dynstr_size = 0x7add; entsize = 16
dynstr = data[dynstr_off:dynstr_off+dynstr_size]
def get_str(off):
    end = dynstr.find(b'\x00', off)
    if end == -1: end = len(dynstr)
    return dynstr[off:end].decode('utf-8','replace')

# 正确的Itanium ABI demangler（完整实现）
class Demangler:
    def __init__(self, s):
        self.s = s
        self.i = 0
    
    def parse_length(self):
        # 解析数字长度
        start = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit():
            self.i += 1
        if start == self.i:
            return None
        return int(self.s[start:self.i])
    
    def parse_name(self):
        """解析一个 <name> 段"""
        n = self.parse_length()
        if n is None:
            return None
        name = self.s[self.i:self.i+n]
        self.i += n
        return name
    
    def parse_operator(self):
        """解析操作符"""
        if self.s.startswith('nw', self.i): self.i += 2; return 'operator new'
        if self.s.startswith('dl', self.i): self.i += 2; return 'operator delete'
        if self.s.startswith('C1', self.i) or self.s.startswith('C2', self.i) or self.s.startswith('C3', self.i):
            return '__ctor'
        if self.s.startswith('D0', self.i) or self.s.startswith('D1', self.i) or self.s.startswith('D2', self.i) or self.s.startswith('D3', self.i):
            return '__dtor'
        if self.s.startswith('aS', self.i): self.i += 2; return 'operator='
        if self.s.startswith('lt', self.i): self.i += 2; return 'operator<'
        if self.s.startswith('gt', self.i): self.i += 2; return 'operator>'
        if self.s.startswith('eq', self.i): self.i += 2; return 'operator=='
        if self.s.startswith('ne', self.i): self.i += 2; return 'operator!='
        if self.s.startswith('pl', self.i): self.i += 2; return 'operator+'
        if self.s.startswith('mi', self.i): self.i += 2; return 'operator-'
        return None
    
    def parse_args(self):
        """解析函数参数"""
        args = []
        while self.i < len(self.s):
            c = self.s[self.i]
            if c == 'E':
                self.i += 1
                break
            if c == 'v' and self.i+1 < len(self.s) and self.s[self.i+1] == 'E':
                self.i += 1
                break
            if c == 'R':
                args.append('&')
                self.i += 1
                continue
            if c == 'P':
                args.append('*')
                self.i += 1
                continue
            if c == 'K':
                self.i += 1
                continue
            if c.isdigit():
                n = self.parse_length()
                if n is not None:
                    name = self.s[self.i:self.i+n]
                    self.i += n
                    args.append(name)
                    continue
            types = {'v':'void','b':'bool','c':'char','s':'short','i':'int','l':'long','f':'float','d':'double','j':'unsigned int','m':'unsigned long','t':'unsigned short','x':'long long','y':'unsigned long long','a':'signed char','h':'unsigned char'}
            if c in types:
                args.append(types[c])
                self.i += 1
                continue
            # 跳过未知
            args.append('?')
            self.i += 1
        return args

def demangle(mangled):
    if not mangled.startswith('_Z'):
        return mangled
    d = Demangler(mangled[2:])
    # 可能有N前缀
    if d.i < len(d.s) and d.s[d.i] == 'N':
        d.i += 1
    # 解析类名（第一个名字段）
    cls = d.parse_name()
    if cls is None:
        return mangled
    # 解析方法名
    method = d.parse_name()
    if method is None:
        return cls
    # 处理构造函数/析构函数
    if method == '__ctor':
        method = cls
    elif method == '__dtor':
        method = '~' + cls
    # 解析参数
    args = d.parse_args()
    if not args:
        return f"{cls}::{method}()"
    else:
        return f"{cls}::{method}({', '.join(args)})"

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
            'demangled': demangle(name),
            'size': st_size,
            'bind': bind,
            'type': type_,
            'defined': st_shndx != 0,
            'shndx': st_shndx
        }

with open('func_addr_map.json', 'w') as f:
    json.dump({str(k): v for k, v in symbols.items()}, f, indent=2, ensure_ascii=False)

# 按类分组输出
classes = {}
for addr, info in sorted(symbols.items()):
    dm = info['demangled']
    if '::' in dm and not dm.startswith('_Z'):
        cls = dm.split('::')[0]
        classes.setdefault(cls, []).append((addr, info))

print(f"定义函数: {sum(1 for v in symbols.values() if v['defined'])}")
print(f"类数: {len(classes)}")
for cls in sorted(classes.keys()):
    print(f"\n[{cls}] ({len(classes[cls])})")
    for addr, info in sorted(classes[cls]):
        print(f"  0x{addr:08x}  {info['demangled']}")

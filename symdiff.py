# -*- coding: utf-8 -*-
import struct, sys
def dyn(path):
    d=open(path,'rb').read()
    e_shoff=struct.unpack_from('<I',d,0x20)[0]; ents=struct.unpack_from('<H',d,0x2E)[0]; n=struct.unpack_from('<H',d,0x30)[0]
    secs=[struct.unpack_from('<IIIIIIIIII',d,e_shoff+i*ents) for i in range(n)]
    shstr=secs[struct.unpack_from('<H',d,0x32)[0]]
    nb=d[shstr[4]:shstr[4]+shstr[5]]
    def nm(x):
        e=nb.find(b'\0',x); return nb[x:e].decode('latin1')
    byname={}
    for s in secs: byname.setdefault(nm(s[0]), s)
    ds=byname['.dynsym']; st=secs[ds[6]]
    strb=d[st[4]:st[4]+st[5]]
    out={}
    for i in range(ds[5]//ds[9]):
        o=ds[4]+i*ds[9]
        name,val,size,info,other,shndx=struct.unpack_from('<IIIBBH',d,o)
        e=strb.find(b'\0',name)
        s=strb[name:e].decode('latin1')
        out[s]=(val,shndx)
    return out
def defined(t):
    return {k for k,(v,sh) in t.items() if sh!=0 and v!=0}
def undef(t):
    return {k for k,(v,sh) in t.items() if sh==0}
ezx=dyn('ezxaid.so')
orig=dyn('orig_local.so')
ours=dyn('libezxam_v35.so.1.0.0')
need=undef(ezx)
od=defined(orig); nd=defined(ours)
print('ezxaid 未定义符号:',len(need))
print('  其中原库提供:',len(need&od))
print('  我们缺失    :',len((need&od)-nd))
for k in sorted((need&od)-nd):
    print('   MISSING',hex(orig[k][0]),k)

print()
am=dyn('am_dev')
need2=undef(am)
print('am 未定义符号:',len(need2))
print('  其中原库提供:',len(need2&od))
miss2=sorted((need2&od)-nd)
print('  我们缺失    :',len(miss2))
for k in miss2:
    print('   MISSING',hex(orig[k][0]),k)

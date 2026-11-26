import sys, struct
sys.path.insert(0, r"D:\WorkBuddy\2026-11-08-18-54-48\ezxam_reverse")
from deploy_check import ELF

for label, path in [("重建库", r"D:\WorkBuddy\2026-11-08-18-54-48\ezxam_reverse\libezxam_reconstructed.so.1.0.0"),
                    ("原库", r"D:\cygwin\home\ezx\qt-2.3.8\lib\libezxam.so.1.0.0")]:
    print(f"=== {label} DT_INIT/DT_FINI ===")
    try:
        elf = ELF(path)
    except Exception as e:
        print("  解析失败:", e); continue
    dyn = elf.section(".dynamic")
    DTMAP = {12: "INIT", 13: "FINI", 12+0: "INIT"}
    o = dyn["offset"]; end = o + dyn["size"]
    while o + 8 <= end:
        tag = struct.unpack_from("<i", elf.data, o)[0]; val = elf._u32(o + 4); o += 8
        if tag == 0: break
        if tag in (12, 13):
            nm = "INIT" if tag == 12 else "FINI"
            sec = [s for s in elf.sections if s["addr"] == val]
            secn = sec[0]["name"] if sec else "?"
            print(f"  DT_{nm} addr={val:#x} -> section {secn}")
    for s in elf.sections:
        if s["name"] in (".init", ".fini"):
            print(f"  section {s['name']} addr={s['addr']:#x} size={s['size']}")
    print()

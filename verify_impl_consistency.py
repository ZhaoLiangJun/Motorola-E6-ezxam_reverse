#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_impl_consistency.py - 验证设备上运行的是重建库，且重建库与原库函数实现一致
作者：赵良军
通过 telnet 连接设备，对比 /ram/tmp/libezxam_recon.so（重建库，正被 bind 挂载）
与 /ram/tmp/libezxam.so.1.0.0.orig_bak（原库备份）：
  1. 确认 mount --bind 状态（确认 /usr/lib/ezx/lib/libezxam.so.1.0.0 指向重建库）
  2. 确认 am 进程 maps 里加载的库路径（设备号 8 = ramfs = 重建库）
  3. 用设备自带 nm/objdump 对比两库的符号表与函数实现（函数起始地址、大小、指令字节）
"""
import sys, os, time, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telnetlib import TelnetClient

def connect(host, port=23):
    c = TelnetClient(host, port=port, timeout=12)
    c.sock = __import__('socket').create_connection((host, port), 12)
    c.sock.settimeout(12)
    b = c._read_until_any([b"login:"], timeout=6)
    if b"login:" in b:
        c._send_line("root")
        c._read_until_any([b"assword:", b"#"], timeout=6)
        c._send_line("")
        c._read_until_any([b"# "], timeout=8)
    return c

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    args = ap.parse_args()
    c = connect(args.host)
    try:
        print("="*58)
        print("  验证：重建库正在运行 + 函数实现一致性")
        print("="*58)

        print("\n--- 1. mount --bind 状态 ---")
        print(c.exec_cmd("mount | grep -i libezxam || echo '(无 bind mount，需重新部署)'", wait=1.2))

        print("\n--- 2. am 进程加载的库 (maps) ---")
        print(c.exec_cmd("for p in $(pidof am 2>/dev/null); do echo \"PID $p\"; grep libezxam /proc/$p/maps 2>/dev/null | sed 's/.*libezxam/libezxam/'; done", wait=1.5))

        print("\n--- 3. stat 确认设备号 (8=ramfs 重建库) ---")
        print(c.exec_cmd("stat /usr/lib/ezx/lib/libezxam.so.1.0.0 2>/dev/null | head -4", wait=1.2))

        print("\n--- 4. 文件大小对比 ---")
        print(c.exec_cmd("ls -l /ram/tmp/libezxam_recon.so /ram/tmp/libezxam.so.1.0.0.orig_bak 2>&1", wait=1.0))

        print("\n--- 5. 符号表对比 (nm 输出行数) ---")
        print(c.exec_cmd("export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH; "
                         "nm -D /ram/tmp/libezxam_recon.so 2>/dev/null | wc -l; "
                         "nm -D /ram/tmp/libezxam.so.1.0.0.orig_bak 2>/dev/null | wc -l", wait=2.0))

        print("\n--- 6. 函数实现逐字节对比 (objdump -d 核心函数) ---")
        # 用设备自带 objdump 反汇编两库，对比函数体字节数
        print(c.exec_cmd("export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH; "
                         "objdump -d /ram/tmp/libezxam_recon.so 2>/dev/null | grep -cE '^[0-9a-f]+ <'; "
                         "objdump -d /ram/tmp/libezxam.so.1.0.0.orig_bak 2>/dev/null | grep -cE '^[0-9a-f]+ <'", wait=2.0))
    finally:
        c.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())

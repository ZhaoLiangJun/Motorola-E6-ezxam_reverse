#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""am 崩溃循环抓栈：监听 am 新 PID，立即 gdb attach 抓 bt/寄存器/PC"""
import socket, sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telnetlib import TelnetClient

T = "/mmc/mmca1/.system/arm-linux-gcc/bin"
GDB = T + "/gdb"
ENV = "export LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib; "


def conn():
    c = TelnetClient("192.168.16.2", port=23, timeout=30)
    c.sock = socket.create_connection(("192.168.16.2", 23), 30)
    c.sock.settimeout(30)
    c._read_until_any([b"login:"], timeout=10)
    c._send_line("root")
    c._read_until_any([b"assword:", b"#"], timeout=10)
    c._send_line("")
    c._read_until_any([b"# "], timeout=10)
    return c


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    c = conn()
    seen = set()
    got = 0
    try:
        for _ in range(120):
            out = c.exec_cmd("pidof am 2>/dev/null; echo ---", wait=0.8)
            pid = (out.split('\n')[0] or '').strip()
            if not pid or pid == '---':
                time.sleep(1)
                continue
            if pid in seen:
                time.sleep(1)
                continue
            seen.add(pid)
            got += 1
            print("\n########## 新 am PID=%s  (第 %d 次) ##########" % (pid, got))
            # 立刻抓：寄存器 + bt + 已加载库
            cmd = (ENV + GDB + " -batch -ex 'set confirm off' -ex 'set pagination off' "
                   "-ex 'attach %s' -ex 'info registers pc lr sp' -ex 'bt 20' "
                   "-ex 'x/8i $pc-16' -ex 'detach' 2>&1" % pid)
            r = c.exec_cmd(cmd, wait=20.0)
            for line in r.split('\n'):
                if any(k in line for k in ('#', 'pc ', 'lr ', 'sp ', '0x', 'Segmentation', 'ptrace',
                                           'Cannot', 'Not ', 'no such', 'warning')):
                    print(line)
            if got >= rounds:
                break
            time.sleep(1)
    finally:
        c.close()


if __name__ == '__main__':
    main()

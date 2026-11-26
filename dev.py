# -*- coding: utf-8 -*-
"""设备连接助手"""
import sys, os, time, socket
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telnetlib import TelnetClient

def connect(host='192.168.16.2', port=23):
    c = TelnetClient(host, port=port, timeout=20)
    c.sock = socket.create_connection((host, port), 20)
    c.sock.settimeout(20)
    c._read_until_any([b'login:'], timeout=10); c._send_line('root')
    c._read_until_any([b'assword:', b'#'], timeout=10); c._send_line('')
    c._read_until_any([b'# '], timeout=10)
    return c

def run(c, cmd, wait=2.0):
    """发送命令后固定读取（避免提示符 # 误匹配截断）。返回全部可读输出。"""
    c._send_line(cmd)
    time.sleep(0.3)
    time.sleep(max(0, wait - 0.3))
    out = b''
    try:
        while True:
            c.sock.settimeout(0.3)
            chunk = c.sock.recv(65536)
            if not chunk:
                break
            out += chunk
    except Exception:
        pass
    return out.decode('utf-8', 'replace')

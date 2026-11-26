# -*- coding: utf-8 -*-
"""sh2.py - 设备命令执行（哨兵结束标记版，v2）

为什么有 v2：
  v1 直接裸 socket、不等 IAC 协商，设备 telnetd 会一直停在协商阶段
  （实测只回 \\xff\\xfd\\x18\\xff\\xfd ... 后再无输出），表现为"命令没反应"。
  v2 改用 telnetlib.TelnetClient 的 IAC 拒绝逻辑，并用唯一哨兵串判定命令结束，
  不再依赖提示符。

用法:
  python sh2.py "cmd1" "cmd2" ...
  EZX_HOST=192.168.16.2 python sh2.py "uname -a"

输出约定:
  $ <cmd>
  <stdout/stderr>
  [rc=N]        # 仅当 rc != 0 时打印
"""
import sys, os, time, re, socket

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telnetlib import TelnetClient

HOST = os.environ.get('EZX_HOST', '192.168.16.2')
PORT = int(os.environ.get('EZX_PORT', '23'))


def connect(host=HOST, port=PORT):
    """登录状态机：设备 telnetd 在上一会话未清时会直接抛 Password:，
    所以不能只等一次 login:，要循环直到出现 shell 提示符 '# '。"""
    c = TelnetClient(host, port=port, timeout=10)
    c.sock = socket.create_connection((host, port), 10)
    c.sock.settimeout(10)
    acc = b''
    for _ in range(12):
        try:
            chunk = c._recv_some()          # 内部会回 WONT/DONT 拒掉 IAC
        except Exception:
            chunk = b''
        if chunk:
            acc += chunk
        if b'# ' in acc:
            return c
        if b'login:' in acc or b'ogin:' in acc:
            c.sock.sendall(b'root\n')
            acc = acc[acc.rfind(b'ogin:') + 6:]
        elif b'assword:' in acc:
            c.sock.sendall(b'\n')
            acc = acc[acc.rfind(b'assword:') + 8:]
        else:
            time.sleep(0.15)
    try:
        c.sock.close()
    except Exception:
        pass
    raise RuntimeError('login failed: %r' % acc[-200:])


def exec_cmd(c, cmd, timeout=30.0):
    """执行单条命令，靠哨兵串判定结束。返回 (out, rc)。rc=-1 表示未等到哨兵。"""
    uniq = 'ZQ%d' % (int(time.time() * 1000) % 1000000)
    full = '{ %s\n} 2>&1; echo "%s=$?"' % (cmd, uniq)
    c.sock.sendall(full.encode('latin1') + b'\n')
    deadline = time.time() + timeout
    acc = b''
    pat = re.compile((uniq + r'=(-?\d+)').encode())
    while time.time() < deadline:
        try:
            chunk = c._recv_some()
        except Exception:
            break
        if chunk:
            acc += chunk
            if pat.search(acc):
                break
        else:
            time.sleep(0.05)
    txt = acc.decode('latin1', 'replace').replace('\r', '')
    m = pat.search(txt.encode('latin1'))
    if m:
        rc = int(m.group(1))
        txt = txt[:txt.rfind(uniq)]
    else:
        rc = -1
    lines = txt.split('\n')
    head = cmd.split('\n')[0].strip()[:20]
    while lines and (lines[0].strip() == '' or (head and head in lines[0])):
        lines.pop(0)
    return '\n'.join(lines).rstrip(), rc


def run(cmds):
    c = connect()
    try:
        for cmd in cmds:
            out, rc = exec_cmd(c, cmd)
            print('$ %s' % cmd)
            if out:
                print(out)
            if rc != 0:
                print('[rc=%d]' % rc)
            print('-' * 50)
    finally:
        try:
            c.sock.close()
        except Exception:
            pass


if __name__ == '__main__':
    run(sys.argv[1:])

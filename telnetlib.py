#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
telnetlib.py - 轻量 telnet 客户端（Python 3.13 无 telnetlib，用原生 socket 实现）
作者：赵良军
用途：连接 Motorola E6 的 telnetd，执行命令、传输文件

核心：telnet 协议 = socket + IAC(0xFF) 协商处理
      传输文件：base64 分块 -> 设备端 base64 -d 还原（不依赖 tftp/ftp）
"""
import socket
import base64
import time
import re

# telnet 命令字节
IAC  = 0xFF   # Interpret As Command
DONT = 0xFE
DO   = 0xFD
WONT = 0xFC
WILL = 0xFB
SB   = 0xFA   # 子协商开始
SE   = 0xF0   # 子协商结束


class TelnetClient:
    def __init__(self, host, port=23, timeout=10, verbose=False):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.verbose = verbose
        self.sock = None
        self.buf = b""

    def _log(self, *a):
        if self.verbose:
            print("[telnet]", *a)

    def connect(self, login_user=None, login_pass=None, login_prompt=b"login:",
                pass_prompt=b"assword:", shell_prompt=None):
        """连接并处理 login（若需要）。shell_prompt 用于判断命令执行完毕。"""
        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.sock.settimeout(self.timeout)
        # 读取 banner / login 提示，回应 IAC 协商（全部 WONT/DONT 拒绝）
        banner = self._read_until_any([login_prompt, b"#", b"$", b">", b"\n"], timeout=5)
        self._log("banner:", banner[:200])
        if login_user and login_prompt in banner:
            self._send_line(login_user)
            if login_pass:
                p = self._read_until_any([pass_prompt, b"#", b"$", b">"], timeout=5)
                self._log("pass prompt:", p[:100])
                self._send_line(login_pass)
        # 若给了 shell_prompt，等它出现
        if shell_prompt:
            self._read_until_any([shell_prompt], timeout=5)
        return True

    def _recv_some(self):
        try:
            data = self.sock.recv(4096)
        except socket.timeout:
            return b""
        if not data:
            return b""
        return self._strip_iac(data)

    def _strip_iac(self, data):
        """剥离并回应 telnet IAC 协商序列，返回净荷。"""
        out = bytearray()
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b != IAC:
                out.append(b)
                i += 1
                continue
            # IAC 序列
            if i + 1 >= n:
                break
            cmd = data[i + 1]
            if cmd == IAC:
                # 转义 0xFF 0xFF -> 0xFF
                out.append(IAC)
                i += 2
            elif cmd in (DO, DONT, WILL, WONT):
                if i + 2 >= n:
                    break
                opt = data[i + 2]
                # 统一拒绝：对方 DO -> 我 WONT；对方 WILL -> 我 DONT
                if cmd == DO:
                    self.sock.sendall(bytes([IAC, WONT, opt]))
                elif cmd == WILL:
                    self.sock.sendall(bytes([IAC, DONT, opt]))
                i += 3
            elif cmd == SB:
                # 跳过子协商直到 IAC SE
                j = data.find(bytes([IAC, SE]), i + 2)
                i = (j + 2) if j != -1 else n
            else:
                # 其他单字节命令（NOP/GOAHEAD 等）忽略
                i += 2
        return bytes(out)

    def _send_line(self, line):
        self.sock.sendall(line.encode() + b"\n")

    def _read_until_any(self, markers, timeout=10):
        """读到任一 marker 出现或超时，返回累计数据。"""
        deadline = time.time() + timeout
        acc = self.buf
        self.buf = b""
        while time.time() < deadline:
            for m in markers:
                if m in acc:
                    # 保留 marker 之后的数据到 buf
                    idx = acc.find(m)
                    self.buf = acc[idx + len(m):]
                    return acc[:idx + len(m)]
            chunk = self._recv_some()
            if chunk:
                acc += chunk
            else:
                time.sleep(0.05)
        self.buf = acc
        return acc

    def exec_cmd(self, cmd, wait=1.0, marker=None):
        """执行命令，返回输出（剥除回显与提示符）。marker 默认探测 '# '/'$ '。"""
        # 发送前清残留
        self.buf = b""
        self._send_line(cmd)
        time.sleep(wait)
        # 收集直到出现提示符或超时
        out = b""
        deadline = time.time() + max(wait, 3.0)
        markers = [marker] if marker else [b"# ", b"$ ", b"> ", b"\n# ", b"\n$ "]
        while time.time() < deadline:
            chunk = self._recv_some()
            if chunk:
                out += chunk
                for m in markers:
                    if m in out[-len(chunk) - len(m):]:
                        return self._clean(out, cmd)
            else:
                time.sleep(0.05)
        return self._clean(out, cmd)

    def _clean(self, raw, cmd):
        text = raw.decode("utf-8", "replace")
        # 去掉命令回显行
        lines = text.split("\n")
        if lines and cmd and lines[0].strip().endswith(cmd.strip()):
            lines = lines[1:]
        # 去掉末尾提示符行
        while lines and re.search(r"[#$>]\s*$", lines[-1].strip() or "#"):
            if lines[-1].strip() == "":
                lines.pop()
            elif re.search(r"[#$>]\s*$", lines[-1].strip()):
                lines[-1] = re.sub(r"[#$>]\s*$", "", lines[-1])
                if lines[-1].strip() == "":
                    lines.pop()
                else:
                    break
            else:
                break
        return "\n".join(lines).replace("\r", "")

    def upload_b64(self, local_path, remote_path, chunk_lines=400, progress=None):
        """用 base64 分块上传文件到设备，设备端 base64 -d 还原。
        要求设备端有 base64（busybox/coreutils 一般都有）。"""
        data = open(local_path, "rb").read()
        b64 = base64.b64encode(data).decode()
        # 先写空目标
        self.exec_cmd(": > %s.b64" % remote_path, wait=0.3)
        total = len(b64)
        sent = 0
        # 分块：每块 chunk_lines 行，每行 76 字符
        line_w = 76
        lines = [b64[i:i + line_w] for i in range(0, len(b64), line_w)]
        i = 0
        while i < len(lines):
            block = "\n".join(lines[i:i + chunk_lines])
            # 用 printf 逐块追加（避免 heredoc 在 telnet 上易出错）
            self.sock.sendall(
                ("cat >> %s.b64 << 'EOFB64'\n" % remote_path).encode()
                + block.encode() + b"\nEOFB64\n"
            )
            time.sleep(0.3)
            # 排空回显
            deadline = time.time() + 2.0
            while time.time() < deadline:
                c = self._recv_some()
                if not c:
                    break
                out_acc = c  # noqa
            i += chunk_lines
            sent = min(i * line_w, total)
            if progress:
                progress(sent, total)
        # 解码还原
        r = self.exec_cmd("base64 -d %s.b64 > %s && rm -f %s.b64 && ls -l %s"
                          % (remote_path, remote_path, remote_path, remote_path), wait=1.5)
        return r

    def close(self):
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("用法: python telnetlib.py <host> [命令]")
        sys.exit(1)
    host = sys.argv[1]
    cmd = sys.argv[2] if len(sys.argv) > 2 else "uname -a"
    c = TelnetClient(host, verbose=True)
    c.connect()
    print(c.exec_cmd(cmd))
    c.close()

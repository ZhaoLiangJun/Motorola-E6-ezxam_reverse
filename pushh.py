# -*- coding: utf-8 -*-
"""pushh.py - 通过 HTTP 把任意本地文件推到设备（比 heredoc 快得多）
用法: python pushh.py <本地文件> <设备路径> [chmod]
"""
import sys, os, time, hashlib, threading, functools, http.server
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
HOST = '192.168.16.1'


def serve(dirpath):
    class Q(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    h = functools.partial(Q, directory=dirpath)
    httpd = http.server.ThreadingHTTPServer((HOST, 8000), h)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else '755'
    path = os.path.abspath(src)
    d = os.path.dirname(path)
    name = os.path.basename(path)
    data = open(path, 'rb').read()
    md5 = hashlib.md5(data).hexdigest()
    serve(d)
    c = connect()
    run(c, 'rm -f ' + dst, wait=1)
    out = run(c, '%s/wget -q -O %s http://%s:8000/%s ; echo WGET=$?' % (T, dst, HOST, name), wait=20)
    print(out.strip())
    run(c, 'chmod %s %s' % (mode, dst), wait=1)
    out = run(c, '%s/md5sum %s' % (T, dst), wait=2)
    print(out.strip())
    ok = md5 in out.lower()
    print('PUSH %s (%d bytes md5=%s)' % ('OK' if ok else 'FAIL', len(data), md5))
    return 0 if ok else 2


if __name__ == '__main__':
    sys.exit(main())

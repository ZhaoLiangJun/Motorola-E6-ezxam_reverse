# -*- coding: utf-8 -*-
"""nc_listen.py - PC 侧 TCP 接收端（配合设备 busybox nc 上传）
用法: python nc_listen.py <port> <outfile> [seconds]
"""
import socket, sys, hashlib

port = int(sys.argv[1]) if len(sys.argv) > 1 else 9911
out = sys.argv[2] if len(sys.argv) > 2 else '_nc_in.bin'
timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 30

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('192.168.16.1', port))
s.listen(1)
s.settimeout(timeout)
print('listening on %d' % port, flush=True)
try:
    c, a = s.accept()
    print('accepted from %s' % (a,), flush=True)
    d = b''
    while True:
        b = c.recv(65536)
        if not b:
            break
        d += b
    c.close()
    open(out, 'wb').write(d)
    print('recv %d bytes md5=%s' % (len(d), hashlib.md5(d).hexdigest()), flush=True)
except Exception as e:
    print('ERR %s' % e, flush=True)
s.close()

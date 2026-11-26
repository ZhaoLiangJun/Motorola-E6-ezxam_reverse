# -*- coding: utf-8 -*-
# 杀旧 tracer，启动 sampler 长时间采样 am
import sys, os, re, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

c = connect()
# 杀掉旧 tracer
print(run(c, 'kill -9 250 2>/dev/null; : > /ram/tmp/sample.txt; : > /ram/tmp/amreg.log', wait=1))
out = run(c, 'ps ax 2>&1', wait=2)
for l in out.splitlines():
    if 'tracer' in l or 'sampler' in l:
        print(l)
# 启动 sampler（10万轮，后台长跑）
c._send_line('/mmc/mmca1/sampler 244 100000 >/ram/tmp/sample.txt 2>&1 &')
time.sleep(2)
out = run(c, 'ps ax 2>&1', wait=2)
print([l for l in out.splitlines() if 'sampler' in l])
print(run(c, 'wc -c /ram/tmp/sample.txt 2>/dev/null; ls -la /ram/tmp/sample.txt', wait=2))

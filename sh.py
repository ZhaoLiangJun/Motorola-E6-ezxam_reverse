# -*- coding: utf-8 -*-
"""sh.py - 在设备上批量执行命令
用法: python sh.py "cmd1" "cmd2" ...
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run


def main():
    c = connect()
    for cmd in sys.argv[1:]:
        out = run(c, cmd, wait=2.5)
        print('$ %s' % cmd)
        print(out.rstrip())
        print('-' * 50)


if __name__ == '__main__':
    main()

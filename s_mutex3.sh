#!/bin/sh
# 反汇编 libezxappbase 的 AM_Mutex 区段（0x41872690 - 0x418727a0）
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
L=/usr/lib/ezx/lib/libezxappbase.so.1.0.0
objdump -d $L 2>/dev/null | sed -n '/^41872690/,/^418727a0/p'

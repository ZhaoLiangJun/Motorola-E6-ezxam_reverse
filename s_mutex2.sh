#!/bin/sh
# 反汇编 libezxappbase 中的 AM_Mutex 相关函数
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
L=/usr/lib/ezx/lib/libezxappbase.so.1.0.0
echo "=== AM_Mutex 符号"
readelf -sW $L 2>/dev/null | grep "AM_Mutex"
echo "=== setBusy 0x418726e0"
objdump -d --start-address=0x418726e0 --stop-address=0x41872750 $L 2>/dev/null | tail -30
echo "=== setNotBusy 0x41872744"
objdump -d --start-address=0x41872744 --stop-address=0x418727b0 $L 2>/dev/null | tail -25

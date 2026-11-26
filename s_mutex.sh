#!/bin/sh
# 找出 AM_Mutex / AM_Global 系统忙相关符号的定义库
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
for f in /usr/lib/ezx/lib/libezxam.so.1.0.0 /usr/lib/ezx/lib/libezxamr.so.1.0.0 \
         /usr/lib/ezx/lib/libezxamstatusbar.so.1.0.0 /usr/lib/ezx/lib/libezxqt.so.1.0.0 \
         /usr/SYSqtapp/am/am; do
    [ -f "$f" ] || continue
    echo "=== $f"
    readelf -sW "$f" 2>/dev/null | grep -E "AM_Mutex|systemBusyMessageBox"
done

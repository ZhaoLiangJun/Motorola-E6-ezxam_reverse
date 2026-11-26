#!/bin/sh
# 查看原库 AM_AppObject 的几个访问器（getVender/getPath/getSize）真实签名
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
export LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib:/usr/lib:/lib
L=/ram/tmp/orig.so
echo "=== 符号"
readelf -sW $L 2>/dev/null | grep -E "AM_AppObject(9getVender|7getPath|7getSize|10getBigIcon|12getSmallIcon)" 
echo "=== getVender 反汇编"
A=$(readelf -sW $L 2>/dev/null | awk '/AM_AppObject9getVenderEv/{print $2; exit}')
echo "addr=$A"
objdump -d --start-address=0x$A --stop-address=0x$(printf '%x' $((0x$A + 64))) $L 2>/dev/null | tail -20

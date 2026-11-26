#!/bin/sh
# 列出某固件库从 libezxam 导入的符号（即我们库必须 1:1 提供的接口）
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
export LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib:/usr/lib:/lib
LIB=${1:-/usr/lib/ezx/lib/libezxaid.so.1.0.0}
echo "=== $LIB 需要 libezxam 提供的符号"
readelf -sW "$LIB" 2>/dev/null | awk '$7=="UND" && $8 ~ /^_ZN/ {print $8}' | sort -u
echo "=== 结束"

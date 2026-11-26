#!/bin/sh
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
export LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib:/usr/lib:/lib
L=/ram/tmp/orig.so
echo "=== getVender 0x41aef960 (24B)"
objdump -d --start-address=0x41aef960 --stop-address=0x41aef978 $L 2>/dev/null | tail -8
echo "=== getBigIcon(QPixmap&) 0x41aee768 (8B)"
objdump -d --start-address=0x41aee768 --stop-address=0x41aee770 $L 2>/dev/null | tail -5
echo "=== getBigIcon() 0x41aef308 前 20 条"
objdump -d --start-address=0x41aef308 --stop-address=0x41aef370 $L 2>/dev/null | tail -20
echo "=== getSmallIcon(QPixmap&) 0x41aee770"
objdump -d --start-address=0x41aee770 --stop-address=0x41aee778 $L 2>/dev/null | tail -5

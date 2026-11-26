#!/bin/sh
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
export LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib:/usr/lib:/lib
L=/ram/tmp/orig.so
echo "=== getVender 附近 0x41aef940-0x41aef990"
objdump -d --start-address=0x41aef940 --stop-address=0x41aef990 $L 2>/dev/null | grep -E "^41aef" 
echo "=== getBigIcon() 入口 0x41aef308-0x41aef330"
objdump -d --start-address=0x41aef308 --stop-address=0x41aef330 $L 2>/dev/null | grep -E "^41aef"
echo "=== getDefaultName / getAnimationIcon 符号"
readelf -sW $L 2>/dev/null | grep -E "getDefaultName|getAnimationIcon|getIconE" 

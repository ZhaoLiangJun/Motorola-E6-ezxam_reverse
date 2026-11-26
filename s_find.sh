#!/bin/sh
# 在全部 EZX 库中查找符号定义（排除 UND）
export PATH=/mmc/mmca1/.system/arm-linux-gcc/bin:$PATH
SYM="$1"
[ -z "$SYM" ] && SYM=AM_Mutex7setBusy
echo "### 查找 $SYM"
for f in /usr/lib/ezx/lib/*.so* /usr/lib/libezx*.so* /usr/SYSqtapp/am/am; do
    [ -f "$f" ] || continue
    out=$(readelf -sW "$f" 2>/dev/null | grep "$SYM" | grep -v " UND ")
    if [ -n "$out" ]; then
        echo "--- $f"
        echo "$out"
    fi
done
echo "### 我方库符号总数"
readelf -sW /usr/lib/ezx/lib/libezxam.so.1.0.0 2>/dev/null | grep -c "FUNC"
echo "### systemBusyMessageBox 现状"
readelf -sW /usr/lib/ezx/lib/libezxam.so.1.0.0 2>/dev/null | grep "systemBusy"
echo "(end)"

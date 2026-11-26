#!/bin/sh
# A/B 对照：把 libezxam 换成本地预置的库并重启 am
#   sh s_ab.sh orig   -> 原版固件库 /ram/tmp/orig.so
#   sh s_ab.sh mine   -> 我们的 v33
L=/usr/lib/ezx/lib/libezxam.so.1.0.0
case "$1" in
  orig) SRC=/ram/tmp/orig.so ;;
  mine) SRC=/ram/tmp/v33.so ;;
  *) echo "usage: s_ab.sh orig|mine"; exit 1 ;;
esac
[ -f "$SRC" ] || { echo "MISSING $SRC"; exit 1; }
mount --bind "$SRC" "$L" && echo "MOUNTED $SRC"
ls -l "$L"
kill -9 $(pidof am) 2>/dev/null
sleep 8
echo "am pids: $(pidof am)"

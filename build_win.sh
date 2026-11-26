#!/bin/bash
# build_win.sh - 在 Windows/Cygwin 下用原版 arm-linux-gcc (GCC 3.3.6) 编译 libezxam 逆向重建源码
# 关键：必须让 我们重写的 include/ezx 优先于 原始 genheader 头文件，
#       并通过 qt_preinclude.h 注入 Qt 基础类型 include。
export PATH="/d/cygwin/bin:$PATH"
export MSYS_NO_PATHCONV=1

BIN="D:/cygwin/home/ezx/arm-linux-gcc/bin"
QTI="D:/cygwin/home/ezx/qt-2.3.8/include"
SRC="D:/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse"
BUILD="$SRC/build"
mkdir -p "$BUILD"

CXX="$BIN/arm-linux-g++.exe"
MOC="$BIN/moc.exe"

CFLAGS="-pipe -DQWS -fno-exceptions -fno-rtti -O2 -D_REENTRANT -fPIC"
# 重要顺序：-I$SRC/include/ezx (重写) 在前，-I$QTI/ezx (原始genheader) 在后
INCS="-I\"$SRC/include/ezx\" -I\"$SRC/include\" -I\"$QTI/qt\" -I\"$QTI/ezx\""
PRE="-include \"$SRC/include/qt_preinclude.h\""

echo "=== 1/2 生成 moc (Q_OBJECT 类) ==="
for h in AM_AppDataInterface AM_AppLockManager AM_Carrier_Info AM_GroupListScrollView AM_RegistryManager AM_StandardDlg; do
    hdr="$SRC/include/ezx/$h.h"
    if [ -f "$hdr" ] && grep -q "Q_OBJECT" "$hdr" 2>/dev/null; then
        "$MOC" "$hdr" -o "$SRC/src/$h.moc" 2>/dev/null && echo "  MOC $h OK" || echo "  MOC $h FAIL"
    fi
done

echo "=== 2/2 编译所有 .cpp ==="
PASS=0; FAIL=0
for cpp in "$SRC"/src/*.cpp; do
    name=$(basename "$cpp" .cpp)
    out="$BUILD/$name.o"
    # shellcheck disable=SC2086
    eval "$CXX $CFLAGS $PRE $INCS -c \"$cpp\" -o \"$out\" 2>\"/tmp/build_$name.log\""
    rc=$?
    if [ $rc -eq 0 ]; then
        PASS=$((PASS+1)); echo "  OK   $name"
    else
        FAIL=$((FAIL+1)); echo "  FAIL $name"
    fi
done
echo ""
echo "=== 通过 $PASS / 失败 $FAIL ==="

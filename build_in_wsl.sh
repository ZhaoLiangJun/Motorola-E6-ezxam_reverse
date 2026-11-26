#!/bin/bash
# libezxam 逆向重建 - WSL 原版 MontaVista 工具链编译脚本
# 用法: 在 WSL Debian 中执行
#   cd /mnt/d/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse
#   bash build_in_wsl.sh

set -e

TOOLCHAIN=/opt/iwmmxt_le/bin
CXX=$TOOLCHAIN/arm-linux-g++
CC=$TOOLCHAIN/arm-linux-gcc
AR=$TOOLCHAIN/arm-linux-ar
CXXFILT=$TOOLCHAIN/arm-linux-c++filt

# Qt 2.3.8 + EZx 头文件 (Windows 侧挂载)
QTDIR=/mnt/d/cygwin/home/ezx/qt-2.3.8
QTINC=$QTDIR/include/qt
EZXINC=$QTDIR/include/ezx

# 本项目
SRC=src
INC=include/ezx
BUILD=build
MOCDIR=moc
LIBOUT=libezxam_reconstructed.so.1.0.0

mkdir -p $BUILD $MOCDIR

CXXFLAGS="-pipe -DQWS -fno-exceptions -fno-rtti -O2 -D_REENTRANT \
  -I$QTINC -I$EZXINC -I$INC -I$MOC -Wall"

echo "=== 编译 libezxam 逆向重建版本 ==="
echo "工具链: $CXX"
$CXX --version | head -1

# 对带 Q_OBJECT 的类跑 moc（生成信号槽实现）
MOCBIN=$TOOLCHAIN/moc
[ -x $MOCBIN ] || MOCBIN=$(dirname $CXX)/moc
QOBJECT_HEADERS="AM_AppDataInterface AM_AppLockManager AM_Carrier_Info \
AM_StandardDlg AM_GroupListScrollView AM_RegistryManager"
for h in $QOBJECT_HEADERS; do
    if grep -q "Q_OBJECT" $INC/$h.h 2>/dev/null; then
        echo "  MOC $h.h"
        $MOCBIN $INC/$h.h -o $MOCDIR/moc_$h.cpp 2>/dev/null || \
        $MOCBIN -i $INC/$h.h -o $MOCDIR/moc_$h.cpp
    fi
done

# 编译每个 .cpp（含 moc 产物）
OBJS=""
for cpp in $SRC/*.cpp; do
    obj="$BUILD/$(basename $cpp .cpp).o"
    echo "  CXX $cpp"
    $CXX $CXXFLAGS -c $cpp -o $obj
    OBJS="$OBJS $obj"
done
for cpp in $MOCDIR/moc_*.cpp; do
    [ -e "$cpp" ] || continue
    obj="$BUILD/$(basename $cpp .cpp).o"
    echo "  CXX $cpp"
    $CXX $CXXFLAGS -c $cpp -o $obj
    OBJS="$OBJS $obj"
done

echo "=== 链接 $LIBOUT ==="
$CXX -shared -Wl,-soname,libezxam.so.1 $OBJS \
    -L$QTDIR/lib -lqte-mt \
    -o $LIBOUT

echo "=== 完成: $LIBOUT ==="
echo "用 arm-linux-readelf 验证:"
$TOOLCHAIN/arm-linux-readelf -h $LIBOUT | grep -E "Class|Machine|Type"

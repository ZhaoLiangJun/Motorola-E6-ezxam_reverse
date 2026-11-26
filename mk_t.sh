#!/bin/bash
# mk_t.sh - 把 t_*.cpp 探针程序交叉编译成 ARM 可执行文件（设备侧用）
#
# 用法: bash mk_t.sh t_reg
# 产物: _tb/<name>
#
# 关键点（踩过的坑都写在这里）：
#   1. cygwin 的 arm-linux-g++ 看不到 MSYS 的 /tmp，也不认 POSIX 路径，
#      所以源码/输出都用 D:/... 形式；调用前清掉 MSYS 的路径转换。
#   2. 探针按修饰名调用，链接期只需要"提供符号的库"存在；
#      用本库做链接期参考，NEEDED 写的是 SONAME=libezxam.so.1。
#      【不要设 rpath】——老 glibc 里 DT_RPATH 优先于 LD_LIBRARY_PATH，
#      一旦设了 rpath=/usr/lib/ezx/lib，就永远只能跑到挂载的原厂库，
#      A/B 变成自己跟自己比。运行期用
#        LD_LIBRARY_PATH=/ram/amlib/ours:/usr/lib/ezx/lib
#      显式选库（后者只是为了解析 libqte-mt.so.2）。
#   3. 本库带一批平台未定义符号（CM_SEC_/DRM_SP_/UTIL_ 等），
#      链接可执行文件时必须 --allow-shlib-undefined，否则报一堆 undefined。
set -e
export PATH="/d/cygwin/bin:$PATH"
export MSYS_NO_PATHCONV=1

NAME="${1:?用法: bash mk_t.sh <name>  (不带 .cpp)}"
BIN="D:/cygwin/home/ezx/arm-linux-gcc/bin"
QTLIB="D:/cygwin/home/ezx/qt-2.3.8/lib"
SRC="D:/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse"
OUT="$SRC/_tb"

mkdir -p "$OUT"
"$BIN/arm-linux-g++.exe" -O2 -DQWS -fno-exceptions -fno-rtti \
    -Wl,--allow-shlib-undefined \
    -o "$OUT/$NAME" "$SRC/$NAME.cpp" \
    "$SRC/libezxam_reconstructed.so.1.0.0" \
    -L"$QTLIB" -lqte-mt \
    2>&1 | grep -vE "find_fast_cwd|public mailing list|cygwin warning|MS-DOS style path|Preferred POSIX|CYGWIN environment|Consult the user|using.html|^\s*$" || true

ls -l "$OUT/$NAME"

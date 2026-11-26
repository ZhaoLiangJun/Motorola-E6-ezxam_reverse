#!/bin/bash
# link_win.sh - Windows/Cygwin 下用原版 arm-linux-g++ (GCC 3.3.6) 链接
#               libezxam 逆向重建库。
#
# 背景：build_win.sh 只做编译（产出 build/*.o），链接此前是手工敲命令、
#       未固化，属可复现性缺口。本脚本固化链接步骤，与 build_win.sh 配套
#       构成一键流程：  bash build_win.sh && bash link_win.sh
#
# 关键约束：
#   1. 平台符号（CM_SEC_/DRM_SP_/UTIL_ 等）必须保持未定义(U)，由设备端
#      libezxappbase/libdmnative 等在加载期解析。stubs/ 下的桩库只定义
#      ELF 内部符号(_init/_fini/_DYNAMIC)，不定义任何平台函数符号，
#      以桩文件路径直接传入链接器（-l 找不到带 .so.NN 后缀的库）。
#   2. 真实 libqte-mt.so.2 提供 Qt 符号；它不 re-export 平台符号，
#      不会污染 CM_SEC_/DRM_SP_ 的未定义状态（deploy_check.py 权威确认）。
#   3. SONAME 必须是 libezxam.so.1（设备端依赖此名）。

export PATH="/d/cygwin/bin:$PATH"
export MSYS_NO_PATHCONV=1

BIN="D:/cygwin/home/ezx/arm-linux-gcc/bin"
CXX="$BIN/arm-linux-g++.exe"
QTLIB="D:/cygwin/home/ezx/qt-2.3.8/lib"
SRC="D:/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse"
BUILD="$SRC/build"
STUBS="$SRC/stubs"
OUT="$SRC/libezxam_reconstructed.so.1.0.0"

# 汇总所有 .o（含模板显式实例化单元 qt_template_instantiations.o）
OBJS=$(ls "$BUILD"/*.o 2>/dev/null | tr '\n' ' ')
if [ -z "$OBJS" ]; then
    echo "错误: $BUILD 下没有 .o，请先运行 build_win.sh"
    exit 1
fi
echo "=== 链接 libezxam_reconstructed.so.1.0.0 ==="
echo "  输入 .o: $(echo "$OBJS" | wc -w) 个"

# 链接：桩库以路径直接传入（非 -l），保证平台符号保持 U
# 固定基址：原库是 prelink 固定基址库（第一个 LOAD vaddr=0x41b28000，
# 数据段在 0x41b73000）。用 fixed.lds 复现该布局，使 vtable 槽的
# R_ARM_ABS32 目标为绝对地址（本库符号），动态链接器加载时按 S+A 正确重定位。
LDS="$SRC/fixed_base.lds"
if [ ! -f "$LDS" ]; then
    echo "错误: 缺少 $LDS（固定基址链接脚本）"
    exit 1
fi

# 导出白名单：必须与 Makefile 的 EXPORTS 保持一致。
# 不加的话本库会把源码里出现的所有 Qt2 模板实例（QString::~QString、
# QMap<...>::operator= 等）和调试符号（amLogWrite/ring_*/crash_install …）
# 一起导出，实测多出 109 个；这些多余"提供者"在别的进程里会抢占同名 UND，
# 是隐蔽的崩溃源。匿名版本节点：只改可见性，不引入 @V1 后缀。
EXPORTS="$SRC/exports.map"
if [ ! -f "$EXPORTS" ]; then
    echo "错误: 缺少 $EXPORTS（导出白名单，可用 gen_exports.py 生成）"
    exit 1
fi

"$CXX" -shared -Wl,-soname,libezxam.so.1 \
    -Wl,-T,"$LDS" \
    -Wl,--version-script,"$EXPORTS" \
    $OBJS \
    -L"$QTLIB" -lqte-mt \
    "$STUBS/libezxappbase.so.1" \
    "$STUBS/libdmnative.so.02" \
    "$STUBS/libezxphone.so.1" \
    "$STUBS/libezx-cmseccert.so" \
    "$STUBS/libezxdrmsp.so.1" \
    "$STUBS/libpthread.so.0" \
    -o "$OUT" 2> "$BUILD/link.log"
rc=$?

grep -v "find_fast_cwd\|nodosfilewarning\|CYGWIN\|Preferred\|Consult\|public mailing\|^$\|MS-DOS\|cygwin warning\|using.html" "$BUILD/link.log" | head -20

if [ $rc -ne 0 ]; then
    echo "链接失败 (exit $rc)，详见 $BUILD/link.log"
    exit $rc
fi

echo "  输出: $OUT ($(stat -c%s "$OUT") bytes)"
echo "=== 完成 ==="

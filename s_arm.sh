#!/bin/sh
# 在 AM_HomeScreen 的"调整顺序"与"卸载软件"链路上挂断点
#   0x3a7d8 slotChangeOrder 入口
#   0x3a7e8 setBusy 返回后（r0=结果）
#   0x3a814 uuid 为空 -> setNotBusy + 返回
#   0x3a884 排序对话框构造
#   0x3ab88 slotUninstallApps 入口
#   0x3ab94 守卫返回后（r0=结果）
#   0x3abf4 AM_UninstallDlg 构造
P=$(pidof am)
set -- $P
echo "am pids: $P  使用 pid=$1"
/ram/tmp/bp $1 900 0x3a7d8 0x3a7e8 0x3a814 0x3a884 0x3ab88 0x3ab94 0x3abf4 > /ram/tmp/bp.log 2>&1 &
echo ARMED

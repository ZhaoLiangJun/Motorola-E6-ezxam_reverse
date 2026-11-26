/* amlog.h - 统一日志开关（v38）
 *
 * 背景（性能问题，用户反馈"很卡，没有原版流畅"）：
 *   v37 之前每个 .cpp 各写一个 alog/llog/flog/glog，每打一行都是
 *   fopen + vfprintf + fclose 三次系统调用。而 getAppObject / Folder::getAppList /
 *   getExecFullPath / getSize / getName 是**每刷一次界面就要跑几十上百次**的热路径，
 *   在 PXA27x（200~300MHz，jffs2/ramfs）上累计起来就是肉眼可见的卡顿。
 *
 * 方案：区分两类日志
 *   AMLOG(...)   低频、有诊断价值（启动装载、卸载、QCop、崩溃相关）→ 总是写
 *   AMVLOG(...)  热路径逐次打印 → **默认不写**，只在 /ram/tmp/am_debug 存在时写
 *
 * 用法：
 *   默认（正常使用，不卡）      : 什么都不用做
 *   排查问题（要全量日志）      : touch /ram/tmp/am_debug 然后重启 am
 *   排查完恢复                  : rm /ram/tmp/am_debug
 *   运行时立刻生效              : touch /ram/tmp/am_debug.reload 后下一次调用会重读
 *                                （其实重启 am 最简单，这里只做了一次性缓存）
 */
#ifndef AMLOG_H
#define AMLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 是否输出热路径日志（结果缓存，首次调用时检测 /ram/tmp/am_debug） */
int  amLogVerbose(void);
/* 写一行到 /ram/tmp/amreg.log（自带相对本进程的毫秒时间戳） */
void amLogWrite(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#define AMLOG(...)   amLogWrite(__VA_ARGS__)
#define AMVLOG(...)  do { if (amLogVerbose()) amLogWrite(__VA_ARGS__); } while (0)

/* ============================================================
   v77 编译期总开关（release 化）：
   默认 EZXAM_DEBUG=0 —— AMLOG/AMVLOG 全部编译成空操作，日志格式串
   不进产物、零运行时开销。仍然保留的非调试设施：
     · ringlog.cpp 的 BOOT 分隔行（识别运行版本，进程启动一次）
     · crash_install() 崩溃现场抓取（SIGSEGV 等写 amreg.log）
     · am_fix_install_mutex() 安装互斥修复（v50 功能修复，非调试）
   需要日志排查时：make EZXAM_DEBUG=1 重编（AMLOG 恢复为必写）。
   直接调用 amLogVerbose()/amLogWrite() 的少量点自带运行时开关，
   两种模式下行为一致（release 只多一次缓存的 access() 检测）。
   ============================================================ */
#ifndef EZXAM_DEBUG
#define EZXAM_DEBUG 0
#endif
#if EZXAM_DEBUG
/* 上面两个宏保持原样（-DEZXAM_DEBUG=1 构建时走这里） */
#else
#undef  AMLOG
#undef  AMVLOG
#define AMLOG(...)   do { } while (0)
#define AMVLOG(...)  do { } while (0)
#endif

#endif /* AMLOG_H */

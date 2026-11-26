/* amlog.cpp - 统一日志实现（v38）
 * 见 amlog.h 的说明。核心两点：
 *   1) 热路径日志默认关闭（/ram/tmp/am_debug 存在才写），避免刷屏卡顿；
 *   2) 保留相对本进程的毫秒时间戳，卡死时能一眼看出停在哪一步。
 */
#include "amlog.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>

#define AMLOG_PATH  "/ram/tmp/amreg.log"
#define AMLOG_FLAG  "/ram/tmp/am_debug"

static unsigned am_ms(void)
{
    static long t0s = -1;
    static long t0u = 0;
    struct timeval tv;
    gettimeofday(&tv, 0);
    if (t0s < 0) { t0s = tv.tv_sec; t0u = tv.tv_usec; }
    return (unsigned)((tv.tv_sec - t0s) * 1000 + (tv.tv_usec - t0u) / 1000);
}

extern "C" int amLogVerbose(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (access(AMLOG_FLAG, F_OK) == 0) ? 1 : 0;
    return cached;
}

extern "C" void amLogWrite(const char* fmt, ...)
{
    FILE* fp = fopen(AMLOG_PATH, "a");
    if (!fp)
        return;
    /* ★v53：前缀加 pid。以前只有"本进程毫秒数"，多进程（am / 安装器 /
       我的收藏 / SoftManage）同时写同一个文件时无法分辨某一行属于谁，
       排查"安装秒退"时只能靠猜。pid + 毫秒 才能把一次操作的多进程时序拼起来。 */
    fprintf(fp, "%d %7u| ", (int)getpid(), am_ms());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

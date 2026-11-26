/* ringlog.h - 内存环形调用日志（零 I/O，SIGUSR1 转储到 /tmp/ring_dump.txt）
 * 用途：还原 am 对我们库的完整调用序列，定位启动卡死点。 */
#ifndef RINGLOG_H
#define RINGLOG_H

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define RING_SIZE 1024
/* ★v61：默认**关闭**调用埋点。
   理由（性能）：ring_put 里 `g_ring_pos % RING_SIZE` 的下标取模，ARM9 无硬件除法，
   编译成 __umodsi3 软除法调用；而 ring_put 全库有 54 个调用点，getIcon/getSize/
   getPath 这些每帧每个图标都会走 —— 这是本库比原库慢的主要原因之一。
   需要还原调用序列时把这里改成 1 重编即可，功能代码一行不动。 */
#ifndef AM_RING_ENABLE
#define AM_RING_ENABLE 0
#endif

/* 标签表：
   AppObject: 1=getName 2=getIcon 3=getBigIcon 4=getSmallIcon 5=getAniIcon
              6=getSize 7=getPath 8=getVender 9=invoke 10=createObject
              11=getAppLockEnabled 12=getAppLockValue 13=dump 14=setName
   AppLnk:    15=getExecFullPath 16=AppLnk::createObject 17=AppLnk::invoke
   Registry:  20=getAppObjById 21=getAppObject 22=RM::createObject
              23=loadMenuTree 24=loadRegistry 25=initRegistryFiles
              26=changeFolder 27=handleCardPlugIn 28=saveFile 29=amParseAll
              30=slotAppInstalled 31=getInstalledAppList 32=getAllAppList
              33=getRootFolder 34=removeApp 35=slotAddAppToFolder
              36=changeFolderPersist 37=getConfigFileName
   Folder:    40=Folder::add 41=Folder::remove
   Link:      45=EmbeddedLink::createObject 46=EmbeddedLink::invoke
              47=Shortcut::createObject
   Misc:      50=copyTextFile 51=looksUtf8
   DataInterface（am 直接导入的委托层，v24 新增）:
              60=getAppLnkById 61=getAppLnkBySerialId 62=getJavaObj
              63=getAutoRunApps 64=changeOrder
   SecurityManager（v24 新增，am 启动校验链）:
              70=verifySecurityByLaunch 71=verifySecurity 72=parseSignedFile
              （a=返回码 b=路径首 8 字节） */

struct RingEnt { unsigned tag, a, b; };
extern RingEnt g_ring[RING_SIZE];
extern volatile unsigned g_ring_pos;
extern volatile unsigned g_name_skip;

#if AM_RING_ENABLE

static inline void ring_put(unsigned tag, unsigned a, unsigned b)
{
    RingEnt* r = &g_ring[g_ring_pos % RING_SIZE];
    r->tag = tag; r->a = a; r->b = b;
    g_ring_pos = g_ring_pos + 1;
}

/* getName 高频：每 8 次记 1 条 */
static inline void ring_put_name(unsigned a)
{
    unsigned c = g_name_skip + 1;
    g_name_skip = c;
    if ((c & 7) == 1)
        ring_put(1, a, c);
}

#else

/* 关闭时编译成空函数：不取模、不访存、不碰 volatile，调用点直接消失 */
static inline void ring_put(unsigned, unsigned, unsigned) {}
static inline void ring_put_name(unsigned) {}

#endif

static inline void ring_install()
{
    static int installed = 0;
    if (installed) return;
    installed = 1;
    FILE* f = fopen("/tmp/ring_dump.txt", "w");
    if (f) fclose(f);
    signal(SIGUSR1, SIG_DFL); /* 占位，实际 handler 在 ringlog.cpp 安装 */
}

void ring_install_handler();
void ring_dump_file();

#endif

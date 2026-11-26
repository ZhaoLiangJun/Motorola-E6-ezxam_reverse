/* AM_RegistryManager_cwrap.cpp - C 风格导出符号（v38 决定性修正）
 *
 * 原版 libezxam.so 导出 3 个 extern "C" 函数：
 *   AM_RegistryManager_changeFolder
 *   AM_RegistryManager_removeAppFromMainMenu
 *   AM_RegistryManager_getAppParentFolderUid
 *
 * ⚠ v37 之前这里写错了：以为它们是"显式接收 AM_RegistryManager* self 的转发器"。
 *   实测原库三者都 **自己 new 一个 AM_RegistryManager**，参数全是 C 字符串/C 标量：
 *
 *   getAppParentFolderUid @0x41B0B5F4 (352B)
 *     r0 -> QString::C1(char const*)         ; QString p(path)      ← r0 = const char*
 *     operator new(0x34) + C1(0,0)           ; ★自己 new 一个 52 字节的管理器
 *     bl  _ZN18AM_RegistryManager21getAppParentFolderUidERK7QStringRS0_
 *                                            ; (m, &p, &uid)，uid 是栈上已构造的 QString
 *     _ZneRK7QStringS1_(QString::null, uid)  ; if (uid != QString::null)
 *     _ZNK7QString6latin1Ev + strcpy         ;     strcpy(out, uid.latin1()) ← r1 = char*
 *     delete m（blx vtable[+4]）
 *
 *   removeAppFromMainMenu @0x41B0B504 (240B)
 *     r0 -> QString(EPKc)                    ; const char* app
 *     r5 = r1 & 0xff                         ; char flag
 *     new(0x34) + C1(0,0)
 *     bl  ...removeAppFromMainMenuERK7QStringb(m, &app, flag)
 *     return r0 & 0xff                       ; bool
 *
 *   changeFolder @0x41B0B754 (492B)
 *     r0/r1/r2 各自 QString(EPKc)，r3 -> r6 int
 *     new(0x34) + C1(0,0)
 *     bl  ...changeFolderERK7QStringS2_S2_i
 *     return bool
 *
 * ★ 这就是 v37 崩溃的根因（SIGSEGV addr=0x4）：
 *   调用方按 (const char* path, char* out) 传 r0/r1，r2 是野值；
 *   旧实现把它当 (self, QString& path, QString& uid) 解释，
 *   在函数体第一句 `uid = QString::null` 时把 QString::null 写进野指针
 *   （实测 r2=0x000f8020，其 d=0x00000004 → QString::deref() 读 *(0x4) 崩溃）。
 *   崩溃栈：libqte QString::operator= ← libezxam+0x2c ← 返回地址 0x41AFD488。
 *   后果是 am 被 SIGSEGV 打死、windowsserver 重新拉起 → 用户感到"卡/闪一下"。
 *
 * 注意：这三个函数每调一次都会完整 loadMenuTree 一遍（原库同样如此），
 *       调用方不应高频使用。
 */
#include <AM_RegistryManager.h>
#include <qstring.h>
#include <string.h>
#include "amlog.h"   /* v38: 记录这三个 C 入口的调用次数/耗时 */

extern "C" {

/* bool AM_RegistryManager_changeFolder(const char* name, const char* oldFolder,
 *                                      const char* newFolder, int index) */
bool AM_RegistryManager_changeFolder(const char* name,
                                     const char* oldFolder,
                                     const char* newFolder,
                                     int index)
{
    AMLOG("[cwrap] changeFolder('%s','%s','%s',%d)",
          name ? name : "", oldFolder ? oldFolder : "",
          newFolder ? newFolder : "", index);
    QString n(name ? name : "");
    QString o(oldFolder ? oldFolder : "");
    QString w(newFolder ? newFolder : "");
    AM_RegistryManager* m = new AM_RegistryManager(0, 0);
    if (!m)
        return false;
    bool r = m->changeFolder(n, o, w, index);
    delete m;
    return r;
}

/* bool AM_RegistryManager_removeAppFromMainMenu(const char* app, char flag) */
bool AM_RegistryManager_removeAppFromMainMenu(const char* app, char flag)
{
    AMLOG("[cwrap] removeAppFromMainMenu('%s',%d)", app ? app : "", (int)flag);
    QString a(app ? app : "");
    AM_RegistryManager* m = new AM_RegistryManager(0, 0);
    if (!m)
        return false;
    bool r = m->removeAppFromMainMenu(a, flag != 0);
    delete m;
    return r;
}

/* void AM_RegistryManager_getAppParentFolderUid(const char* path, char* out)
 * 找不到时**不写 out**（原库用 `uid != QString::null` 判定）。 */
void AM_RegistryManager_getAppParentFolderUid(const char* path, char* out)
{
    AMLOG("[cwrap] getAppParentFolderUid('%s')", path ? path : "");
    QString p(path ? path : "");
    AM_RegistryManager* m = new AM_RegistryManager(0, 0);
    if (!m)
        return;
    QString uid;
    m->getAppParentFolderUid(p, uid);
    if (uid != QString::null && out)
        strcpy(out, uid.latin1());
    delete m;
}

} /* extern "C" */

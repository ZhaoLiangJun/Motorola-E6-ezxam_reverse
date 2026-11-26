/* AM_EmbeddedLink.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   虚表证据：AM_EmbeddedLink : AM_AppObject，slot[4] 覆盖 dump（与 Shortcut::dump 共享 0x41af4c24）
   invoke：system(命令行) + AM_LauncherInterface::autoOpenWithAnotherApp(QUuid, QString, bool)
*/
#include <AM_EmbeddedLink.h>
#include <AM_LauncherInterface.h>
#include <DmtExternal.h>
#include <qmap.h>
#include <stdlib.h>   /* system() */

/* ============ 构造函数 ============ */
AM_EmbeddedLink::AM_EmbeddedLink()
    : AM_AppObject(EmbeddedLink)
{
}

/* ============ 析构函数 ============ */
AM_EmbeddedLink::~AM_EmbeddedLink()
{
}

/* ============ invoke ============ */
/* 反编译 0x41af4564 完整调用序列：
   1) system(m_cmdLine.latin1());     ← 用系统 shell 执行内嵌命令行
   2) 构造固定 QUuid（字节序列 7e2f 9e6d f8f6 3b2f 3c40 …，内嵌浏览器目标）
   3) AM_LauncherInterface::autoOpenWithAnotherApp(uuid, param1[0x54], true)
      （param1[0x54] = launcher 的 QString 字段）
   QUuid 固定字节：7e 2f 9e 6d f8 f6 3b 2f 3c 40 ...，此处用 QByteArray 构造。 */
void AM_EmbeddedLink::invoke(AM_LauncherInterface const& launcher)
{
    /* 1) system() 执行内嵌命令行（m_cmdLine 是 EmbeddedLink 专属字段） */
    if (!m_cmdLine.isEmpty())
        system(m_cmdLine.latin1());

    /* 2) 构造目标 QUuid（真实 rodata 常量 0x41b18d80）
       "2a58c0d6-05c6-45aa-8938-63059d41a4e9" → MFlashPlayer 内嵌播放器
       QUuid(ulong l, ushort w1, ushort w2, uchar b1..b8) */
    QUuid targetUuid(
        0x2a58c0d6UL,   /* data1 */
        0x05c6,         /* data2 */
        0x45aa,         /* data3 */
        0x89, 0x38, 0x63, 0x05, 0x9d, 0x41, 0xa4, 0xe9  /* b1..b8 */
    );

    /* 3) autoOpenWithAnotherApp(uuid, launcher[0x54], true)
       launcher[0x54] 是 AM_LauncherInterface 内的 QString 成员，
       通过 static 方法接口调用，第二参数用空串占位（无暴露 accessor）。 */
    AM_LauncherInterface::autoOpenWithAnotherApp(targetUuid, QString::null, true);
}

/* ============ createObject ============ */
/* 反编译 0x41af4608：按 rodata 真实键名填充成员。填完 Directory 后调用
   getInvisiblNetLinkFromDmt(&url,&name)，并按其返回值分支：
     false(0) -> this+0x54 = map["Exec"]，this+0x38 = map["Name"]
     true(1)  -> this+0x54 = url，this+0x38 = name（DMT 取到的不可见网络链接）
   注：createObject 原库末尾 return 1，但调用方未消费，此处保持 void（原声明一致）。 */
void AM_EmbeddedLink::createObject(QMap<QString, QString> const& map)
{
    if (map.contains("Type"))
        FTemp00C = map["Type"].toInt();
    if (map.contains("AniIcon"))
        FAniIcon = map["AniIcon"];
    if (map.contains("BigIcon"))
        FBigIcon = map["BigIcon"];
    if (map.contains("Icon"))
        FSmallIcon = map["Icon"];
    if (map.contains("AppID"))
        FUUID = map["AppID"];
    if (map.contains("Directory"))
        FDir = map["Directory"].stripWhiteSpace();

    /* 命令行/名称：优先取 DMT 不可见网络链接，取不到则回退 map 的 Exec/Name */
    QString url, name;
    if (!getInvisiblNetLinkFromDmt(&url, &name)) {
        if (map.contains("Exec"))
            m_cmdLine = map["Exec"].stripWhiteSpace();   /* invoke() system() 执行 */
        if (map.contains("Name"))
            FName = map["Name"];
    } else {
        m_cmdLine = url;
        FName = name;
    }
}

/* ============ getInvisiblNetLinkFromDmt ============ */
/* 反编译 0x41af4c28（返回 undefined4，成功路径 uVar5=1 / 失败 return 0 → 实为 bool）：
     Initialize() 失败 -> return false
     GetSubtree(principal, path, sptr) -> 遍历树 findNode("url")/findNode("name")
     -> DmtData::GetString 取值 -> 写 outUrl/outName -> return true
     最后 Uninitialize()
   ABI 修复：返回类型 void -> bool（createObject 消费其返回值做 Exec/Name vs DMT 值分支）。
   忠实调用签名已知的平台入口 Initialize()/Uninitialize()。
   安全取舍：GetSubtree/findNode 依赖 DmtPrincipal/JemSmartPtr<DmtTree> 内部布局，
   无公开符号、仅虚表派发（vtable+0x8/+0x1c），臆测布局会让平台库读到非法指针崩溃；
   故 Initialize 成功后走安全降级 return false，与原库 Initialize 失败时行为等价，
   令 createObject 回退到 map 的 Exec/Name（确定正确的默认路径）。 */
bool AM_EmbeddedLink::getInvisiblNetLinkFromDmt(QString* outUrl, QString* outName)
{
    if (!DmtTreeFactory::Initialize())
        return false;

    /* GetSubtree + findNode("url"/"name") + DmtData::GetString 依赖平台类内部布局，
       无法在不臆造 ABI 前提下重建虚调用 -> 安全降级 */
    DmtTreeFactory::Uninitialize();
    return false;
}

/* ============ dump (虚表 slot[4] 覆盖) ============ */
void AM_EmbeddedLink::dump()
{
    // 反编译为空函数
}

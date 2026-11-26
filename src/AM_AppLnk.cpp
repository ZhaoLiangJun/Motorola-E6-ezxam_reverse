/* AM_AppLnk.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   虚表证据：AM_AppLnk : AM_AppObject，覆盖全部 5 个虚函数。
   锁成员：getAppLockEnabled @ 0x5A(bool), getAppLockValue @ 0x5C(int)
   uid/gid 映射：见 getUidByString/getGidByString
*/
#include <AM_AppLnk.h>
#include <AM_Global.h>
#include <AM_ActionData.h>
#include <AM_LauncherInterface.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qmap.h>
#include <stdio.h>
#include <stdarg.h>
#include "ringlog.h"
#include "amlog.h"   /* v38: AMLOG/AMVLOG 日志开关 */

/* 运行调试日志（与 AM_RegistryManager 共用 /ram/tmp/amreg.log）
   v38：getExecFullPath / invoke 是启动与刷界面高频路径，默认不写，
   /ram/tmp/am_debug 存在时才写（见 amlog.h）。 */
#define llog(...)  AMVLOG(__VA_ARGS__)

/* ============ 构造函数 ============ */
AM_AppLnk::AM_AppLnk()
    : AM_AppObject(Application),
      m_pad50(0),
      m_field58(false),
      m_massStorage(false),
      m_appLockEnabled(false),
      m_appLockValue(0),
      m_uid(2000),
      m_gid(0),
      m_javaId(0),
      m_javaMemSize(0),
      m_visible(true)
{
}

AM_AppLnk::AM_AppLnk(AM_AppObject::Type type)
    : AM_AppObject(type),
      m_pad50(0),
      m_field58(false),
      m_massStorage(false),
      m_appLockEnabled(false),
      m_appLockValue(0),
      m_uid(2000),
      m_gid(0),
      m_javaId(0),
      m_javaMemSize(0),
      m_visible(true)
{
}

/* ============ 析构函数 ============ */
AM_AppLnk::~AM_AppLnk()
{
}

/* ============ invoke ============ */
/* 反编译 0x41af02a0（AM_AppLnk::invoke(launcher)）完整调用序列（v36 按 PLT 逐一命名）：
     AM_ActionData data;                              ; 0x41ae8e14 C1
     data.setOpenWithSystemApp();                     ; 0x41ae9300
     data.setAction(QString("launch"));               ; rodata 0x41b18a28
     data.setObject(FUUID);                           ; add r1,r7,#0x1c
     data.setValue(QString("path"), getExecFullPath());; 0x41ae8b8c = getExecFullPath
     data.setValue(QString("args"), FTemp04C);         ; add r2,r7,#0x4c  ★Args 解析结果
     data.setValue(QString("uid"),  m_uid);            ; ldr r2,[r7,#0x60] + setValue(QString,int)
     data.setValue(QString("gid"),  m_gid);            ; ldr r2,[r7,#0x64]
     launcher.active(data);                            ; 虚表 +0x40 */
void AM_AppLnk::invoke(AM_LauncherInterface const& launcher)
{
    ring_put(17, (unsigned)this, 0);
    llog("[invoke] uuid=%s exec=%s path=%s args0=%s args=%s",
         (const char*)FUUID.utf8(), (const char*)getExecFullPath().utf8(),
         (const char*)getPath().utf8(),
         (const char*)FTemp048.utf8(), (const char*)FTemp04C.utf8());
    AM_ActionData data;
    data.setOpenWithSystemApp();
    data.setAction(QString("launch"));      /* rodata 0x41b18a28 "launch" */
    data.setObject(FUUID);
    data.setValue(QString("path"), getExecFullPath());  /* rodata 0x41b18a30 "path" */
    data.setValue(QString("args"), FTemp04C);           /* 0x41b18a38 "args" ← this+0x4C */
    data.setValue(QString("uid"), m_uid);               /* 0x41b18a40 "uid"（int 版） */
    data.setValue(QString("gid"), m_gid);               /* 0x41b18a48 "gid"（int 版） */
    launcher.active(data);                  /* 虚函数：触发 launcher 执行 */
}

/* ============ getExecFullPath ============ */
/* 反编译：返回 m_execPath（0x54），若为空则回退 getPath() */
QString AM_AppLnk::getExecFullPath() const
{
    ring_put(15, (unsigned)this, 0);
    /* v18：加日志定位 am 的启动校验（"无效应用程序文件"对话框） */
    QString r = execFullPathImpl();
    llog("[getExecFullPath] dir=%s exec=%s slot=%d dev=%d arg0='%s' arg='%s' -> %s",
         (const char*)FDir.utf8(), (const char*)m_execPath.utf8(),
         FTemp00C, FTemp044, (const char*)FTemp048.utf8(),
         (const char*)FTemp04C.utf8(), (const char*)r.utf8());
    return r;
}

QString AM_AppLnk::execFullPathImpl() const
{
    /* v25（1:1 重写，0x41af1518）：原库结构是「先定 base，再统一拼 /Directory/Exec」，
       而不是每个 case 各拼一套。字面池实测：
         DAT_41af1b04 = "/"        （SEP）
         DAT_41af1b08 = "/javaapps"（slot 3）
         DAT_41af1b0c = "/qtapps"  （slot 2）
       等价 C：
         if (FDir.left(1) == "/")  out = FDir + "/" + Exec;      // Directory 是绝对路径
         else {
             base = (slot==1) ? getNativeDir()
                  : (slot==2) ? getPreloadDir()+"/qtapps"
                  : (slot==3) ? getPreloadDir()+"/javaapps"
                  : (slot==4||slot==5) ? getInstalledQtDir(dev)
                  : "";                                            // 其它：只有相对拼接
             out = base + "/" + FDir + "/" + Exec;
         }
       旧实现 slot 1/4/5 漏掉 "/"+FDir，导致卡应用/内置应用返回
       "/mmc/mmca1/.system//QTDownLoad/SoftManage" 这类缺段路径（am 校验失败 →
       "无效应用程序文件"），这也是"通话记录（Contacts: Directory=addressbook,
       Exec=addrbk）点不动"的直接原因之一。 */
    if (m_execPath.isEmpty())
        return getPath();  /* getPath 已按 Type 拼接路径 */
    if (m_execPath[0] == '/')
        return m_execPath;                      /* Exec 本身是绝对路径 */
    if (!FDir.isEmpty() && FDir[0] == '/')
        return FDir + "/" + m_execPath;         /* Directory 是绝对路径 */

    QString base;
    switch (FTemp00C) {                         /* 安装位置类型（0x0C） */
    case 1:
        base = AM_Global::getNativeDir();
        break;
    case 2:
        base = AM_Global::getPreloadDir() + "/qtapps";
        break;
    case 3:
        base = AM_Global::getPreloadDir() + "/javaapps";
        break;
    case 4:
    case 5:
        base = AM_Global::getInstalledQtDir(
                   static_cast<AM_Global::Device>(FTemp044));
        break;
    default:
        break;                                  /* 原库此分支 base 为空串 */
    }
    return base + "/" + FDir + "/" + m_execPath;
}

/* ============ createObject ============ */
/* 反编译 0x41af0500（1030 条）逐键实证。键名序列与写入偏移（顺序即代码顺序）：
     "Exec"(GOT 0xbd0)          → 仅"存在性校验"，值 stripWhiteSpace 后写 this+0x54
     "Attribute"(0xbd4)         → toInt → this+0x0C   ★不是 "Type"
     "Daemon"(0xbd8)            → toInt → this+0x58
     "MassStorageStatus"(0xbdc) → toInt → this+0x59
     "BigIcon"(0xbe0)           → QString this+0x10
     "Icon"(0xbe4)              → QString this+0x14
     "AniIcon"(0xbe8)           → QString this+0x18
     "AppID"(0xbec)             → QUuid  this+0x20    ★关键
     "LockEnabled"(0xbf0)       → toInt → this+0x5A
     "FixedPosition"(0xbf4)     → toInt → this+0x30
     "FixedAllPosition"(0xbf8)  → toInt → this+0x31
     "Directory"(0xbfc)         → QString(strip) this+0x34
     "Args"(0xc00) + ";"(0xc04) → QStringList::split(";", v, TRUE) → this+0x48=first / 0x4C=last
     "UserID"(0xc08)            → getUidByString → this+0x60
     "GroupID"(0xc0c)           → getGidByString → this+0x64
     "Visible"(0xc10)           → toInt → this+0x40     ★不是 0x80
     "Eraseable"(0xc14)         → toInt → this+0x80     ★0x80 是 Eraseable
     "Name"(0xc18)              → QString this+0x38
     "vender"(0xc20)            → QString this+0x3C（缺省先用 "Motorola"(0xc24) 填）
     "InstalledDate"(0xc28)     → QString this+0x68
     "JavaId"(0xc1c)            → toInt → this+0x6C
     "MpkgFile"(0xc2c)          → QString this+0x70
     "Rotation"(0xc30)          → QString this+0x74
     "Shared"(0xc34)            → toInt → this+0x78
     "Size"(0xc38)              → QString this+0x7C
   旧实现把 "AppID" 存成 QString 到 FUUID(0x1C)、把 "Visible" 写到 0x80、
   并把 "FixedPosition" 写到 0x59 —— 偏移全部错位；尤以 AppID 最致命：
   am 的 Application 启动分支只读 this+0x20 的 QUuid。

   v36：Args 落地（原库 0x41af0bfc..0x41af0d38 实证）
     QString("Args") → map 查值 → QString(";") → QStringList::split(sep, v, allowEmpty=TRUE)
     list.at(0)   → this+0x48
     list.at(last)→ this+0x4C     （sentinel->prev，单元素时与 at(0) 相同）
     AM_AppLnk::invoke @0x41af03d8 用 `add r2, r7, #76` 把 0x4C 当 "args" 值送 launcher。
     实测注册表：Camera `Args = photo` / Camcord `Args = video` 同一程序靠它区分；
                 Browser `Args = ;-s` → 0x48="" , 0x4C="-s"。 */
void AM_AppLnk::createObject(QMap<QString, QString> const& map)
{
    ring_put(16, (unsigned)this, 0);
    if (!map.contains("Exec"))
        return;                       /* 原库：无 Exec 直接返回，不填充任何字段 */

    m_execPath = map["Exec"].stripWhiteSpace();          /* this+0x54 */

    if (map.contains("Attribute"))
        FTemp00C = map["Attribute"].toInt();             /* this+0x0C */
    if (map.contains("Daemon"))
        m_field58 = map["Daemon"].toInt() != 0;          /* this+0x58 */
    if (map.contains("MassStorageStatus"))
        m_massStorage = map["MassStorageStatus"].toInt() != 0;   /* this+0x59 */
    if (map.contains("BigIcon"))
        FBigIcon = map["BigIcon"];                       /* 0x10 */
    if (map.contains("Icon"))
        FSmallIcon = map["Icon"];                        /* 0x14 */
    if (map.contains("AniIcon"))
        FAniIcon = map["AniIcon"];                       /* 0x18 */

    if (map.contains("AppID")) {
        /* 原库把 AppID 解析成 QUuid 存 this+0x20。注册表里带花括号
           （{95ffa462-...}），QUuid(QString) 两种写法都能吃。 */
        QString t = map["AppID"].stripWhiteSpace();
        FAppID = QUuid(t);
        /* 同时保留旧的 FUUID(0x1C) 规范化，供本库内部按 uuid 查表使用：
           注册表组名/Items 都是不带花括号的写法。 */
        int len = t.length();
        if (len > 2 && t[0] == '{' && t[len - 1] == '}')
            t = t.mid(1, len - 2);
        FUUID = t;
    }

    if (map.contains("LockEnabled"))
        m_appLockEnabled = map["LockEnabled"].toInt() != 0;  /* 0x5A */
    /* 注册表实测拼写差异：Customer Services（5b608840-…）那组写成
       "FixedPostion = 0"（少一个 'i'）。原库只认 "FixedPosition"，
       故这里保持原库行为，不额外兼容——0x30 默认 0 时两种写法等价。 */
    if (map.contains("FixedPosition"))
        setFixedPosition(map["FixedPosition"].toInt() != 0);      /* 0x30 */
    else if (map.contains("FixedPostion"))                       /* 拼错变体 */
        setFixedPosition(map["FixedPostion"].toInt() != 0);
    if (map.contains("FixedAllPosition"))
        setFixedAllPosition(map["FixedAllPosition"].toInt() != 0);/* 0x31 */
    if (map.contains("Directory"))
        FDir = map["Directory"].stripWhiteSpace();        /* 0x34 */

    /* ---- Args：原库 0x41af0bfc → 0x41af0d38 ---- */
    if (map.contains("Args")) {
        QStringList al = QStringList::split(QString(";"), map["Args"], true);
        if (!al.isEmpty()) {
            FTemp048 = al.first();                        /* 0x48 */
            FTemp04C = al.last();                         /* 0x4C */
        }
    }

    if (map.contains("UserID"))
        m_uid = getUidByString(map["UserID"]);            /* 0x60 */
    if (map.contains("GroupID"))
        m_gid = getGidByString(map["GroupID"]);           /* 0x64 */
    /* 注册表实测：China Mobile In 那组写的是 "GroupId"（小写 d），
       原库只认 "GroupID" → gid 保持默认 0。这里同样保持原库行为，
       但补一条兜底以免 gid=0 导致 launcher 以 root 组启动。 */
    else if (map.contains("GroupId"))
        m_gid = getGidByString(map["GroupId"]);
    if (map.contains("Visible"))
        FTemp040 = map["Visible"].toInt() != 0;           /* 0x40 */
    if (map.contains("Eraseable"))
        m_visible = map["Eraseable"].toInt() != 0;        /* 0x80 */
    if (map.contains("Name"))
        FName = map["Name"];                             /* 0x38 */

    FVender = QString("Motorola");                        /* 0x3C 缺省值（原库 0x41af1058） */
    if (map.contains("vender"))
        FVender = map["vender"];                          /* 0x3C 实际值（原库 0x41af119c） */

    if (map.contains("InstalledDate"))
        m_javaCmdLine = map["InstalledDate"];             /* 0x68 */
    if (map.contains("JavaId"))
        m_javaId = map["JavaId"].toInt();                 /* 0x6C */
    if (map.contains("MpkgFile"))
        m_javaWorkDir = map["MpkgFile"];                  /* 0x70 */
    if (map.contains("Rotation"))
        m_javaClassPath = map["Rotation"];                /* 0x74 */
    if (map.contains("Shared"))
        m_javaMemSize = map["Shared"].toInt();            /* 0x78 */
    if (map.contains("Size"))
        m_javaUrl = map["Size"];                          /* 0x7C */
}

/* ============ getUidByString ============ */
/* 反编译：lower(输入) 与 "ezx"/"root"/"drm" 比较：
     "ezx"  -> 2000 (ezxUid)
     "root" -> 0    (rootUid)
     "drm"  -> 518  (DRMUid = 0x206)
     default-> 2000 */
int AM_AppLnk::getUidByString(QString const& s)
{
    QString lower = s.lower();
    if (lower == "ezx")  return 2000;
    if (lower == "root") return 0;
    if (lower == "drm")  return 518;   /* 0x206 */
    return 2000;
}

/* ============ getGidByString ============ */
/* 反编译：lower(输入) 与 "ezx" 比较：
     "ezx"   -> 233 (ezxGid = 0xE9)
     default -> 0 */
int AM_AppLnk::getGidByString(QString const& s)
{
    QString lower = s.lower();
    if (lower == "ezx") return 233;   /* 0xE9 */
    return 0;
}

/* ============ dump (虚表 slot[4] 覆盖) ============ */
void AM_AppLnk::dump()
{
    llog("[dump] uuid=%s name=%s", (const char*)FUUID.utf8(),
         (const char*)FName.utf8());
    // 反编译为空函数
}

/* ============ getAppLockEnabled (虚表 slot[0] 覆盖) ============ */
bool AM_AppLnk::getAppLockEnabled() const
{
    llog("[getAppLockEnabled] uuid=%s -> %d", (const char*)FUUID.utf8(),
         (int)m_appLockEnabled);
    return m_appLockEnabled;   /* this[0x5A] */
}

/* ============ setAppLockEnabled (虚表 slot[1] 覆盖) ============ */
void AM_AppLnk::setAppLockEnabled(bool enabled)
{
    m_appLockEnabled = enabled;   /* this[0x5A] = param */
}

/* ============ getAppLockValue (虚表 slot[2] 覆盖) ============ */
int AM_AppLnk::getAppLockValue() const
{
    llog("[getAppLockValue] uuid=%s -> %d", (const char*)FUUID.utf8(),
         m_appLockValue);
    return m_appLockValue;   /* *(this + 0x5C) */
}

/* ============ setAppLockValue (虚表 slot[3] 覆盖) ============ */
void AM_AppLnk::setAppLockValue(int value)
{
    m_appLockValue = value;   /* *(int*)(this + 0x5C) = param */
}

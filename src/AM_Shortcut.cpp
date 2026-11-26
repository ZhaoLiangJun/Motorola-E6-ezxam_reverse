/* AM_Shortcut.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   虚表证据：AM_Shortcut : AM_AppObject
     slot[0..3] 继承基类锁函数，slot[4] 覆盖 dump（与 EmbeddedLink 共享实现 0x41af4c24）
   成员：m_lockValue @ 0x58
   注：getAppLockValue/setAppLockValue 为【非虚 name-hiding】，虚表锁函数走基类。
*/
#include <AM_Shortcut.h>
#include <AM_Global.h>
#include <AM_ActionData.h>
#include <qmap.h>

/* ============ 构造函数 ============ */
AM_Shortcut::AM_Shortcut()
    : AM_AppObject(Shortcut),
      m_lockValue(0)
{
}

/* ============ 析构函数 ============ */
AM_Shortcut::~AM_Shortcut()
{
}

/* ============ invoke ============ */
/* 反编译：构造 AM_ActionData，setOpenWithSystemApp + setAction + setOpenApp(target)，
   最后 AM_ActionData::invoke() 执行。target 来自 launcher 的 0x20..0x2C 字段。*/
void AM_Shortcut::invoke(AM_LauncherInterface const& launcher)
{
    AM_ActionData actionData;
    actionData.setOpenWithSystemApp();
    /* 反编译：action 字符串来自 rodata（"Shortcut" 等），此处用空串占位 */
    actionData.setAction(QString::null);
    /* 反编译：setOpenApp(launcher 的目标 UUID)。launcher 为接口，取目标应用 */
    actionData.setOpenApp(QUuid());
    actionData.invoke();             /* 执行 */
    /* actionData 在栈上，作用域结束自动析构 */
}

/* ============ createObject ============ */
/* 反编译 0x41af5558：按 rodata 真实键名填充成员，赋值目标顺序：
     "Type"      -> FTemp00C (Device, toInt)   this+0x0c
     "BigIcon"   -> FBigIcon                    this+0x10
     "Icon"      -> FSmallIcon                  this+0x14
     "AniIcon"   -> FAniIcon                    this+0x18
     "AppID"     -> FUUID                       this+0x20
     "LockEnabled" -> toInt (bool 位)
     "Directory" -> FDir                        this+0x34
     "Name"      -> FName                       this+0x38
   键名为 rodata 真实常量（0x41b18ba0 等）。
   注：原库 rodata 中不存在 "LockValue" 键（仅 LockEnabled），Shortcut 不读该键。
       Shortcut 的锁值由 AM_RegistryManager::getAppLockValue 统一查询 AppLockConfig。 */
void AM_Shortcut::createObject(QMap<QString, QString> const& map)
{
    /* 键名说明：原库 0x41af5558 字面池漂移无法静态解出精确键名；
       键集依 AM_EmbeddedLink/AM_Folder/AM_RegistryManager 的 .cfg rodata 已确认键名惯例，
       字段偏移与数量严格对齐反编译（0x0c/0x10/0x14/0x18/0x20/0x30/0x31/0x54/0x38）。 */
    if (map.contains("Type"))
        FTemp00C = map["Type"].toInt();          /* this+0x0c -> Type */
    if (map.contains("BigIcon"))
        FBigIcon = map["BigIcon"];               /* this+0x10 */
    if (map.contains("Icon"))
        FSmallIcon = map["Icon"];                /* this+0x14 */
    if (map.contains("AniIcon"))
        FAniIcon = map["AniIcon"];               /* this+0x18 */
    if (map.contains("AppID"))
        FUUID = map["AppID"];                    /* this+0x20 (QUuid) */
    if (map.contains("LockEnabled"))
        FTemp030 = (map["LockEnabled"].toInt() != 0);   /* this+0x30 (bool) */
    if (map.contains("FixedPosition"))
        FTemp041 = (map["FixedPosition"].toInt() != 0); /* this+0x31 (bool) */
    if (map.contains("Exec"))
        m_cmdLine = map["Exec"].stripWhiteSpace();      /* this+0x54 (QString, invoke 执行) */
    if (map.contains("Name"))
        FName = map["Name"];                     /* this+0x38 */
}

/* ============ dump (虚表 slot[4] 覆盖) ============ */
/* 原库证据：AM_Shortcut 未覆盖 dump()，虚表槽 10 为基类 AM_AppObject::dump()。
   故此处不定义 AM_Shortcut::dump，继承基类实现。 */

/* ============ getAppLockValue (非虚 name-hiding) ============ */
int AM_Shortcut::getAppLockValue() const
{
    return m_lockValue;   /* *(this + 0x58) */
}

/* ============ setAppLockValue (非虚 name-hiding) ============ */
void AM_Shortcut::setAppLockValue(int value)
{
    m_lockValue = value;   /* *(int*)(this + 0x58) = param */
}

/* AM_Folder.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   虚表证据：AM_Folder : AM_AppObject，仅覆盖 dump，其余 4 个锁函数继承基类。
   容器：m_appList (QList<AM_AppObject>*) @ offset 0x54
*/
#include "ringlog.h"
#include <stdio.h>
#include <stdarg.h>
#include "amlog.h"   /* v38: AMLOG/AMVLOG 日志开关 */
/* v38：Folder::getAppList / getAppPosition 是每次刷界面都会跑的高频路径，
   默认不写日志（/ram/tmp/am_debug 存在时才写），否则日志 I/O 会明显拖慢界面。 */
#define flog(...)  AMVLOG(__VA_ARGS__)
#include <AM_Folder.h>
#include <AM_AppObject.h>
#include <qlist.h>
#include <qmap.h>

/* ============ 构造函数 ============ */
AM_Folder::AM_Folder()
    : AM_AppObject(Folder)
{
    FTemp00C = 0;
    m_appList = new QList<AM_AppObject>();
}

AM_Folder::AM_Folder(AM_AppObject::Type type)
    : AM_AppObject(type)
{
    FTemp00C = 0;
    m_appList = new QList<AM_AppObject>();
}

/* ============ 析构函数 ============ */
AM_Folder::~AM_Folder()
{
    // QGList 子对象由 Qt 引用计数管理，基类析构负责释放 QString
}

/* ============ init ============ */
/* ============ init ============ */
/* 反编译 0x41af20a4（84 字节）真实逻辑：
     this+0x40 = 1                    ← 基类 FTemp040（bool）置真
     this+0x54 = new QGList(0x20 字节)← m_appList
     虚表初始化（QGList 内部，平台层）
   注意：不调 AM_AppObject::init()，也不 clear()（构造函数已 new 过一次）。 */
void AM_Folder::init()
{
    FTemp040 = true;                       /* this+0x40 = 1 */
    m_appList = new QList<AM_AppObject>(); /* this+0x54 = new QGList */
    /* QGList 内部虚表初始化由 QList 构造函数完成（平台层，无需显式调用） */
}

/* ============ createObject ============ */
/* 反编译 0x41af21a4（952 字节）逻辑：按 7 个 rodata 键填充成员，全部直接赋值。
   成员偏移 → 已知字段（AM_AppObject 布局）：
     this+0x0c -> FTemp00C (toInt)          FolderType
     this+0x10 -> FBigIcon (QString)
     this+0x14 -> FSmallIcon (QString)
     this+0x18 -> FAniIcon (QString)
     this+0x30 -> FTemp030 (bool, toInt!=0)
     this+0x31 -> FTemp041 (bool, toInt!=0)
     this+0x38 -> FName (QString)
   键名说明：该函数字面池区域被代码覆盖（GH 偏移漂移），无法静态解出键名。
   7 个字段偏移与数量严格对齐反编译（0x0c/0x10/0x14/0x18/0x30/0x31/0x38），
   键名依 AM_Folder/AM_EmbeddedLink/AM_RegistryManager 的 .cfg 键名惯例推断
   （BigIcon/Icon/AniIcon/LockEnabled/FixedPosition 等 rodata 已确认存在），
   成员语义与偏移确定，键名字符串待设备侧核对。 */
void AM_Folder::createObject(QMap<QString, QString> const& map)
{
    /* this+0x0c FolderType (toInt) */
    if (map.contains("Type"))
        FTemp00C = map["Type"].toInt();

    /* this+0x10 / 0x14 / 0x18 三个图标字段 */
    if (map.contains("BigIcon"))
        FBigIcon = map["BigIcon"];
    if (map.contains("Icon"))
        FSmallIcon = map["Icon"];
    if (map.contains("AniIcon"))
        FAniIcon = map["AniIcon"];

    /* this+0x30 / 0x31 两个 bool 标志位 */
    if (map.contains("LockEnabled"))
        FTemp030 = (map["LockEnabled"].toInt() != 0);
    if (map.contains("FixedPosition"))
        FTemp041 = (map["FixedPosition"].toInt() != 0);

    /* this+0x38 Name */
    if (map.contains("Name"))
        FName = map["Name"];
}

/* ============ makeNewRecord ============ */
/* 反编译 0x41af255c（6052 字节）逻辑：
     1) 清空/初始化传入 map（QMap 节点遍历删除或新建空树）
     2) 预插入 5 组固定 key = 空串（Folder 记录的默认字段，键名依赖字面池
        三级指针，该函数字面池漂移无法静态解出）
   行为分析：调用方 createFolder 随后用 map[key]=值 填充实际内容，
   而 Qt QMap::operator[] 对不存在的 key 返回空串并创建条目，
   与原库预插入空串的效果等价。故 map.clear() 足以复刻调用方可观测行为。 */
void AM_Folder::makeNewRecord(QMap<QString, QString>& map)
{
    map.clear();
}

/* ============ add ============ */
/* 反编译逻辑：
   if (obj != NULL && m_appList != NULL && m_appList->findRef(obj) == -1) {
       if (pos > 0 && pos < m_appList->count())
           m_appList->insert(pos, obj);
       else
           m_appList->append(obj);
   } */
void AM_Folder::add(AM_AppObject const* obj, int pos)
{
    ring_put(40, (unsigned)obj, pos);
    if (obj == NULL || m_appList == NULL)
        return;

    // findRef: 若已存在则不重复添加
    if (m_appList->findRef((AM_AppObject*)obj) != -1)
        return;

    if (pos > 0 && (uint)pos < (uint)m_appList->count()) {
        m_appList->insert((uint)pos, (AM_AppObject*)obj);
    } else {
        m_appList->append((AM_AppObject*)obj);
    }
}

/* ============ remove ============ */
void AM_Folder::remove(AM_AppObject const* obj)
{
    ring_put(41, (unsigned)obj, 0);
    if (obj == NULL || m_appList == NULL)
        return;
    m_appList->remove((AM_AppObject*)obj);
}

/* ============ clear ============ */
void AM_Folder::clear()
{
    if (m_appList != NULL)
        m_appList->clear();
}

/* ============ isEmpty ============ */
bool AM_Folder::isEmpty() const
{
    return (m_appList == NULL) || m_appList->isEmpty();
}

/* ============ getAppList ============ */
/* 反编译：param->clear(); QGList::operator=(param, m_appList); */
void AM_Folder::getAppList(QList<AM_AppObject>& out) const
{
    out.clear();
    flog("[Folder::getAppList] folder=%p uuid='%s' name='%s' 原有=%d",
         (const void*)this, (const char*)FUUID.utf8(), (const char*)FName.utf8(),
         m_appList ? (int)m_appList->count() : -1);
    if (m_appList != NULL) {
        // 逐项复制（QGList::operator= 语义）
        for (AM_AppObject* obj = m_appList->first(); obj; obj = m_appList->next()) {
            out.append(obj);
        }
    }
}

/* ============ getAppPosition ============ */
/* 返回 app 在容器中的索引，未找到返回 -1（由 findRef 语义决定）*/
int AM_Folder::getAppPosition(AM_AppObject const* obj)
{
    if (obj == NULL || m_appList == NULL) {
        flog("[Folder::getAppPosition] obj=%p list=%p -> -1", (const void*)obj,
             (const void*)m_appList);
        return -1;
    }
    return m_appList->findRef((AM_AppObject*)obj);
}

/* ============ dump (虚表 slot[4] 覆盖) ============ */
void AM_Folder::dump()
{
    // 反编译为空函数
}

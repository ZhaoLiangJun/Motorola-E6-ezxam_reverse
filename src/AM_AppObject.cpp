/* AM_AppObject.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6) */
#include <AM_AppObject.h>
#include <AM_Global.h>
#include <stdio.h>
#include <stdarg.h>

/* v19: 运行调试日志（与 AM_AppLnk 共用 /ram/tmp/amreg.log） */
/* v38：日志统一走 amlog.cpp；热路径用 AMVLOG（默认静默，
   touch /ram/tmp/am_debug 后才输出），避免日志 I/O 拖慢界面。 */
#include "amlog.h"
#define olog(...)  AMVLOG(__VA_ARGS__)
#include <qobject.h>
#include <qapplication.h>   /* v42: qApp->translate(context,key,cmt) 用于 i18n 定位 */
#include <string.h>
#include <strings.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qtextstream.h>
#include <RES_ICON_Reader.h>
#include <SETUP_Utility.h>
#include <SETUP_SET_Manager.h>
#include <ZLanguage.h>
#include <UTIL_Language.h>
#include <stdlib.h>
#include "ringlog.h"

/* ============ 构造函数 ============ */
AM_AppObject::AM_AppObject()
    : FTemp004(0), FTemp008(InvalidType), FTemp00C(0),
      FAppID(), FTemp030(0),
      FTemp040(false), FTemp041(false), FTemp042(false), FTemp044(0)
{
    // QString 成员默认构造为 null
    init();
}

AM_AppObject::AM_AppObject(AM_AppObject::Type type)
    : FTemp004(0), FTemp008(type), FTemp00C(0),
      FAppID(), FTemp030(0),
      FTemp040(false), FTemp041(false), FTemp042(false), FTemp044(0)
{
    init();
}

/* ============ 析构函数 ============ */
AM_AppObject::~AM_AppObject()
{
    // QString 成员自动析构
}

/* ============ init ============ */
void AM_AppObject::init()
{
    FTemp044 = 1;
    FTemp042 = false;
    FTemp004 = 0;
    FTemp040 = false;
    FTemp041 = false;
}

/* ============ 文件夹本地化名表（v40） ============
 *
 * 原库 `AM_AppObject::getName @0x41AEAC88`（14020B）的真实结构（本轮用
 * `gnemu.py` 迷你 ARM 解释器逐条跑通确认，不是猜的）：
 *
 *     switch (Type) {
 *     case Folder(1):      -> 0x41AEAE88  一串 "FAppID == <UUID> ? tr(键,注释) : 下一个"
 *     case Application(2): -> 0x41AEB65C  SETUP 取运营商名（ServerNameForSetup*）
 *     case Shortcut(4):    -> 0x41AEACD4  设备 6/7 时匹配 c1e0df66 -> 下载管理器
 *     default:             -> return FName
 *     }
 *
 * 每个命中分支的收尾都是同一条尾巴：
 *     ldr r1,[sl,#off1] ; ldr r2,[sl,#off2] ; bl QObject::tr
 * 其中两个 GOT 槽指向的正是**源串**与**注释**，实测解出的 13 组键：
 *     TXT_RID_CBUTTON_ALL/"All"、_OFFICE/"Office"、_MULTIMEDIA/"Multimedia"、
 *     _EXTRA/"Extra"、_USER1/"User1"、_USER2/"User2"、_GAMES/"Games"、
 *     _APPLICATION/"Application"、TXT_RID_GRID_ITEM_SETTINGS/"Settings"、
 *     TXT_RID_LIST_A_DIALING_SERVICES/"Dialing Services"、
 *     TXT_RID_LIST_A_DOWNLOAD_MANAGER/"Download Manager"、TXT_RID_3COLUMN_CAMCORD/"Camcord"
 *
 * 也就是说**中文名不是硬编码，而是 tr() 走 am_zh-cn.qm 得到的**
 * （设备 /usr/language/string/zh-cn/am_zh-cn.qm，5996B，已拉回本地核对，
 *  其中确实含 "全部/多媒体" 等译文）。原库因此天然支持多语言。
 *
 * 另外用同一套方法证明了：原库**不翻译应用名**——
 * 整库二进制里根本没有 "Contacts" 这个字符串，应用名一律沿用注册表 Name 字段。
 * 设备上 SysRegistry 的 Name 是英文，所以应用名显示英文与原版一致。
 *
 * 注意：Folder 的注册表组里没有 AppID 键（v29 已证实），所以文件夹的
 * FAppID 是空 QUuid，必须用 FUUID（= 注册表组名）来比对——原库比的是 FAppID，
 * 那是因为原库在 createObject 里把组名也写进了 AppID；本库统一用 FUUID 更稳。
 */
namespace {

struct FolderNameEntry {
    const char* uuid;   /* 注册表组名（UserMenuTree/SysRegistry 的 [uuid]） */
    const char* key;    /* tr() 源串 = TXT_RID_* */
    const char* cmt;    /* tr() 注释 = 英文原文（原库写死在字面池里） */
};

const FolderNameEntry g_folderNames[] = {
    { "cb839c8f-72c3-42ac-bf17-b01080cb5705", "TXT_RID_CBUTTON_ALL",            "All" },
    { "e8c83dac-db4e-4fb5-98a6-61de1998b277", "TXT_RID_CBUTTON_OFFICE",         "Office" },
    { "b18dbbd2-503a-4442-9308-5575ee38c210", "TXT_RID_CBUTTON_MULTIMEDIA",     "Multimedia" },
    { "51f2cb88-3040-4f00-b66c-c4bb974e4d18", "TXT_RID_CBUTTON_EXTRA",          "Extra" },
    { "48973844-3955-4323-b5a0-f69435a1dba0", "TXT_RID_CBUTTON_USER1",          "User1" },
    /* ★v61：User2 之前**漏了**——菜单里只有它显示英文 "User2"。
       译文在 lib_zh-cn.qm 里（自定2），hash=elfHash(cmt+key)=0b8bfa52，已核对。 */
    { "3bb7f1b5-12bb-4b79-985b-96436e094256", "TXT_RID_CBUTTON_USER2",          "User2" },
    { "c34463b7-800f-483b-8e01-5ea39730d7d5", "TXT_RID_GRID_ITEM_SETTINGS",     "Settings" },
    { "fe948c2a-b7a6-4dac-a1d1-915b3ba57168", "TXT_RID_LIST_A_DIALING_SERVICES","Dialing Services" },
    { "67dc1191-2928-4938-abce-1a73cb5ce490", "TXT_RID_CBUTTON_GAMES",          "Games" },
    { 0, 0, 0 }
};

/* ============ 应用名中文表（v42 增强，原库不做这件事） ============
 *
 * 原库 `getName` 对 Type=Application/Shortcut **只返回注册表 Name 字段**（英文），
 * 全库没有第二个取名的路径（见 docs/I18N.md §7 的三条证据）。
 * 所以应用名要显示中文，只能自建表 —— 这里按"注册表组名(uuid) → 中文"映射。
 *
 * 取材原则（避免瞎翻）：
 *   1) 只翻语义明确的常用项；专业缩写（STK/UAC/VCM/WSPServer/Picsel/S3/Engineering）
 *      一律不翻，未命中表就回落 FName，显示英文，不会出现怪译名。
 *   2) 只在**中文翻译器真的装上了**的时候启用（判据见 amChineseActive()），
 *      英文固件上不会莫名其妙冒出中文。
 *
 * ★v64 关键修正：本表的键必须是 **FUUID（= 注册表 AppID）**，不是注册表组名！
 *   `AM_AppLnk::createObject` 遇到 "AppID" 键就把 FUUID 覆盖成 AppID
 *   （src/AM_AppLnk.cpp：`FAppID = QUuid(t); ... FUUID = t;`），
 *   `AM_RegistryManager::createObject` 只在 UUID 为空时才回填组名。
 *   SysRegistry 72 个组里有 **18 个组名 ≠ AppID**（实测扫描 _SysRegistry_now）：
 *     fc4eee73(China Mobile In) → af8770bf、5b608840(Customer Services) → 897fb59e、
 *     5d73dbd1(FixedDial) → fc182488、28ff7b93(ServiceDial) → d5a68f04、
 *     eff907f3(DownloadManager) → ea1572d1、bc062df8(dmfota) → 90b182ae、
 *     1178f368(DM) → eb475096、691bbbc0(DRM) → ab5d34ee、
 *     b86502c1(Data Connections) → 87355437、9aaa6311(screensaver) → 1d619a59、
 *     4c99e984(Engineering) → 08e91b63、ba63e2ca(MediaPlayer) → 同一个 AppID……
 *   按组名建表（v42 做法）时这 18 个全部查不到 → 菜单里显示英文。
 *   实测日志（v63，/ram/tmp/am_debug 打开后）：
 *     [getAppObject] HIT 'fc4eee73-…' -> 0xed110 type=2 name='China Mobile In'
 *     紧随其后**没有** [appName] 行 —— 查表落空的直接证据。
 *   下面同时保留组名与 AppID 两种键，两种都命中，互不影响。 */
struct AppNameEntry { const char* uuid; const char* zh; };

const AppNameEntry g_appNames[] = {
    /* —— 通讯类 —— */
    { "220072c6-9008-468e-9968-11f3c50fa95e", "信息" },            /* Messages */
    { "0a92f2c5-584d-440a-954c-1a155079d71a", "通讯录" },          /* Contacts */
    { "ce59717d-fb23-4b7c-8800-25ce613f4042", "通话记录" },        /* RecentCall */
    { "5d73dbd1-a51a-4a46-b7ac-4a34a32ceb5e", "固定拨号" },        /* FixedDial */
    { "28ff7b93-0139-4c33-af5a-5a04ad5052f0", "服务拨号" },        /* ServiceDial */
    { "bb621a8f-c681-477f-9d15-9c4b53b37a3e", "电子邮件" },        /* EMail */
    { "949dc9d2-8ca6-44d9-9f21-67fac1177a22", "即时消息" },        /* IM */
    { "fc4eee73-2b5b-4ebd-b6dc-32dd62fbd490", "中国移动" },        /* China Mobile In */
    { "5b608840-eb39-4d34-bfdb-8806436d140d", "客户服务" },        /* Customer Services */
    { "bacc9585-ae26-4cf9-8e72-fb520709b4b0", "STK" },             /* 保留原名 */

    /* —— 多媒体 —— */
    { "c1e0df66-11ef-4b33-bddb-0cbd13827312", "相机" },            /* Camera */
    { "5f05b101-02ad-432c-99dd-1693110f3b35", "摄像机" },          /* Camcord */
    { "ba63e2ca-089c-4a95-8e13-81522eed47a0", "播放器" },          /* MediaPlayer */
    { "6cd5ea93-d3e2-4cbd-ac46-9dad6ff62b46", "收音机" },          /* Radio */
    { "4d690409-f29f-48f4-8967-48fc151b2272", "录音机" },          /* Recorder */
    { "5382eeae-3a6f-40ef-ac3a-6f85c4a7f87f", "媒体编辑" },        /* MediaEditor */
    { "b92adf27-d073-451d-a990-4ac5e0c87596", "移动Flash播放器" }, /* MFlashPlayer */
    { "1c280c0d-bbb4-4947-b1c9-afe6c6a62139", "移动Flash" },       /* Mobile Flash */

    /* —— 工具 —— */
    { "0b6c1459-2fbe-4b2e-95c9-a4f93762fa11", "计算器" },          /* Calculator */
    { "1e6b9963-24cc-41f8-bbee-a23ae761ff81", "日历" },            /* Calendar */
    { "9a4cdb74-34ec-415e-ba36-b8c2a35d0071", "任务" },            /* Tasks */
    { "23723ec3-ab86-408a-86c0-f70ac6f5c153", "世界时钟" },        /* WorldClock */
    { "6aff7618-c940-48d9-ad3b-3106d9a884c2", "闹钟" },            /* AlarmClock */
    { "c30158ae-e2a7-4d1c-9f6c-3b079c2f4c4e", "记事本" },          /* Notes */
    { "9faafa35-0688-4529-8310-04f215cb2d0c", "我的收藏" },        /* FileMgr(mystuff) */
    { "eff907f3-f5c4-44df-8837-6fcca656e8d7", "下载管理器" },      /* DownloadManager */
    { "83e501e2-4308-4691-a971-e771221197e4", "帮助" },            /* Help */
    { "32d4c7dd-df1a-4da7-986e-9d844934f1ff", "搜索" },            /* Search */
    { "18b62bb5-bd39-4397-9076-fe912a4b6dd0", "词典" },            /* talkingdict */
    { "18b62bb5-bd39-4397-9076-fe912a4b6dd1", "朗文词典" },        /* longmantd */
    { "45e9c626-06be-40d0-a166-fd5b85471aa8", "名片识别" },        /* BCReader */
    { "c4e5410b-c0fe-4889-8619-6bdce79f0e47", "条形码识别" },      /* Barcode Reader */
    { "e6e6fdea-4d8a-494b-99ce-37cec94d6b9a", "条形码识别" },      /* TWN Barcode Reader */

    /* —— 网络/连接 —— */
    { "95ffa462-7e2f-49a9-9e6d-f8f63b2f3c40", "浏览器" },          /* Browser */
    { "b86502c1-6115-4adb-acb4-be2c5eac68df", "数据连接" },        /* Data Connections */
    { "f9563cd6-ef69-44f8-aa73-3bc5decfd7b6", "连接" },            /* Connections */
    { "626d3290-2ec3-4b34-88f8-a3641dbc5f85", "网络设置" },        /* Network Settings */
    { "9667de53-e43e-47bd-96ff-89c06dc3cdbd", "蓝牙" },            /* Bluetooth */
    { "067a149d-00b4-4ba3-b839-11ab2dbd88c0", "同步" },            /* Sync */
    { "0f0c629a-9524-4fc9-8429-6df7dedf394d", "调制解调器" },      /* Modem */

    /* —— 设置类 —— */
    { "5af701f6-33e3-4fe6-b15e-b7ef7c986c74", "设置" },            /* Setup */
    { "a226e86a-0ba2-40bc-9636-56f466fde523", "通话设置" },        /* Call Settings */
    { "f97a1d55-5506-458e-8db1-a00d46ea4df9", "安全设置" },        /* Security Settings */
    { "3fb732ee-6aad-405f-a0a0-6f8aaef0e603", "情景模式" },        /* Profile */
    { "a34ef25d-d791-4aee-943d-db5db70c6220", "主题" },            /* Themes */
    { "691bbbc0-a7a6-4206-80c5-a158a0a997b9", "数字版权" },        /* DRM */
    { "1178f368-c398-4ec6-bac7-d833fd21e188", "设备管理" },        /* DM */
    { "bc062df8-aa16-45ff-b890-a1748a744b35", "系统更新" },        /* dmfota */
    { "9aaa6311-ae30-4114-92b9-9bd0b3faf861", "屏幕保护" },        /* native_screensaver */

    /* —— AppID 别名（v64）——
       查表用的是 FUUID，而 FUUID 被 AM_AppLnk::createObject 覆盖成了注册表
       **AppID**。下面这些应用的 AppID ≠ 组名，按组名建的行永远查不到，
       必须再按 AppID 补一遍。组名行保留：万一将来某条路径 FUUID=组名，
       两种键都能命中。
       注：c1e0df66 同时是 Camera 的组名和 Camcord 的 AppID（注册表复用同一个
       AppID），两个对象 FUUID 完全相同、无法在 getName 里区分，暂统一显示"相机"。 */
    { "af8770bf-b66d-4b29-a70b-9de205add992", "中国移动" },        /* China Mobile In  (组 fc4eee73) */
    { "897fb59e-d690-4aae-b3a0-fe489aebdbcb", "客户服务" },        /* Customer Services(组 5b608840) */
    { "fc182488-537d-4355-aab2-9a0167dbf1eb", "固定拨号" },        /* FixedDial        (组 5d73dbd1) */
    { "d5a68f04-8d4b-4964-9d85-870f5cdad20c", "服务拨号" },        /* ServiceDial      (组 28ff7b93) */
    { "ea1572d1-bf6c-4533-bd73-98c423b94369", "下载管理器" },      /* DownloadManager  (组 eff907f3) */
    { "90b182ae-c160-4a2a-9085-d1ad1b38fb5a", "系统更新" },        /* dmfota           (组 bc062df8) */
    { "eb475096-b5fe-4571-b916-b19014f0fa44", "设备管理" },        /* DM               (组 1178f368) */
    { "ab5d34ee-042c-4dd9-87eb-c37e4f79ec8f", "数字版权" },        /* DRM              (组 691bbbc0) */
    { "87355437-d7d2-463a-946d-a6c5cc6c361a", "数据连接" },        /* Data Connections (组 b86502c1) */
    { "1d619a59-37c2-4893-b9d1-0d8cc3da0699", "屏幕保护" },        /* native_screensaver(组 9aaa6311) */

    /* —— v65：v64 遗留的三个"有意不译"项收尾 ——
       这三个不是专业缩写，而是内部代号，语义唯一、译名不会歧义：
         am(58223c91)          = 应用管理（am 本体）
         dmfota_flex(3ba4abcc) = 固件更新（FOTA = Firmware Over-The-Air；flex 是定制构建标记）
         Engineering(08e91b63) = 工程模式
       仍保持不译的：STK / UAC / VCM / WSPServer / Picsel / S3 / TTS / KVM / NewsExta
       —— 它们要么是行业缩写、要么是第三方组件名，硬译只会更难懂。 */
    { "58223c91-311d-4f7f-9f4b-0f1cc4238cd3", "应用管理" },        /* am               (组 fbcd464d) */
    { "3ba4abcc-904d-4b77-9c7c-087eadf26361", "固件更新" },        /* dmfota_flex      (组 d8a763c1) */
    { "08e91b63-2234-4880-8b2c-0a2ae9fab35c", "工程模式" },        /* Engineering      (组 4c99e984) */

    { 0, 0 }
};

/* 当前语言是不是中文？判据：中文翻译器装上后，这个键必然被译成"全部"。
 * 比查 /usr/language 目录存在更准（目录在、但用户选了英文时不该显示中文）。 */
bool amChineseActive()
{
    static int s = -1;
    if (s < 0) {
        QString t = QObject::tr("TXT_RID_CBUTTON_ALL", "All");
        s = (t != QString::fromLatin1("TXT_RID_CBUTTON_ALL")) ? 1 : 0;
    }
    return s == 1;
}

/* 应用名中文查表（带缓存）。未命中返回 QString::null → 调用方回落 FName。 */
QString amAppLocalName(const QString& uuid)
{
    if (uuid.length() != 36)
        return QString::null;
    if (!amChineseActive())
        return QString::null;

    static QMap<QString, QString>* s_loc = 0;
    if (!s_loc)
        s_loc = new QMap<QString, QString>;
    QMap<QString, QString>::ConstIterator hit = s_loc->find(uuid);
    if (hit != s_loc->end())
        return hit.data().isEmpty() ? QString::null : hit.data();

    const char* u = uuid.latin1();
    if (!u)
        return QString::null;
    for (int i = 0; g_appNames[i].uuid; ++i) {
        if (strcasecmp(u, g_appNames[i].uuid) != 0)
            continue;
        QString zh = QString::fromUtf8(g_appNames[i].zh);
        (*s_loc)[uuid] = zh;
        {
            static int s_n = 0;
            if (amLogVerbose() && s_n < 80) {
                ++s_n;
                AMLOG("[appName] %s -> '%s'", g_appNames[i].uuid,
                      (const char*)zh.local8Bit());
            }
        }
        return zh;
    }
    /* v64：落空也要留痕。以前只记命中，导致"菜单里某项还是英文"这类问题
       只能靠猜（v63 就是这样多花了一轮）。注意这是热路径，靠 amLogVerbose()
       和次数上限双闸门，正式固件上 /ram/tmp/am_debug 不存在时零开销。 */
    {
        static int s_miss = 0;
        if (amLogVerbose() && s_miss < 40) {
            ++s_miss;
            AMLOG("[appName] MISS '%s'", u);
        }
    }
    (*s_loc)[uuid] = QString("");
    return QString::null;
}

/* 查表：命中返回 tr() 结果（已本地化），未命中/无译文返回 QString::null。
 * 关键保护：设备若没加载 am_zh-cn.qm，tr() 会把**源串本身**返回
 * （"TXT_RID_CBUTTON_ALL"），那比英文 "All" 还难看 —— 此时必须回落到 FName。
 *
 * 性能：getName() 每次绘制每个图标都会被调（本机主菜单 ~40 个图标），
 * 所以先用 length()==36 快速排除、再取一次 latin1()（Qt2 会缓存在
 * QStringData 里，后续零分配）做 strcasecmp，命中才走 tr()。 */
QString amFolderLocalName(const QString& uuid)
{
    if (uuid.length() != 36)          /* 快速排除空串/非 UUID（热路径） */
        return QString::null;
    /* v41：结果缓存。getName() 每帧每图标都会被调，tr() 虽然不慢但也别每次做。
       缓存里存空串表示"查过，没有译文"，下次直接跳过。 */
    static QMap<QString, QString>* s_loc = 0;
    if (!s_loc)
        s_loc = new QMap<QString, QString>;
    QMap<QString, QString>::ConstIterator hit = s_loc->find(uuid);
    if (hit != s_loc->end())
        return hit.data().isEmpty() ? QString::null : hit.data();

    const char* u = uuid.latin1();
    if (!u)
        return QString::null;
    for (int i = 0; g_folderNames[i].uuid; ++i) {
        if (strcasecmp(u, g_folderNames[i].uuid) != 0)
            continue;
        QString t = QObject::tr(g_folderNames[i].key, g_folderNames[i].cmt);
        /* v61：删掉 v42 的 6 组 tr 对照探针（QObj.tr/AMObj.tr/tr(cmt)/
           qApp->translate 三种 context 各打一遍）。那是当时为了定位"译文挂在
           哪个 context 上"做的实验，结论已经固化到本函数里，留着只会让
           getName 这条热路径多做 5 次 tr 查找。 */
        /* 无译文时 tr() 把源串原样返回 → 必须回落 FName。
           v42：用 strcmp 取代 QString::fromLatin1(key) 比较，避免热路径每次
           都构造一个 QString（这一句在没装语言包的设备上是"每次绘制都走"）。 */
        if (t.isEmpty() || strcmp(t.latin1(), g_folderNames[i].key) == 0) {
            (*s_loc)[uuid] = QString("");   /* 无译文 / 无翻译器：记住，别再查 */
            return QString::null;
        }
        (*s_loc)[uuid] = t;
        return t;
    }
    (*s_loc)[uuid] = QString("");
    return QString::null;
}

} /* namespace */


/* ============ S3 应用的「运营商下发名」 ============
 * 原库 AM_AppObject::getName @0x41aeac88 的**最开头**有一段特殊分支（v67 补）：
 *
 *   if (Type==Application(2) && Device∈{1,2}) {
 *       if (AppID == 220072c6-9008-468e-9968-11f3c50fa95e （信息）) goto 普通流程;
 *       if (AppID == 5715eb5a-e6e0-4538-bd86-a0e6379ea78c （S3）) {
 *           path = "%1%2".arg(getenv("EZX_APP_WRITE_PATH")).arg("/setup/ezx_screen3.cfg")
 *           mgr  = SETUP_SET_Manager(path, false)
 *           prof = mgr.getSettingInfo("ConnectPublicInfo","CurrentConnectProfileName","",false)
 *           lang = UTIL_Language::getLanguageIdByLanCode(ZLanguage::getSystemLanguageCode())
 *           key  = lang==20 ? "ServerNameForSetupInSimplifiedChinese"      (0x14)
 *                : lang==22 ? "ServerNameForSetupInTraditionalChinese"     (0x16)
 *                :            "ServerNameForSetupInEnglish"
 *           val  = mgr.getSettingInfo(prof, key, out, **"S3"**, false)      ← 默认值就是 "S3"
 *           return val;                                                    ← 无条件返回
 *       }
 *   }
 *
 * 证据：字面池 0x41aebcec/0x41aebcf0/0x41aebcf4 = "%1%2"/EZX_APP_WRITE_PATH//setup/ezx_screen3.cfg；
 *       0x41aebcf8/cfc/d00 = ConnectPublicInfo / CurrentConnectProfileName / ""；
 *       0x41aebd04/d08/d0c = ServerNameForSetupIn{English,TraditionalChinese,SimplifiedChinese}；
 *       0x41aebd10 = "S3"（默认值）；UUID 常量 0x41aebce0=0x220072c6 / 0x41aebce4=0x5715eb5a。
 *
 * 性能：原库每次都新建 SETUP_SET_Manager（要解析一遍 .cfg），而 getName 是每次重绘都调的
 *       热路径。这里对**唯一会走到这条路的 S3 应用**做一次性缓存（缓存指针故意不释放，
 *       与 amAppLocalName 的 s_loc 同风格），避免给菜单引入解析开销。
 *       代价：运营商改名字后要重启 am 才生效——而改这类配置的界面本来就会重启 am。
 */

/* ============ getName ============
 * 原库 0x41AEAC88（14020B）。本库按上面证明的语义实现：
 *   Folder      → 文件夹 UUID 表 → tr(键,注释) → 中文（全部/办公/多媒体/附加…）
 *   Shortcut    → 设备 6/7 且 uuid=c1e0df66…=Download Manager 时 tr(下载管理器)
 *   其余（Application/EmbeddedLink/未命中）→ FName（与原库一致）
 *
 * 为什么 Application 分支仍是 FName：那一段（0x41AEB65C 起、55 个 UUID 比较 +
 * SETUP_SET_Manager::getSettingInfo 取 ServerNameForSetupInSimplifiedChinese）
 * 是给「中国移动/客户服务」这类**运营商定制名**用的，取的是 SETUP 配置里的
 * 运营商串。本机注册表里这些名字已是中文（"中国移动"/"客户服务"），
 * 走 FName 结果相同；强行接 SETUP 反而要复制 SETUP_SET_Manager 的取键细节。
 */
QString AM_AppObject::getName() const
{
    ring_put_name((unsigned)this);

    int type = FTemp008;
    if (type == Folder) {
        QString n = amFolderLocalName(FUUID);
        if (!n.isNull())
            return n;
        return FName;
    }
    if (type == Shortcut) {
        /* 原库先判 0x0C（设备槽位）∈ {6,7}，再比 FAppID == c1e0df66-11ef-4b33-…  */
        if (FTemp00C == 6 || FTemp00C == 7) {
            static const uchar dm4[8] = { 0xbd, 0xdb, 0x0c, 0xbd, 0x13, 0x82, 0x73, 0x12 };
            if (FAppID.data1 == 0xc1e0df66UL && FAppID.data2 == 0x11ef &&
                FAppID.data3 == 0x4b33 && memcmp(FAppID.data4, dm4, 8) == 0) {
                QString t = QObject::tr("TXT_RID_LIST_A_DOWNLOAD_MANAGER",
                                        "Download Manager");
                if (!t.isEmpty() &&
                    t != QString::fromLatin1("TXT_RID_LIST_A_DOWNLOAD_MANAGER"))
                    return t;
            }
        }
        /* v42：原库这里直接返回 FName；中文环境下先查应用名表（增强，非 1:1） */
        QString an = amAppLocalName(FUUID);
        if (!an.isNull())
            return an;
        return FName;
    }
    if (type == Application) {
        /* v67：先查「运营商下发的名字」——原库 0x41aeac88 最开头那段，
           只对 Device∈{1,2} 且 AppID == S3(5715eb5a-…-a0e6379ea78c) 的应用生效。
           同一分支里还会先比一次「信息」(220072c6-…)，命中则**跳过**这段查表
           直接走普通流程——两者是 if/else if 的关系，这里只处理 S3 这一支。 */
        if (FTemp00C == 1 || FTemp00C == 2) {
            static const uchar s3_d4[8] = { 0xbd, 0x86, 0xa0, 0xe6, 0x37, 0x9e, 0xa7, 0x8c };
            if (FAppID.data1 == 0x5715eb5aUL && FAppID.data2 == 0xe6e0 &&
                FAppID.data3 == 0x4538 && memcmp(FAppID.data4, s3_d4, 8) == 0) {
                /* 原库这里是**内联**在上面的：path→SETUP_SET_Manager→读 prof→
                   按语言选 key→取值（默认 "S3"）→直接 return。
                   等价地搬进来（不另开函数），这样 audit_all 的调用序列比对
                   才能在 getName 里看到 ZLanguage/UTIL_Language/SETUP_SET_Manager。
                   结果按进程缓存一次，避免热路径反复解析 .cfg。 */
                static QString* s_s3 = 0;
                if (!s_s3) {
                    s_s3 = new QString;
                    const char* env = getenv("EZX_APP_WRITE_PATH");
                    QString path = QString(env ? env : "") + QString("/setup/ezx_screen3.cfg");
                    /* ★必须 new 到堆上，不能放栈上：
                       SETUP_SET_Manager 的真身比我们声明的（只继承 ZConfig、无自有成员）
                       大得多，栈上实例会被平台 ctor 写穿调用者的栈帧 →
                       函数返回时跳到垃圾地址（实测真机 pc=0x1c / lr=0x1d 的循环崩溃）。
                       这正是 PLAYBOOK §3.5「空壳类 sizeof 必须与真身一致」的复发。 */
                    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(path, false);
                    QString prof;
                    mgr->getSettingInfo(QString("ConnectPublicInfo"),
                                        QString("CurrentConnectProfileName"),
                                        prof, QString(""), false);
                    int lang = (int)UTIL_Language::getLanguageIdByLanCode(
                                   ZLanguage::getSystemLanguageCode());
                    const char* key = (lang == 20) ? "ServerNameForSetupInSimplifiedChinese"
                                    : (lang == 22) ? "ServerNameForSetupInTraditionalChinese"
                                    :                "ServerNameForSetupInEnglish";
                    mgr->getSettingInfo(prof, QString::fromLatin1(key), *s_s3,
                                        QString("S3"), false);
                    delete mgr;
                }
                if (!s_s3->isEmpty())
                    return *s_s3;
            }
        }
        QString an = amAppLocalName(FUUID);
        if (!an.isNull())
            return an;
        return FName;
    }
    return FName;   /* getName 调用极频（每次绘制），不能写日志——启动超时 */
}

/* ============ getDefaultName ============
 * 原库 0x41AEE34C（1012B）：与 getName 的 Folder 链几乎同构（同一批 UUID、
 * 同一个 sl 基址、同样的 tr(键,注释)），差别只在末尾对 Games/Application 用
 * SETUP_Utility::getFlexBit(0xAE) 二选一。这里同样走文件夹表。
 */
QString AM_AppObject::getDefaultName()
{
    if (FTemp008 == Folder) {
        QString n = amFolderLocalName(FUUID);
        if (!n.isNull())
            return n;
    }
    return FName;
}

/* ============ setName ============ */
void AM_AppObject::setName(QString const& name)
{
    FName = name;
}

/* ============ getBigIcon(QPixmap&) ============ */
void AM_AppObject::getBigIcon(QPixmap& pixmap) const
{
    ring_put(3, (unsigned)this, 0);
    getIcon(pixmap, Big);
}

/* ============ getSmallIcon(QPixmap&) ============ */
void AM_AppObject::getSmallIcon(QPixmap& pixmap) const
{
    ring_put(4, (unsigned)this, 0);
    getIcon(pixmap, Small);
}

/* ============ getAnimationIcon(QPixmap&) ============ */
void AM_AppObject::getAnimationIcon(QPixmap& pixmap) const
{
    ring_put(5, (unsigned)this, 0);
    getIcon(pixmap, Ani);
}

/* ============ 图标缓存（v41） ============
 *
 * 原库 getIcon 只按需取图标；本库以前的实现**每次绘制**都：
 *   QFileInfo::exists()（一次 stat 系统调用）→ 失败 → RES_ICON_Reader::getIcon()
 *   （查皮肤 + 解码 .gif/.png）
 * 主菜单有 ~40 个图标，一次重绘就是 40 次 stat + 40 次图片解码。
 * 实测（/proc/<pid>/stat 采样）：空闲时 am 仍处于 R 状态、40 秒吃掉
 * **11.4 秒系统态 CPU**（stime 从 199 涨到 1336 tick），用户态只涨 1.9 秒
 * ——典型的"系统调用风暴"，最终把 CPU 占满、输入事件排不上队 = 屏幕点不动。
 *
 * 这里加一层进程内缓存：key = "路径|图标名|类型"，命中直接返回（QPixmap 在
 * Qt2.3.8 里是 QShared 隐式共享，拷贝只加引用计数，几乎零成本）。
 * 查不到的那一项也缓存空 QPixmap，避免反复 stat 不存在的路径。 */
namespace {
QMap<QString, QPixmap>* g_iconCache = 0;
/* 上限 64（不是 192）：本机 MemFree 常态只有 1.3MB / 46MB。
   QPixmap 在 Qt2.3.8 是 QShared，am 自己已经持有的图标进缓存只加引用计数、不多占内存；
   但 am 丢弃后缓存仍持有 → 最多多留 64 张。按 big icon ≈ 48×48×4B ≈ 9KB 粗算
   ≈ 576KB 上限，仍嫌多时再降。菜单一屏 12 个，64 足够覆盖翻页，
   真超了也只是"整体清空重新解码一次"，不会进死循环。 */
const int kIconCacheMax = 64;
}

/* 供 AM_RegistryManager 在装/卸载后调用：图标可能变了，清掉重取 */
void amAppIconCacheClear()
{
    if (g_iconCache)
        g_iconCache->clear();
}

/* ============ getIcon(QPixmap&, IconType) ============ */
/* 原库 0x41aee780：按类型取图标名 → 按 Type 拼路径 → QFileInfo 存在则
   QPixmap(path) 直接加载；否则走 RES_ICON_Reader::getIcon(图标名, bool)
   从皮肤 iconres.ezx 资源加载（getPreloadDir=getenv("EZX_PRELOAD_APP_PATH")，
   本机未设置该环境变量，拼出的文件路径不存在，实际都走资源加载）。 */
void AM_AppObject::getIcon(QPixmap& pixmap, AM_AppObject::IconType type) const
{
    ring_put(2, (unsigned)this, type);
    QString iconFile;
    switch (type) {
    case Big:    iconFile = FBigIcon; break;
    case Small:  iconFile = FSmallIcon; break;
    case Ani:    iconFile = FAniIcon; break;
    }
    if (iconFile.isEmpty())
        return;

    QString fullPath;
    /* v21（决定性）：路径按安装位置类型（0x0C）分支——与原库 getIcon
       （0x41aee780 按 this+0xc switch）及 getExecFullPath 同一模式。
       卡应用图标在应用目录（QTDownLoad/<FDir>/icon-big.png），
       旧实现统一用 preload 前缀 → 卡应用图标永远找不到。 */
    switch (FTemp00C) {
    case 2:
        fullPath = AM_Global::getPreloadDir() + "/qtapps/" + FDir + "/" + iconFile;
        break;
    case 3:
        fullPath = AM_Global::getPreloadDir() + "/javaapps/" + FDir + "/" + iconFile;
        break;
    case 4:
    case 5:
        fullPath = AM_Global::getInstalledQtDir(
                       static_cast<AM_Global::Device>(FTemp044))
                   + "/" + FDir + "/" + iconFile;
        break;
    case 1:
    default:
        fullPath = AM_Global::getNativeDir() + "/" + FDir + "/" + iconFile;
        break;
    }
    if (FDir.length() > 0 && FDir[0] == '/')
        fullPath = FDir + "/" + iconFile;      /* Directory 是绝对路径 */

    /* ---- v41 缓存命中直接返回 ---- */
    QString ck;
    ck.sprintf("%s|%s|%d", fullPath.latin1(), iconFile.latin1(), (int)type);
    if (!g_iconCache)
        g_iconCache = new QMap<QString, QPixmap>;
    QMap<QString, QPixmap>::ConstIterator ci = g_iconCache->find(ck);
    if (ci != g_iconCache->end()) {
        pixmap = ci.data();
        return;
    }

    QPixmap result;
    QFileInfo fi(fullPath);
    if (fi.exists()) {
        QPixmap pm(fullPath);
        if (!pm.isNull())
            result = pm;
    }
    if (result.isNull()) {
        /* 内置应用的 .gif 图标在皮肤资源里（iconres.ezx），文件路径不存在时回退 */
        /* 关键（v12 修复）：结果必须写入输出参数——旧实现调用了 RES_ICON_Reader
           却丢弃返回值，图标永远为空。 */
        /* v69：RES_ICON_Reader 的真身由设备 libezxappbase 提供，本地声明只是空壳
           （只有 ctor + getIcon 等成员函数）。栈上实例化会被真身 ctor 写穿栈帧，
           与 v68 的 SETUP_SET_Manager 是同一类事故，一律改堆。 */
        RES_ICON_Reader* reader = new RES_ICON_Reader();
        result = reader->getIcon(iconFile.latin1(), type == Ani);
        delete reader;
    }

    pixmap = result;
    if (g_iconCache->count() >= kIconCacheMax)
        g_iconCache->clear();            /* 简单粗暴：超上限整体清空（极少发生） */
    (*g_iconCache)[ck] = result;
}

/* ============ getBigIcon() (返回值) ============ */
/* 原库 0x41aef308 `_ZNK12AM_AppObject10getBigIconEv`：只读 + sret(QPixmap)。
   语义 = getIcon(pixmap, Big) 的按值包装。 */
QPixmap AM_AppObject::getBigIcon() const
{
    QPixmap pixmap;
    getBigIcon(pixmap);
    return pixmap;
}

/* ============ getVender ============ */
/* 原库 0x41aef960（24 字节）：`add r1,r1,#0x3c` → QString 拷贝构造 → 返回 sret。
   即 `QString AM_AppObject::getVender() { return FVender; }` */
QString AM_AppObject::getVender()
{
    ring_put(8, (unsigned)this, 0);
    return FVender;
}

/* ============ getSize ============ */
/* 反编译 0x41aef978：
   if (FTemp008 <= 1) return 0;   ← Folder(1) 及以下直接返回 0
   path = getPath();
   if (path.isEmpty()) return 0;
   QDir dir(path, "*", Name|DirsFirst);
   if (dir.count() == 0) return 0;
   return AM_Global::getBlockSize(path) >> 10;   ← 字节→KB */
int AM_AppObject::getSize()
{
    ring_put(6, (unsigned)this, 0);
    if (FTemp008 <= 1)          /* Folder(1) 及 InvalidType(0) 直接返回 0 */
        return 0;
    QString path = getPath();
    olog("[getSize] uuid=%s path=%s", (const char*)FUUID.utf8(),
         (const char*)path.utf8());
    if (path.isEmpty())
        return 0;
    QDir dir(path, "*", QDir::Name | QDir::DirsFirst);
    if (dir.count() == 0) {
        olog("[getSize] uuid=%s -> 0 (dir empty)", (const char*)FUUID.utf8());
        return 0;
    }
    int r = AM_Global::getBlockSize(path) >> 10;   /* 字节→KB */
    olog("[getSize] uuid=%s -> %d", (const char*)FUUID.utf8(), r);
    return r;
}

/* ============ getPath ============ */
QString AM_AppObject::getPath() const
{
    ring_put(7, (unsigned)this, FTemp008);
    // 根据反编译：根据 Type 返回不同路径
    switch (FTemp008) {
    case Folder:
        // Folder 返回 null
        return QString::null;
    case Application:
        // Application: 基于 FDir 构建
        // 如果 FDir 以 "/" 开头，则直接返回 FDir + "/" + 后缀
        // 否则使用 getPreloadDir() 等
        if (!FDir.isEmpty()) {
            // 检查 FDir 是否以 "/" 开头
            if (FDir.left(1) == "/") {
                // 绝对路径
                return FDir;
            }
            // 相对路径：根据 FTemp00C (Device) 拼接
            switch (FTemp00C) {
            case 1:
                return AM_Global::getNativeDir() + FDir;
            case 2:
                return AM_Global::getPreloadDir() + "/" + FDir;
            case 3:
                return AM_Global::getPreloadDir() + "/" + FDir;
            case 4:
            case 5:
                return AM_Global::getInstalledQtDir() + FDir;
            default:
                return FDir;
            }
        }
        return QString::null;
    case EmbeddedLink:
    case Shortcut:
        // 返回 null
        return QString::null;
    default:
        return QString::null;
    }
}

/* ============ invoke (虚函数，基类默认) ============ */
void AM_AppObject::invoke(AM_LauncherInterface const&)
{
    ring_put(9, (unsigned)this, 0);
    // 基类默认空实现
}

/* ============ createObject (非虚，基类默认) ============ */
void AM_AppObject::createObject(QMap<QString, QString> const&)
{
    ring_put(10, (unsigned)this, 0);
    // 基类默认: 反编译显示返回 1（此处在 void 签名下无返回值，
    // 真实二进制返回 int=1，派生类各自实现）
}

/* ============ getAppLockEnabled (虚函数) ============ */
bool AM_AppObject::getAppLockEnabled() const
{
    ring_put(11, (unsigned)this, 0);
    // 基类默认: 根据反编译返回 0
    return false;
}

/* ============ setAppLockEnabled (虚函数) ============ */
void AM_AppObject::setAppLockEnabled(bool enabled)
{
    // 基类默认: 根据反编译直接返回参数
    (void)enabled;
}

/* ============ getAppLockValue (虚函数) ============ */
int AM_AppObject::getAppLockValue() const
{
    ring_put(12, (unsigned)this, 0);
    // 基类默认: 根据反编译返回 0
    return 0;
}

/* ============ setAppLockValue (虚函数) ============ */
void AM_AppObject::setAppLockValue(int value)
{
    // 基类默认: 根据反编译直接返回参数
    (void)value;
}

/* ============ dump (虚函数) ============ */
void AM_AppObject::dump()
{
    ring_put(13, (unsigned)this, 0);
    // 基类默认空实现
}

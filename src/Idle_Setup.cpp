/* Idle_Setup.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   唯一成员：SETUP_SET_Manager* @ 0x00。
   构造：getenv(EZX 配置环境变量) 拼 idle 配置路径 → new SETUP_SET_Manager(path, false)
   所有 get/set 方法：构造 SETUP_SET_Manager → getSettingInfo/setSettingInfo 读写对应节键。
   字符串常量为 PC 相对寻址（literal pool），此处以语义等价常量占位，
   忠实还原访问模式（节名 "dynamicIdle"/"shortcuts"/键 "shortcut_N[_freeze]" 等）。
*/
#include "Idle_Setup.h"

#include <qstring.h>
#include <quuid.h>
#include <SETUP_SET_Manager.h>
#include <SETUP_Utility.h>       /* getImsiChangedFlag（平台 UND，运行期解析） */
#include <stdlib.h>
#include <string.h>

/* TAPI_SIM_GetImsiExt 平台 API：原库 doAutoSelection 引用（UND，运行期解析）。
   其声明头 TAPI_SIM_Client.h 会引入 EZX_TypeDefs/TAPI_General 等平台头，
   在本机 cygwin 交叉编译环境可能因平台 typedef 冲突而不可 include。
   故此处按平台头原样给出 C 接口最小声明（IMSI_EXT_S 三字段 mcc/mnc/subscribeId，
   与 TAPI_SIM_Client.h 0x48-0x53 行一致），保证编译通过且运行期行为一致。 */
extern "C" {
typedef struct _TAPI_SIM_IMSI_EXT_S {
    unsigned char mcc[3];
    unsigned char mnc[3];
    unsigned char subscribeId[15];   /* TAPI_IMSI_LENGTH */
} TAPI_SIM_IMSI_EXT_S;
extern void TAPI_SIM_GetImsiExt(TAPI_SIM_IMSI_EXT_S* pImsiExt);
}

/* ============ 内部辅助 ============ */
/* 原库 Idle_Setup 构造 0x41b11254 实证：
     QString(getenv("EZX_APP_WRITE_PATH")) + QString("setup/ezx_idle.cfg")
   注意 "setup/ezx_idle.cfg" **不带前导斜杠**（EZX_APP_WRITE_PATH 自身以 '/' 结尾，
   设备上 = /ezx_user/download/appwrite/ → /ezx_user/download/appwrite/setup/ezx_idle.cfg）。
   旧实现写 "/usr/setup/ezx_idle.cfg" 且拼在 EZX_APP_WRITE_PATH 之后 → 文件不存在。 */
QString Idle_Setup::buildConfigPath(const char* suffix) const
{
    const char* env = getenv("EZX_APP_WRITE_PATH");
    QString base = env ? QString(env) : QString("");
    return base + QString(suffix);
}

SETUP_SET_Manager* Idle_Setup::openMgr(bool ro)
{
    return new SETUP_SET_Manager(buildConfigPath("setup/ezx_idle.cfg"), ro);
}

/* ============ 构造函数 ============ */
Idle_Setup::Idle_Setup()
{
    m_pSetMgr = new SETUP_SET_Manager(buildConfigPath("setup/ezx_idle.cfg"), false);
}

/* ============ 析构函数 ============ */
Idle_Setup::~Idle_Setup()
{
    delete m_pSetMgr;
}

/* ============ doAutoSelection ============ */
/* 反编译 0x41b11400（966 行）控制流还原：
   构造 SETUP_SET_Manager(path, false)，读 3 项配置：
     getSettingInfo(S, K_A, ok1, 1);          // ok1=连接配置状态
     getSettingInfo(S, K_B, local_71, 0);     // 中间状态
     getSettingInfo(S, K_C, val, K_C_def);    // 当前配置组名
   if (!ok1) {                                 // 未配置
     getSettingInfo(S2, K_D, val, K_D_def);
     if (!K_C_val && !force)                   // 无配置且非强制
       getSettingInfo(S2, K_E, val, K_E_def);  // 读默认组
   }
   getEntryKeyList(S3);                        // 枚举可用网络组
   for each entry:
     getSettingInfo(S3, entry, flag, 0);
     if (flag) {                               // 命中可用组
       param_2 = true;
       setSettingInfo(S3, K_F, 0/1/0/0);       // 重置选择状态
       setSettingInfo(S3, K_F, param_2);       // 写入选中结果
       break;
     }
   setSettingInfo(S, K_A, result);            // 回写最终结果
   flush();
   section/key 名为 PC 相对寻址 rodata（语义占位），控制流结构已还原。 */
/* v71：1:1 忠实重写 0x41b11400（966 行反编译）。
   ★ 关键更正：旧占位实现的节名/键名(AutoSelection/Available/Selected/AutoSelectResult)
     全是编造的，原库根本不存在这些键 —— 功能实际失效。本轮从 rodata 解出真实常量
     (DAT_41b122xx)，并锁定 getEntryKeyList 返回类型 = QMap<QString,QString>：
       - 真身 libezxappbase@0x41892984 返回槽构造逐字节对应 Qt2 qmap.h 的
         QMapPrivate{count,node_count,header} + QMapNode{data,key}；
       - ARM ABI 探针确认 r0=隐藏槽/r1=this/r2=QString，与原库调用点一致；
       - 旧"按 QStringList 用崩 @0x41b002d8"根因：QMapPrivate 被当 QValueListPrivate 析构。
     控制流、节名、键名、setSettingInfo 序列均严格对照反编译还原。 */
void Idle_Setup::doAutoSelection(bool& ok1, bool& ok2, bool force)
{
    /* ---- 真实 rodata 常量（DAT_41b122xx，已从原库解出）---- */
    const char* const S_CONN       = "ConnectPublicInfo";       // 12220
    const char* const K_AUTOSEL    = "AutoSelection";           // 12224
    const char* const K_AUTORES    = "AutoSelectResult";        // 12228
    const char* const K_CURPROFILE = "CurrentConnectProfileName";// 1222c
    const char* const V_DEFAULT_UUID = "68227C27-55BE-42AB-B6AB-29BB9178DFC4"; // 12230
    const char* const K_S3ITEM     = "S3ConnectionItem";        // 1223c  getEntryKeyList 实参
    const char* const K_HIDEFAIL   = "HideS3WhenAutoSelectFail";// 12244
    const char* const K_S3DEFAULT  = "S3DefaultMode";           // 12248
    const char* const K_ENABLES3   = "EnableS3FeatureByDefault";// 1224c
    const char* const K_DEFPROFILE = "DefaultProfile";          // 12258
    const char* const V_FAILGROUP  = "GroupNameAutoSelectionFailed"; // 1225c

    /* ---- 前置：IMSI 变化检测 + cp 刷新（反编译 0x41b11428-0x41b114f0）----
       真实命令串 = "cp /usr/setup/ezx_screen3.cfg " + getenv("EZX_APP_WRITE_PATH")
       （DAT_41b1226c + DAT_41b12260 "%1%2" + DAT_41b12264 EZX_APP_WRITE_PATH）。 */
    /* v69：堆分配空壳类，避免栈写穿（v68 事故） */
    SETUP_Utility* util = new SETUP_Utility();
    bool imsiChanged = (util->getImsiChangedFlag() != 0);
    delete util;
    if (imsiChanged) {
        QString envPath(getenv("EZX_APP_WRITE_PATH"));
        QString cmd = QString("cp /usr/setup/ezx_screen3.cfg ") + envPath;
        system(cmd.latin1());
    }

    /* ---- 构造 manager：path = "%1%2".arg(envPath).arg("/setup/ezx_screen3.cfg") ---- */
    QString envPath(getenv("EZX_APP_WRITE_PATH"));
    QString path = QString("%1%2").arg(envPath).arg("/setup/ezx_screen3.cfg");
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(path, false);
    if (!mgr) return;

    /* ---- 读三项配置（反编译 0x41b117xx）---- */
    mgr->getSettingInfo(S_CONN, K_AUTOSEL, ok1, true);
    bool midState = false;
    mgr->getSettingInfo(S_CONN, K_AUTORES, midState, false);
    QString curProfile;
    mgr->getSettingInfo(S_CONN, K_CURPROFILE, curProfile, V_DEFAULT_UUID, false);

    /* local_100 = 最终选中的配置组名（反编译 local_100） */
    QString resultGroup;

    if (!ok1) {
        /* 未配置分支（反编译 0x41b119xx / line 290-334）：
           if (curProfile != UUID) { resultGroup = curProfile; goto 收尾; }
           else resultGroup = getSettingInfo(S_CONN,"DefaultProfile", "", V_FAILGROUP); */
        if (curProfile != V_DEFAULT_UUID) {
            resultGroup = curProfile;
            goto FINALIZE;
        }
        mgr->getSettingInfo(S_CONN, K_DEFPROFILE, resultGroup, V_FAILGROUP, false);
    }
    else {
        /* 已配置分支（反编译 line 335-...）：
           if (!force && curProfile != UUID) {
               resultGroup = getSettingInfo(S_CONN, K_CURPROFILE, "", "");
               goto 清理后返回;   // 反编译此处走 LAB_41b129bc 直接收尾
           }
           否则：TAPI 取 IMSI → getEntryKeyList 枚举 → 遍历匹配 → 写 S3 标志。 */
        if (!force && curProfile != V_DEFAULT_UUID) {
            mgr->getSettingInfo(S_CONN, K_CURPROFILE, resultGroup, "", false);
            goto FINALIZE;
        }

        ok2 = false;
        TAPI_SIM_IMSI_EXT_S imsi;
        memset(&imsi, 0, sizeof(imsi));
        TAPI_SIM_GetImsiExt(&imsi);

        /* IMSI 串：mcc + "-" + mnc + subscribeId（反编译 QCString 拼接，
           DAT_41b12234="-"）。作为候选组名匹配的查找键。 */
        QCString mcc((const char*)imsi.mcc);
        QCString mnc((const char*)imsi.mnc);
        QCString sub((const char*)imsi.subscribeId);
        QString imsiKey = QString(mcc) + "-" + QString(mnc) + QString(sub);

        /* ★ getEntryKeyList("S3ConnectionItem") 返回 QMap<QString,QString>。
           遍历其 key，与 IMSI 串比较；命中 → resultGroup=该条目 value, ok2=true；
           未命中 → resultGroup=V_FAILGROUP。反编译 line 660-700 的树遍历即
           QMapIterator 的 COW detach + compare，用标准 iterator 等价表达。 */
        QMap<QString, QString> entries = mgr->getEntryKeyList(K_S3ITEM);
        bool matched = false;
        for (QMap<QString, QString>::ConstIterator it = entries.begin();
             it != entries.end() && !matched; ++it) {
            if (it.key().compare(imsiKey) == 0) {
                resultGroup = it.data();
                ok2 = true;
                matched = true;
            }
            (void)it; // 抑制未用告警（iterator 语义遍历）
        }
        if (!matched) {
            resultGroup = V_FAILGROUP;
        }

        /* 根据 resultGroup 的 HideS3WhenAutoSelectFail 标志写 S3 状态
           （反编译 line 714-817，setSettingInfo 序列按 0/1 忠实还原）。 */
        bool hideFlag = false;
        mgr->getSettingInfo(resultGroup, K_HIDEFAIL, hideFlag, false);
        if (!hideFlag) {
            mgr->setSettingInfo(resultGroup, K_S3DEFAULT, false);
            mgr->setSettingInfo(resultGroup, K_ENABLES3, false);
        }
        else {
            mgr->setSettingInfo(resultGroup, K_S3DEFAULT, true);
            mgr->setSettingInfo(resultGroup, K_ENABLES3, false);
        }

        /* if (ok2 != midState) setSettingInfo(S_CONN, K_AUTORES, ok2) — 反编译 817-830 */
        if (ok2 != midState) {
            mgr->setSettingInfo(S_CONN, K_AUTORES, ok2);
        }
    }

FINALIZE:
    /* 反编译 918：if (resultGroup != curProfile) setSettingInfo(S_CONN,K_CURPROFILE,resultGroup) */
    if (resultGroup != curProfile) {
        mgr->setSettingInfo(S_CONN, K_CURPROFILE, resultGroup, false);
    }

    delete mgr;
}

/* ============ S3 / OTA 家族（ezx_screen3.cfg）============
 * ★v66 更正：这一族此前**全部读写错文件**。
 *
 * 旧实现一律走 openMgr() → `EZX_APP_WRITE_PATH` + "setup/ezx_idle.cfg"、节名 "dynamicIdle"。
 * 原库实证（每个函数开头都是同一段模板代码，字面池 0xf70/0xf34/0xf74）：
 *
 *     path  = QString("%1%2").arg(getenv("EZX_APP_WRITE_PATH")).arg("/setup/ezx_screen3.cfg")
 *     mgr   = SETUP_SET_Manager(path, false)
 *     sect  = mgr.getSettingInfo("ConnectPublicInfo", "CurrentConnectProfileName", &s, "", false)
 *     value = mgr.getSettingInfo(sect, <键>, ...)          ← 节名就是**当前连接配置名**
 *
 * 少数几个直接用 "ConnectPublicInfo" 当节（见各函数注释）。
 * 也就是说「动态待机 / S3 / OTA」的开关存在**按网络配置分组的 ezx_screen3.cfg** 里，
 * 我们却写在 ezx_idle.cfg 的 [dynamicIdle] 里 —— 那个文件屏保不读、我们也不读，
 * 于是「改了不生效」，而且因为没有报错、没有崩溃，一直没暴露。
 *
 * 附带更正：`dynamicIdle` 这个节名本身就是误读，它来自 setDisableEnableStatus 的
 * 路径片段 "/dynamicidle/newsreaderaddress.cfg"（小写 d、是路径不是节名）。
 *
 * 另：这一族原库**都不调 flush()**（setSettingInfo 内部落盘），
 * 旧实现多出来的 flush 一并去掉，与调用序列对齐。 */
static QString screen3Path()
{
    const char* env = getenv("EZX_APP_WRITE_PATH");
    return QString(env ? env : "") + QString("/setup/ezx_screen3.cfg");
}

/* 读「当前连接配置名」；原库 5 个参数版本的第 5 个 bool 传的是 false */
static QString currentProfile(SETUP_SET_Manager* mgr)
{
    QString profile;
    mgr->getSettingInfo(QString("ConnectPublicInfo"),
                        QString("CurrentConnectProfileName"),
                        profile, QString(""), false);
    return profile;
}

/* ============ getS3Info ============ */
/* 0x41b12d80（1240B）：
     prof = CurrentConnectProfileName
     if (prof == "") return;                       ← 0x41b13064 比空串后直接返回，**不动输出参数**
     enabled = getSettingInfo(prof, "EnableS3FeatureByDefault", true)   ← 默认 true
     visible = getSettingInfo(prof, "S3DefaultMode",            false)  ← 默认 false
   旧实现读的是 S3DefaultMode/HideS3WhenAutoSelectFail（键也错）。 */
void Idle_Setup::getS3Info(bool& enabled, bool& visible)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    if (prof.isEmpty()) {
        delete mgr;
        return;
    }
    mgr->getSettingInfo(prof, QString("EnableS3FeatureByDefault"), enabled, true);
    mgr->getSettingInfo(prof, QString("S3DefaultMode"), visible, false);
    delete mgr;
}

/* ============ setS3Info ============ */
/* 0x41b13258（1264B）：prof 为空则返回；否则
     setSettingInfo(prof, "EnableS3FeatureByDefault", enabled)
     setSettingInfo(prof, "S3DefaultMode",            visible) */
void Idle_Setup::setS3Info(bool enabled, bool visible)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    if (prof.isEmpty()) {
        delete mgr;
        return;
    }
    mgr->setSettingInfo(prof, QString("EnableS3FeatureByDefault"), enabled);
    mgr->setSettingInfo(prof, QString("S3DefaultMode"), visible);
    delete mgr;
}

/* ============ setS3BaseURL ============ */
/* 0x41b13748（1152B）：prof 为空则返回；否则 setSettingInfo(prof, "MGURL", url, true) */
void Idle_Setup::setS3BaseURL(QString url)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    if (prof.isEmpty()) {
        delete mgr;
        return;
    }
    mgr->setSettingInfo(prof, QString("MGURL"), url, true);
    delete mgr;
}

/* ============ setS3AddNewURL ============ */
/* 0x41b13bc8（1152B）：prof 为空则返回；否则 setSettingInfo(prof, "AddNewChannelURL", url, true) */
void Idle_Setup::setS3AddNewURL(QString url)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    if (prof.isEmpty()) {
        delete mgr;
        return;
    }
    mgr->setSettingInfo(prof, QString("AddNewChannelURL"), url, true);
    delete mgr;
}

/* ============ setEnableS3Feature ============ */
/* 0x41b14048（1008B）：读 prof → setSettingInfo(prof, "EnableS3FeatureByDefault", enable)
   注意：这一支原库**没有**空 prof 判断（照写，节名为空自然落到无效组）。 */
void Idle_Setup::setEnableS3Feature(bool enable)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    mgr->setSettingInfo(prof, QString("EnableS3FeatureByDefault"), enable);
    delete mgr;
}

/* ============ getHideS3WhenAutoSelectFail ============ */
/* 0x41b14438（704B）：**不读 prof**，直接以 "ConnectPublicInfo" 为节：
     return getSettingInfo("ConnectPublicInfo", "HideS3WhenAutoSelectFail", false) */
bool Idle_Setup::getHideS3WhenAutoSelectFail()
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    bool v = false;
    mgr->getSettingInfo(QString("ConnectPublicInfo"),
                        QString("HideS3WhenAutoSelectFail"), v, false);
    delete mgr;
    return v;
}

/* ============ getOTAEnabled ============ */
/* 0x41b146f8（1000B）：prof = CurrentConnectProfileName；
     return getSettingInfo(prof, "OTAEnabled", false) */
bool Idle_Setup::getOTAEnabled()
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    bool v = false;
    mgr->getSettingInfo(prof, QString("OTAEnabled"), v, false);
    delete mgr;
    return v;
}

/* ============ getIgnoreOTAUrlUpdate ============ */
/* 0x41b14ae0（1004B）：与 getOTAEnabled **读的是同一个键**（prof / "OTAEnabled"），
   且 0x41b14e88 `mov r0,r4` 原样返回，没有取反。
   ⇒ 原库这两个函数行为完全相同（疑为代码复用/历史遗留）。
   旧实现读的是 "AddNewChannelURL"（错）。 */
bool Idle_Setup::getIgnoreOTAUrlUpdate()
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    QString prof = currentProfile(mgr);
    bool v = false;
    mgr->getSettingInfo(prof, QString("OTAEnabled"), v, false);
    delete mgr;
    return v;
}

/* ============ setDisableEnableStatus ============ */
/* 0x41b14ecc（696B）：**这一支既不用 SETUP_SET_Manager，也不用 EZX_APP_WRITE_PATH**：
     path = getenv("EZX_LOCAL_APP_WRITE_PATH") + "/dynamicidle/newsreaderaddress.cfg"
             （lit 0xf88 / 0xf8c，注意 dynamicidle 全小写）
     ZConfig cfg(path, false);
     cfg.writeEntry("MCMRGroup", "DisableEnableValue", disable)     ← 直接 ZConfig，bool 版
   旧实现写的是 ezx_idle.cfg 的 [dynamicIdle] "DisableEnableValue"。 */
void Idle_Setup::setDisableEnableStatus(bool disable)
{
    const char* env = getenv("EZX_LOCAL_APP_WRITE_PATH");
    QString path = QString(env ? env : "") + QString("/dynamicidle/newsreaderaddress.cfg");
    ZConfig cfg(path, false);
    cfg.writeEntry(QString("MCMRGroup"), QString("DisableEnableValue"), disable);
}

/* ============ getS3KillStatus ============ */
/* 0x41b15184（700B）：节 = "ConnectPublicInfo"（不读 prof），键 "S3KillStatus"，默认 false */
bool Idle_Setup::getS3KillStatus()
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    bool v = false;
    mgr->getSettingInfo(QString("ConnectPublicInfo"), QString("S3KillStatus"), v, false);
    delete mgr;
    return v;
}

/* ============ setS3KillStatus ============ */
/* 0x41b15440（696B）：节 = "ConnectPublicInfo"，键 "S3KillStatus" */
void Idle_Setup::setS3KillStatus(bool kill)
{
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(screen3Path(), false);
    mgr->setSettingInfo(QString("ConnectPublicInfo"), QString("S3KillStatus"), kill);
    delete mgr;
}

/* ============ setItemAvailable ============ */
/* 原库 0x41b156f8 实证（只处理 0/1/2，其余返回 0；并且**不 flush**）：
     case 0 → setSettingInfo("dynamic", "dynamicIdle", v)
     case 1 → setSettingInfo("static",  "nextEvent",  v)
     case 2 → setSettingInfo("static",  "shortcuts",  v)
   返回 setSettingInfo 的返回值(int)。 */
int Idle_Setup::setItemAvailable(set_item_t item, bool available)
{
    if (!m_pSetMgr) return 0;
    switch (item) {
    case ITEM_DYNAMIC:
        return m_pSetMgr->setSettingInfo("dynamic", "dynamicIdle", available);
    case ITEM_NEXT_EVENT:
        return m_pSetMgr->setSettingInfo("static", "nextEvent", available);
    case ITEM_SHORTCUTS:
        return m_pSetMgr->setSettingInfo("static", "shortcuts", available);
    default:
        return 0;
    }
}

/* ============ isItemAvailable ============ */
/* 原库 0x41b1588c 实证：
     bool v = false;
     case 0 → getSettingInfo("dynamic","dynamicIdle", v, true)
     case 1 → getSettingInfo("static", "nextEvent",  v, true)
     case 2 → getSettingInfo("static", "shortcuts",  v, true)
     default → return false          ← 旧实现此处 return true，是错的
     return v; */
bool Idle_Setup::isItemAvailable(set_item_t item)
{
    if (!m_pSetMgr) return false;
    bool v = false;
    switch (item) {
    case ITEM_DYNAMIC:
        m_pSetMgr->getSettingInfo("dynamic", "dynamicIdle", v, true);
        break;
    case ITEM_NEXT_EVENT:
        m_pSetMgr->getSettingInfo("static", "nextEvent", v, true);
        break;
    case ITEM_SHORTCUTS:
        m_pSetMgr->getSettingInfo("static", "shortcuts", v, true);
        break;
    default:
        return false;
    }
    return v;
}

/* ============ 桌面 4 个快捷方式的出厂默认 UUID ============ */
/* 原库 getShortcutItem @0x41b15ccc 在栈上逐字节构造 16 字节 QUuid 再
   QUuid::toString() 作为默认值（等价于内联字面量）：
     shortcut_1 = af8770bf-6db6-4b29-a70b-9de205add992
     shortcut_2 = 95ffa462-7e2f-49a9-9e6d-f8f63b2f3c40   ← web_access（浏览器）
     shortcut_3 = ba63e2ca-089c-4a95-8e13-81522eed47a0
     shortcut_4 = ce59717d-fb23-4b7c-8800-25ce613f4042   ← RecentCall（通话记录）
   设备 /usr/setup/ezx_idle.cfg 的 [static] 节实测（CMCC 出厂）：
     shortcut_1 = fc4eee73-2b5b-4ebd-b6dc-32dd62fbd490  icon Home_CMCC_Service_B.gif
     shortcut_2 = 95ffa462-7e2f-49a9-9e6d-f8f63b2f3c40  icon web_access_std.g
     shortcut_3 = ba63e2ca-089c-4a95-8e13-81522eed47a0  icon media_finder_std.g
     shortcut_4 = ce59717d-fb23-4b7c-8800-25ce613f4042  icon recnt_call_std.g
   只有 shortcut_1 与库内默认值不同（出厂时被替换成"移动服务"），
   其余三项与库内默认完全一致 —— 反证上面的解码正确。 */
static void defaultShortcutUuid(int index, QUuid& out)
{
    switch (index) {
    case 0: out = QUuid(0xaf8770bful, 0x6db6, 0x4b29,
                        0xa7, 0x0b, 0x9d, 0xe2, 0x05, 0xad, 0xd9, 0x92); break;
    case 1: out = QUuid(0x95ffa462ul, 0x7e2f, 0x49a9,
                        0x9e, 0x6d, 0xf8, 0xf6, 0x3b, 0x2f, 0x3c, 0x40); break;
    case 2: out = QUuid(0xba63e2caul, 0x089c, 0x4a95,
                        0x8e, 0x13, 0x81, 0x52, 0x2e, 0xed, 0x47, 0xa0); break;
    default: out = QUuid(0xce59717dul, 0xfb23, 0x4b7c,
                         0x88, 0x00, 0x25, 0xce, 0x61, 0x3f, 0x40, 0x42); break;
    }
}

/* ============ setShortcut ============ */
/* 原库 0x41b15a5c：switch(index) 0..3 → 节 "static"、键 "shortcut_{index+1}"
   → setSettingInfo(sec, key, value, false)。index>3 直接 return 0。无 flush。 */
void Idle_Setup::setShortcut(int index, QString name)
{
    if (!m_pSetMgr) return;
    if (index < 0 || index > 3) return;
    QString key = QString("shortcut_%1").arg(index + 1);
    m_pSetMgr->setSettingInfo("static", key, name, false);
}

/* ============ getShortcut ============ */
/* 原库 0x41b15cb4（24 字节）：`ldr r1,[r1]` 取 m_pSetMgr → 尾调 getShortcutItem。
   即 `return getShortcutItem(*m_pSetMgr, index);` */
QString Idle_Setup::getShortcut(int index)
{
    if (!m_pSetMgr) return QString::null;
    return getShortcutItem(*m_pSetMgr, index);
}

/* ============ getShortcutItem ============ */
/* 原库 0x41b15ccc（static，本节名/键名/默认值全部逐字节实证）：
     if (index < 0 || index > 3) return QString::null;
     QString v; mgr.getSettingInfo("static", "shortcut_{index+1}", v,
                                  QUuid(defaults[index]).toString(), false);
     return v;
   旧实现用节 "shortcuts" + 键 "shortcut_{index}" + 默认 QString::null
   → 三项全错，读不到任何值，桌面 4 个快捷方式因此全部为空（不显示）。 */
QString Idle_Setup::getShortcutItem(SETUP_SET_Manager& mgr, int index)
{
    if (index < 0 || index > 3)
        return QString::null;
    QUuid def;
    defaultShortcutUuid(index, def);
    QString key = QString("shortcut_%1").arg(index + 1);
    QString val;
    mgr.getSettingInfo("static", key, val, def.toString(), false);
    return val;
}

/* ============ getDefaultShortcut ============ */
/* 原库 0x41b160f0（static）：用**只读出厂文件** /usr/setup/ezx_idle.cfg 建
   SETUP_SET_Manager → getShortcutItem(mgr, index)。 */
QString Idle_Setup::getDefaultShortcut(int index)
{
    /* v69：★这里原来是栈对象，和 v68 让 am 崩溃循环的是同一种写法。
       am 启动时不走这条路径（所以没当场炸），一旦打开待机屏/屏保就会踩到。 */
    SETUP_SET_Manager* mgr = new SETUP_SET_Manager(QString("/usr/setup/ezx_idle.cfg"), false);
    QString r = getShortcutItem(*mgr, index);
    delete mgr;
    return r;
}

/* ============ setShortcutFreeze ============ */
/* 原库 0x41b16194：节 "static"、键 "shortcut_{index+1}_freeze"。 */
void Idle_Setup::setShortcutFreeze(int index, bool freeze)
{
    if (!m_pSetMgr) return;
    if (index < 0 || index > 3) return;
    QString key = QString("shortcut_%1_freeze").arg(index + 1);
    m_pSetMgr->setSettingInfo("static", key, freeze);
}

/* ============ isShortcutFreeze ============ */
/* 原库 0x41b16334：节 "static"、键 "shortcut_{index+1}_freeze"。 */
bool Idle_Setup::isShortcutFreeze(int index)
{
    if (!m_pSetMgr) return false;
    if (index < 0 || index > 3) return false;
    QString key = QString("shortcut_%1_freeze").arg(index + 1);
    bool v = false;
    m_pSetMgr->getSettingInfo("static", key, v, false);
    return v;
}

/* ============ flush ============ */
bool Idle_Setup::flush()
{
    /* 反编译：调用 m_pSetMgr->flush()（虚表 slot flush） */
    return m_pSetMgr ? m_pSetMgr->flush() : false;
}

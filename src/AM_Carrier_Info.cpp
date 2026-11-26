/* AM_Carrier_Info.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   继承 QObject；成员：m_strName@0x24, m_pMsgClient@0x28(PHONE_MsgClient*), m_infoString@0x2C
   依赖外部：UTIL_GetRFMode / UTIL_GetNetworkStatus / UTIL_GetSpnDisplayedStatus /
             UTIL_GetOrgSpnName / UTIL_GetServiceName / PHONE_MsgClient
   这些外部符号由 libezxphone / libezxtapi 等提供，此处保留调用骨架。
*/
#include "AM_Carrier_Info.h"

#include <qstring.h>
#include <qobject.h>

/* 外部 TAPI/PHONE 辅助函数（由 libezxtapi / libezxphone 提供） */
extern "C" {
int   UTIL_GetRFMode(void);
int   UTIL_GetNetworkStatus(void);
int   UTIL_GetSpnDisplayedStatus(void);
void  UTIL_GetOrgSpnName(char*);
void  UTIL_GetServiceName(char*);
}

/* PHONE_MsgClient 的真实实现由 libezxphone.so.1 提供，本处只借用它的构造函数
   （C1EP7QObjectPKc），实体本身由那个库创建。

   **尺寸必须与真实类一致 = 40 字节**：
     原库构造函数 0x41b0e9a4 `mov r0,#40` → operator new(40)
     我们上一版空壳类 sizeof = sizeof(QObject) = 36，实测编译产物是
     0x41af4aa0 `mov r0,#36` —— 少 4 字节，真实构造函数会写到堆块之外，
     属于 4 字节堆越界（典型后果是破坏下一个堆块头，偶发崩溃）。

   Qt2 的 QObject 是 36 字节，这里补 4 字节填充凑到 40；下面的数组 typedef
   做编译期校验，尺寸一旦不符直接编译失败，不会再悄悄退步。 */
class PHONE_MsgClient : public QObject
{
public:
    PHONE_MsgClient(QObject* parent, const char* name);
private:
    char m_alignToRealSize[4];      /* 36 + 4 = 40 */
};
typedef char PHONE_MsgClient_size_must_be_40[
    sizeof(PHONE_MsgClient) == 40 ? 1 : -1 ];

/* ============ 构造函数 ============ */
AM_Carrier_Info::AM_Carrier_Info(QObject* parent)
    : QObject(parent)
{
    m_strName = QString::null;
    m_infoString = QString::null;

    /* new PHONE_MsgClient 并连接 5 个信号（反编译 0x41b0e9a4~0x41b0ea3c） */
    m_pMsgClient = new PHONE_MsgClient(this, 0);

    /* v62：这 5 条 connect 此前是注释，等于"构造时算一次，之后永不刷新"。
       电话功能开/关切换后状态栏仍停在「电话功能关」，根因就在这里。

       原库 5 条 connect 的信号串（GOT 槽 0xeb8/0xebc/0xec4/0xec8/0xecc，
       均由 pltdbg.py 解析出真实字符串）：
         signalServiceName(QString)                    ← 0x41b0e9d8
         signalSpnNameExtend(SERVICE_PROVIDER_DATA_S)  ← 0x41b0e9f4
         signalNetworkStatus(TAPI_NETWORK_STATUS_E)    ← 0x41b0ea0c
         signalServiceAvailability(TAPI_BOOL_STATUS_E) ← 0x41b0ea24
         signalSetRFModResult(bool)                    ← 0x41b0ea3c
       接收方槽统一为 slotUpdateInfoString()（GOT 槽 0xeb4）。
       注：原库第 2 条传的是不带括号的 "1slotUpdateInfoString"，属于原厂源码
       的小笔误；Qt 规范化后等价，此处统一写带括号的标准形式。 */
    QObject::connect(m_pMsgClient, SIGNAL(signalServiceName(QString)),
                     this, SLOT(slotUpdateInfoString()));
    QObject::connect(m_pMsgClient, SIGNAL(signalSpnNameExtend(SERVICE_PROVIDER_DATA_S)),
                     this, SLOT(slotUpdateInfoString()));
    QObject::connect(m_pMsgClient, SIGNAL(signalNetworkStatus(TAPI_NETWORK_STATUS_E)),
                     this, SLOT(slotUpdateInfoString()));
    QObject::connect(m_pMsgClient, SIGNAL(signalServiceAvailability(TAPI_BOOL_STATUS_E)),
                     this, SLOT(slotUpdateInfoString()));
    QObject::connect(m_pMsgClient, SIGNAL(signalSetRFModResult(bool)),
                     this, SLOT(slotUpdateInfoString()));

    /* 0x41b0ea44：构造完立刻算一次初值 */
    slotUpdateInfoString();
}

/* ============ 析构函数 ============ */
AM_Carrier_Info::~AM_Carrier_Info()
{
    /* QObject 基类析构负责子对象清理 */
}

/* ============ setIcons ============ */
void AM_Carrier_Info::setIcons()
{
    /* 反编译返回 1（此版本空实现） */
}

/* ============ updateInfoString ============ */
/* 反编译：m_infoString = str; emit signalStrUpdate(m_infoString); */
void AM_Carrier_Info::updateInfoString(QString str)
{
    m_infoString = str;
    emit signalStrUpdate(m_infoString);
}

/* ============ getInfoString ============ */
/* 反编译：返回 m_infoString 拷贝 */
QString AM_Carrier_Info::getInfoString()
{
    return m_infoString;
}

/* ============ slotUpdateInfoString ============ */
/* 反编译 0x41b0ebf8，v61 逐句还原（旧版把 7 个分支的字符串几乎全写成 "No Service"，
   并且误用 QObject::tr —— 原库用的是 **AM_Carrier_Info::tr**，上下文不同，
   翻译表里查不到，导致桌面状态栏恒显示英文 "No Service" 而不是"电话功能关"）。

   实测 GOT 槽 → 字符串（gotslot.py）：
     0x1138 QString::null
     0xefc/0xf00  TXT_RID_CONTENT_SLINE_ANTENNA_OFF  "Antenna Off"   ← RF 关闭，中文"电话功能关"
     0xed0/0xed4  TXT_RID_CONTENT_SLINE_SEARCHING    "Searching..."  ← status 1
     0xed8/0xedc  TXT_RID_CONTENT_SLINE_REGISTERING  "Registering..."← status 2
     0xee0/0xee4  TXT_RID_LABEL_NO_SERVICE           "No Service"    ← status 4
     0xee8/0xeec  TXT_RID_CBUTTON_NETWORKBUSY        "NetworkBusy"   ← status 5
     0xef0/0xef4  TXT_RID_LABEL_EMERGENCY_ONLY       "Emergency Only"← status 6
     0xef8        " "（裸 QString(const char*)，非 tr）              ← status 7
   调用点 0x41ae88e0 解析为 _ZN15AM_Carrier_Info2trEPKcS1_（不是 QObject::tr）。 */
void AM_Carrier_Info::slotUpdateInfoString()
{
    /* 0x41b0ec0c：str = QString::null（不是 "No Service"） */
    QString str = QString::null;

    /* 0x41b0ec20：RF 关闭 → 早退 */
    if (UTIL_GetRFMode() == 1) {
        str = tr("TXT_RID_CONTENT_SLINE_ANTENNA_OFF", "Antenna Off");
        updateInfoString(str);
        return;
    }

    /* 0x41b0ec2c：switch(UTIL_GetNetworkStatus() - 1)，0..6 走跳表，其余不更新 */
    switch (UTIL_GetNetworkStatus() - 1) {
    case 0:                                   /* 0x41b0ec5c */
        str = tr("TXT_RID_CONTENT_SLINE_SEARCHING", "Searching...");
        break;
    case 1:                                   /* 0x41b0ecf4 */
        str = tr("TXT_RID_CONTENT_SLINE_REGISTERING", "Registering...");
        break;
    case 2:                                   /* 0x41b0ed00 已注册：SPN / 服务名 */
        {
            char buf[64];
            buf[0] = '\0';
            /* 0x41b0ed04：UTIL_GetSpnDisplayedStatus() != 0 → GetServiceName
                           否则（==0）        → GetOrgSpnName      */
            if (UTIL_GetSpnDisplayedStatus() != 0)
                UTIL_GetServiceName(buf);
            else
                UTIL_GetOrgSpnName(buf);
            str = QString::fromUtf8(buf, -1);   /* 0x41ae918c QString::fromUtf8(char*,int) */
        }
        break;
    case 3:                                   /* 0x41b0ede0 */
        str = tr("TXT_RID_LABEL_NO_SERVICE", "No Service");
        break;
    case 4:                                   /* 0x41b0edec */
        str = tr("TXT_RID_CBUTTON_NETWORKBUSY", "NetworkBusy");
        break;
    case 5:                                   /* 0x41b0edf8 */
        str = tr("TXT_RID_LABEL_EMERGENCY_ONLY", "Emergency Only");
        break;
    case 6:                                   /* 0x41b0ee04：裸 " "，非 tr */
        str = QString(" ");
        break;
    default:                                  /* 0x41b0ecb8：只析构，不 update */
        return;
    }

    updateInfoString(str);
}

/* ============ signalStrUpdate（Qt 信号实现桩） ============ */
/* moc 生成的信号实现；此处仅给出签名以保持可编译，实际由 moc 提供。*/
#include "AM_Carrier_Info.moc"

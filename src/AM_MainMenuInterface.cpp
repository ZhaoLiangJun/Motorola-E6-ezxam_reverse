/* AM_MainMenuInterface.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   ShowMainMenu：通过 QCopChannel 向主菜单应用发送消息。
   反编译：
     channel = <rodata 字符串>            // QCop 通道名（主菜单通道）
     if (QCopChannel::isRegistered(channel))
         QCopChannel::send(channel, msg); // msg = <rodata 消息>
     return channel是否已注册;
*/
#include "AM_MainMenuInterface.h"

#include <qstring.h>
#include <qcstring.h>
#include <qcopchannel_qws.h>

/* ============ 构造函数 ============ */
AM_MainMenuInterface::AM_MainMenuInterface()
{
}

/* ============ 析构函数 ============ */
AM_MainMenuInterface::~AM_MainMenuInterface()
{
}

/* ============ ShowMainMenu ============ */
bool AM_MainMenuInterface::ShowMainMenu()
{
    /* 反编译：通道名与消息来自 rodata（主菜单 QCop 通道）。
       已从 .rodata 提取精确常量：
         通道 0x41b1b2e0 = "AM_MainMenuChannel"
         消息 0x41b1b2f8 = "SHOWMAINMENU" */
    const char* channel = "AM_MainMenuChannel";
    const char* message = "SHOWMAINMENU";

    QCString ch(channel);
    bool registered = QCopChannel::isRegistered(ch);
    if (registered) {
        QCopChannel::send(QCString(channel), QCString(message));
    }
    return registered;
}

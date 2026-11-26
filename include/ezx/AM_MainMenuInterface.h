/*
libezxam.so.1.0.0  逆向重建
ShowMainMenu：QCopChannel::isRegistered(channel) 检查，已注册则 send(msg)。
返回值：bool（反编译为 bool，原始 genheader 为 void，取 bool 语义）
*/
#ifndef AM_MAINMENUINTERFACE
#define AM_MAINMENUINTERFACE

class AM_MainMenuInterface
{
public :
  AM_MainMenuInterface();
  ~AM_MainMenuInterface();

  bool ShowMainMenu();
};

#endif

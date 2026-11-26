/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译）
AM_Carrier_Info : public QObject（Q_OBJECT 信号槽）
成员：m_strName @ 0x24(QString)、m_pMsgClient @ 0x28(PHONE_MsgClient*)、m_infoString @ 0x2C(QString)
构造：QObject(parent) → 初始化 2 个 QString → new PHONE_MsgClient(0x28) → 5 个 connect → slotUpdateInfoString()
虚表证据：_ZTV15AM_Carrier_Info @ 0x41b22c10
*/
#ifndef AM_CARRIER
#define AM_CARRIER

#include <qobject.h>
#include <qstring.h>

class PHONE_MsgClient;

class AM_Carrier_Info : public QObject
{
    Q_OBJECT
public :
  AM_Carrier_Info(QObject* parent);
  ~AM_Carrier_Info();

  virtual void setIcons();   /* 原库虚表 slot[17]（QObject 虚槽之后）已证实为虚函数 */
  void updateInfoString(QString);
  QString getInfoString();

public slots:
  /* v32：原库元对象槽表 1 项 slotUpdateInfoString()
     （_ZN15AM_Carrier_Info16staticMetaObjectEv @0x41b17d08）。 */
  void slotUpdateInfoString();

signals:
  void signalStrUpdate(QString);   /* Qt 信号（updateInfoString 中 emit）*/

private :
  QString m_strName;               /* 0x24 */
  PHONE_MsgClient* m_pMsgClient;   /* 0x28 */
  QString m_infoString;            /* 0x2C */
};

#endif  /* AM_CARRIER */

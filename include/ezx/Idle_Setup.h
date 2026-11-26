/*
libezxam.so.1.0.0  逆向重建（Idle_Setup — 待机屏/S3 快捷方式配置）
唯一成员：SETUP_SET_Manager* @ 0x00（继承 ZConfig→Q_OBJECT 链）
构造：getenv(环境变量) 拼 idle 配置路径 → new SETUP_SET_Manager(path, false)
所有 get/set 方法：构造 SETUP_SET_Manager → getSettingInfo/setSettingInfo 读写对应节键
方法清单（21 个公共方法 + 构析）：
  构造/析构, doAutoSelection(bool&,bool&,bool),
  getS3Info(bool&,bool&), setS3Info(bool,bool),
  setS3BaseURL(QString), setS3AddNewURL(QString),
  setEnableS3Feature(bool), getHideS3WhenAutoSelectFail(),
  getOTAEnabled(), getIgnoreOTAUrlUpdate(),
  setDisableEnableStatus(bool),
  getS3KillStatus(), setS3KillStatus(bool),
  setItemAvailable(set_item_t,bool), isItemAvailable(set_item_t),
  setShortcut(int,QString), getShortcut(int),
  getShortcutItem(SETUP_SET_Manager&,int),
  getDefaultShortcut(int),
  setShortcutFreeze(int,bool), isShortcutFreeze(int),
  flush()
注意：set_item_t 为 Idle 屏 item 枚举（头文件未随 genheader 导出，此处定义占位）。
*/
#ifndef _IDLE_SETUP_H_
#define _IDLE_SETUP_H_

#include <qobject.h>
#include <qstring.h>
#include <SETUP_SET_Manager.h>

/* Idle 屏可配置项枚举（原库 setItemAvailable/isItemAvailable 实测只处理 0/1/2，
   对应 ezx_idle.cfg 的 three 个键：
     0 → [dynamic] / dynamicIdle
     1 → [static]  / nextEvent
     2 → [static]  / shortcuts
   其它值两个函数都直接返回 0。 */
enum set_item_t
{
    ITEM_DYNAMIC     = 0,   /* [dynamic] dynamicIdle */
    ITEM_NEXT_EVENT  = 1,   /* [static]  nextEvent   */
    ITEM_SHORTCUTS   = 2    /* [static]  shortcuts   */
};

class Idle_Setup
{
public:
    Idle_Setup();
    ~Idle_Setup();

    void  doAutoSelection(bool& ok1, bool& ok2, bool force);
    void  getS3Info(bool& enabled, bool& visible);
    void  setS3Info(bool enabled, bool visible);
    void  setS3BaseURL(QString url);        /* 原版: 值传参 QString */
    void  setS3AddNewURL(QString url);      /* 原版: 值传参 QString */
    void  setEnableS3Feature(bool enable);
    bool  getHideS3WhenAutoSelectFail();
    bool  getOTAEnabled();
    bool  getIgnoreOTAUrlUpdate();
    void  setDisableEnableStatus(bool disable);
    bool  getS3KillStatus();
    void  setS3KillStatus(bool kill);
    int   setItemAvailable(set_item_t item, bool available);
    bool  isItemAvailable(set_item_t item);
    void  setShortcut(int index, QString name);   /* 原版: 值传参 QString */
    QString getShortcut(int index);
    /* 原库 ABI 实证（关键，勿改回非静态）：
         getShortcutItem @0x41b15ccc  `_ZN10Idle_Setup15getShortcutItemER17SETUP_SET_Manageri`
              调用点 0x41b15cc0 只装 (r0=sret, r1=&mgr, r2=index) —— 没有 this，
              故为 **static**：static QString getShortcutItem(SETUP_SET_Manager&, int)。
         getDefaultShortcut @0x41b160f0
              am 调用点 0x622c0: `mov r0,r9; mov r1,r4; bl` → (r0=sret, r1=index)，
              同样没有 this → **static**。
         若按非静态实现（this 在 r1），am 会把 index(0..3) 当作 this 解引用 → 崩溃。 */
    static QString getShortcutItem(SETUP_SET_Manager& mgr, int index);
    static QString getDefaultShortcut(int index);
    void  setShortcutFreeze(int index, bool freeze);
    bool  isShortcutFreeze(int index);
    bool  flush();

private:
    SETUP_SET_Manager* m_pSetMgr;   /* 0x00 唯一成员（am 用 operator new(4)） */

    /* 内部辅助：按 getenv 构造配置文件路径 */
    QString buildConfigPath(const char* suffix) const;
    /* 内部辅助：新建 SETUP_SET_Manager */
    SETUP_SET_Manager* openMgr(bool ro);
};

#endif /* _IDLE_SETUP_H_ */

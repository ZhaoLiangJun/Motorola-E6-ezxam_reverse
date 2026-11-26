/*
  libezxam.so.1.0.0
*/
#ifndef AM_APPOBJECT
#define AM_APPOBJECT

#include <qmap.h>
#include <qpixmap.h>
#include <qstring.h>
#include <quuid.h>
#include <AM_LauncherInterface.h>

class AM_AppObject
{
public :
	enum Type
    {
        InvalidType = 0,
        Folder = 1,
        Application = 2,
        EmbeddedLink = 3,
        Shortcut = 4
    };
    enum IconType { Big, Small, Ani };
public :
  AM_AppObject();
  AM_AppObject(AM_AppObject::Type);
  virtual ~AM_AppObject();   /* 虚析构：虚表 slot[2]=D1(完整)/slot[3]=D0(删除)，原库已证实 */

  void init();
  QString getName() const;
  QString getDefaultName();
  void setName(QString const&);
  void getBigIcon(QPixmap&) const;
  void getIcon(QPixmap&, AM_AppObject::IconType) const;
  void getSmallIcon(QPixmap&) const;
  void getAnimationIcon(QPixmap&) const;
  /* 原库 0x41aef308 `_ZNK12AM_AppObject10getBigIconEv`（1624B，9 寄存器压栈 + `mov r6,r1`）
     → 返回值是 QPixmap（sret 隐藏返回槽），不是指针。
     旧声明 `QPixmap* getBigIcon() const` 的 mangled 名相同（返回类型不入名），
     但 ABI 不同：调用方按 sret 传 r0，我们却当成 4 字节指针返回 → 破坏栈/寄存器。
     目前 am 只导入 `getBigIcon(QPixmap&) const`，故未爆发，但必须按原库改回。 */
  QPixmap getBigIcon() const;
  /* 原库 0x41aef960（24B：`add r1,r1,#0x3c` + QString 拷贝构造 + 返回 r4）
     → `QString getVender()`，返回 FVender(0x3C) 的拷贝，sret 传参。
     旧声明 `void getVender()` 会让调用方拿不到返回槽 → 使用未初始化的 QPixmap/QString。 */
  QString getVender();
  int getSize();
  QString getPath() const;
  /* invoke/createObject 是虚函数（原库虚表 slot[4]/slot[5] 已证实）：
     子类 AppLnk/Folder/Shortcut/EmbeddedLink 各自覆盖 createObject，
     Folder/EmbeddedLink/Shortcut 覆盖 invoke。声明为 virtual 保证基类指针
     虚调用能派发到子类实现，并与原库虚表布局一致。 */
  virtual void invoke(AM_LauncherInterface const&);
  virtual void createObject(QMap<QString, QString> const&);
  /* 以下 5 个为虚函数，对应虚表 slot[6..10] */
  virtual bool getAppLockEnabled() const;
  virtual void setAppLockEnabled(bool);
  virtual int getAppLockValue() const;
  virtual void setAppLockValue(int);
  virtual void dump();

  const QString& UUID(){return FUUID;}
  void setUUID(const QString& u){ FUUID = u; }   /* loadMenuTree 创建根 Folder 时用 UUID 作键 */
  const QString& Dir(){return FDir;}
  Type getType() const { return static_cast<Type>(FTemp008); }
  /* FTemp044(0x44) 是设备来源字段：init()=1(内置)，handleCardPlugIn 设卡应用=2，
     slotAppInstalled 写 param_2(Device)，handleCardPlugOut 判 ==2 卸载卡应用。 */
  int  getDeviceSource() const { return FTemp044; }
  void setDeviceSource(int d)  { FTemp044 = d; }
  /* 0x0C 设备槽位（v12 语义修正）：getInstalledAppList 过滤 0x0C ∈ {4,5}
     （反编译 0x41b064b4），由 RegistryManager::createObject 按注册表来源设置：
     内置(SysRegistry)=4，用户/卡(UserRegistry/CardRegistry)=5。 */
  int  getDeviceSlot() const   { return FTemp00C; }
  void setDeviceSlot(int d)    { FTemp00C = d; }
  /* FTemp040(0x40) 是"可见/已安装"标志：slotAppInstalled 设 true，
     handleCardPlugIn 用 getAllAppList 过滤 *(obj+0x40)=='\0' 时跳过。 */
  bool isVisibleFlag() const    { return FTemp040; }
  void setVisibleFlag(bool v)   { FTemp040 = v; }
  /* FTemp004(0x04) 是父文件夹指针（v16）：原库 loadMenuTree 0x41afa684
     挂树时 *(AM_Folder**)(obj+4) = folder（folder != 根时）。 */
  void setParentFolder(void* f) { FTemp004 = (int)f; }
  void* parentFolder() const    { return (void*)FTemp004; }
  void setPath(const QString& p)   { FDir = p; }
  const QUuid& appId() const { return FAppID; }
  void setAppId(const QUuid& u)    { FAppID = u; }
  bool isFixedPosition() const     { return (FTemp030 & 0xFF) != 0; }
  void setFixedPosition(bool v)    { FTemp030 = v ? (FTemp030 | 0x01) : (FTemp030 & ~0x01); }
  bool isFixedAllPosition() const  { return ((FTemp030 >> 8) & 0xFF) != 0; }
  void setFixedAllPosition(bool v) { FTemp030 = v ? (FTemp030 | 0x0100) : (FTemp030 & ~0x0100); }
  void setBigIcon(const QString& i) { FBigIcon = i; }
  void setSmallIcon(const QString& i){ FSmallIcon = i; }
  void setAniIcon(const QString& i)  { FAniIcon = i; }
  /* 注册表里的图标**路径字符串**（0x10 / 0x18）。
     注意与 getBigIcon() 区分：后者原库里是"按 Type 拼候选路径 + QFileInfo::isFile
     探测"的解析函数（1624B，内部 60 个调用全是 QString/QFileInfo，**一次
     QPixmap 都没有**），即它返回的其实是 QString 路径，不是 QPixmap。
     本项目当前把它声明成 QPixmap（ABI 同宽所以没崩），属已知未修项，
     故这里另开一对纯字段访问器，供 modifyApp 之类"只比字符串"的地方使用。 */
  const QString& bigIconPath() const { return FBigIcon; }
  const QString& aniIconPath() const { return FAniIcon; }
  /* 0x50：派生类首字段（AM_AppLnk::m_pad50 / Folder 的填充）。基类 sizeof=0x50，
     所以 0x50 属于**派生区**；工厂创建的对象最小 0x58，写 1 字节不会越界。
     原库 AM_RegistryManager::modifyApp 用 `strb r,#1/0` 写这里（0x41afde08
     / 0x41afe020），语义 = "名字被用户改过（modification）"，与配置项
     "modification = YES/NO" 同值。 */
  void setModificationFlag(bool v)   { *(char*)((char*)this + 0x50) = v ? 1 : 0; }
  bool modificationFlag() const      { return *(const char*)((const char*)this + 0x50) != 0; }
  /* 0x40/0x41/0x42 三个 bool：原库 modifyApp 写完配置后置 0x41（名字变）
     /0x42（图标变），loadMenuTree 装载时清 0。AM_RegistryManager 不是本类
     的派生类，碰不到 protected 成员，故开这两个语义化的 public setter。 */
  void setChangedName(bool v)        { FTemp041 = v; }
  void setChangedIcon(bool v)        { FTemp042 = v; }
protected:
    int FTemp004;       // $4
    int FTemp008;       // $8 //AM_AppObject::Type
    int FTemp00C;       // $C
    QString FBigIcon;       // $10 //Home_NotePad_B.gif
    QString FSmallIcon;       // $14 //Home_NotePad_S.gif
    QString FAniIcon;       // $18 //Home_NotePad_ani.gif
    QString FUUID;       // $1C //c30158ae-e2a7-4d1c-9f6c-3b079c2f4c4e
    /* $20..$2F：原库 `AM_AppLnk::createObject @0x41af08b8-0x41af08e8` 实证——
       读注册表键 "AppID" 后 `add r4, r9, #32` 把结果作为 **QUuid** 存到 this+0x20
       （16 字节，与原 4 个 int 占位完全同布局）。am 的启动路径
       （sub_29350 @0x29370 `add r7,r1,#32` / sub_281ec）始终把 +0x20 当 QUuid 读，
       旧实现留空 → am 拿到全零 UUID → 启动/发送 appStart 失败。
       故此处必须是 QUuid，不能用 4 个 int 占位。 */
    QUuid FAppID;       // $20 注册表 AppID（浏览器 = {95ffa462-...}）
    int FTemp030;       // $30 低字节=FixedPosition 次字节=FixedAllPosition
    QString FDir;       // $34 //notepad | /mmc/mmca1/.system/java/DownloadApps/MIDlet20069
    QString FName;       // $38 //Motorola
    QString FVender;       // $3C
    bool FTemp040;       // $40
    bool FTemp041;
    bool FTemp042;
    int FTemp044;       // $44
    QString FTemp048;       // $48 // ""
    QString FTemp04C;   //$4C //""
};

/* v41：进程内图标缓存（key = "全路径|图标名|类型"）。
   装/卸载后图标可能变化，注册表管理器的安装/卸载槽会调它作废缓存。 */
void amAppIconCacheClear();

#endif

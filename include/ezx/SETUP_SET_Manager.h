#ifndef _SETUPSETMANAGER_H_
#define _SETUPSETMANAGER_H_

#include <AM_Global.h>
#include <ZConfig.h>

class SETUP_SET_Manager: public ZConfig
{
public:
    SETUP_SET_Manager(QString, bool);
    SETUP_SET_Manager(AM_Global::Apps, bool);

    virtual int clearGroup(QString);
    virtual int removeEntry(QString, QString);

    int getSettingInfo(QString Section, QString Name, QString& Value, QString Default, bool);
    int getSettingInfo(QString Section, QString Name, bool& Value, bool Default);
    int getSettingInfo(QString Section, QString Name, int& Value, int Default);
    int getSettingInfo(QString Section, QString Name, unsigned& Value, unsigned Default);
    int getSettingInfo(QString Section, QString Name, unsigned short& Value, unsigned short Default);

    int setSettingInfo(QString Section, QString Name, QString, bool);
    int setSettingInfo(QString Section, QString Name, bool);
    int setSettingInfo(QString Section, QString Name, int);
    int setSettingInfo(QString Section, QString Name, unsigned);
    int setSettingInfo(QString Section, QString Name, unsigned short);
    /* v71：返回类型已锁定 = QMap<QString,QString>（按值返回，隐藏返回槽 r0）。
       铁证：真身 libezxappbase @0x41892984 的返回槽构造与 Qt2 qmap.h 的
       QMapPrivate(count,node_count,header) + QMapNode(data,key) 逐字节对应。
       mangled 名不含返回类型，故声明成 QMap 不改变链接符号
       _ZN17SETUP_SET_Manager15getEntryKeyListE7QString（UND，运行期解析）。
       arm-linux-g++ ABI 探针确认调用序列 r0=槽/r1=this/r2=QString 与原库一致。 */
    QMap<QString,QString> getEntryKeyList(QString);
    int getSettingFileName(AM_Global::Apps);
    virtual bool flush(void);

    virtual ~SETUP_SET_Manager();
protected:

private:
    /* ★v69：真身尺寸对齐。我们本地只声明了继承关系（sizeof=8，继承 ZConfig 的 8 字节），
       而原库 `Idle_Setup::getDefaultShortcut @0x41b160f0` 的栈帧是 `sub sp,sp,#0x10`=16：
       里面只有一个 QString 临时(4) + 本对象 ⇒ 真身 sizeof ≈ **12**，我们少 4 字节。
       栈上实例化时会写穿调用者的栈帧（v67→v68 那次 am 崩溃循环就是这么来的：
       返回时跳到 pc=0x1c 的垃圾地址）。补 4 字节填充并加编译期断言，让它下次退步直接编不过。 */
    char m_realSizePad[4];
};

typedef char SETUP_SET_Manager_size_must_be_12[
    sizeof(SETUP_SET_Manager) == 12 ? 1 : -1 ];


#endif

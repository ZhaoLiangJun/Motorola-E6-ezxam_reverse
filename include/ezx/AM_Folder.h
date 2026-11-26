/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 虚表分析）
虚表证据：AM_Folder : public AM_AppObject
  虚表 slot[0..3] 继承基类 getAppLockEnabled/setAppLockEnabled/getAppLockValue/setAppLockValue
  虚表 slot[4] 覆盖为 AM_Folder::dump
  FolderType @ 0x0C (基类 FTemp00C 复用), QList<AM_AppObject>* @ 0x54
*/
#ifndef AM_FOLDER
#define AM_FOLDER

#include <qmap.h>
#include <qlist.h>
#include <AM_AppObject.h>

class AM_Folder : public AM_AppObject
{
public :
  AM_Folder();
  AM_Folder(AM_AppObject::Type type);   /* C1/C2/C4 */
  ~AM_Folder();

  void createObject(QMap<QString, QString> const&);
  void makeNewRecord(QMap<QString, QString>&);
  void init();                                       /* AM_Folder::init（原版独立符号）*/
  void add(AM_AppObject const*, int);
  void remove(AM_AppObject const*);
  void clear();
  bool isEmpty() const;
  void getAppList(QList<AM_AppObject>&) const;
  int  getAppPosition(AM_AppObject const*);
  virtual void dump();                               /* 虚表 slot[4] 覆盖 */

  /* 继承基类虚函数（AM_Folder 未覆盖，直接沿用基类实现）：
     getAppLockEnabled/setAppLockEnabled/getAppLockValue/setAppLockValue */

private :
  int m_pad50;                           /* 0x50 派生前导 */
  QList<AM_AppObject>* m_appList;        /* 0x54  QGList 派生 */
};

#endif

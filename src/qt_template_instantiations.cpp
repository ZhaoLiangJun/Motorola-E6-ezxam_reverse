/* qt_template_instantiations.cpp - Qt 模板显式实例化单元
   逆向重建自 libezxam.so.1.0.0 (Motorola E6)

   依据：精确符号对账（原库 vs 重建库 dynsym STT_FUNC 定义集）发现，
   原库导出了 QCache<int>（键=QString，值=int 指针）整套成员函数，
   全部为弱符号(STB_WEAK, bind=2)，而重建库完全未实例化该模板，
   导致 5 个导出符号缺失：
     _ZN6QCacheIiE5clearEv              QCache<int>::clear()
     _ZNK6QCacheIiE5countEv             QCache<int>::count() const
     _ZN6QCacheIiE10deleteItemEPv       QCache<int>::deleteItem(void*)
     _ZN6QCacheIiED0Ev                  QCache<int>::~QCache() (deleting)
     _ZN6QCacheIiED1Ev                  QCache<int>::~QCache() (complete)

   原库证据（Ghidra 反编译）：
     0x41b0d9b4 QCache<int>::~QCache()
     0x41b0da24 QCache<int>::clear()
     0x41b0db3c QCache<int>::count() const
     0x41b0db58 QCache<int>::deleteItem(void*)
     （紧邻 AM_AppLockManager 编译单元，属同一 TU 隐式实例化后导出）

   作用：原库在 AM_AppLockManager.cpp 等编译单元实例化并**导出**了
   QCache<int>，供其它 E6 共享库（弱引用）在加载期绑定。若重建库缺失，
   引用了这些弱符号的外部库会在运行期得到未解析符号（弱符号未解析为 0，
   调用点若未判空将崩溃）。本单元通过显式实例化（explicit instantiation）
   让 GCC 3.3.6 发出同名弱符号，恢复与原库一致的导出表。

   说明：
   - 显式实例化产生的是弱符号(W)，不会污染强符号表，与原库 bind=2 一致。
   - QCache<int> 的完整/基类构造 C1/C2、赋值等在本单元同时被实例化导出，
     与原库的导出超集兼容（原库弱符号缺失 = 严重，弱符号多出 = 无害）。
   - 关于原库另有 QMapPrivate<QString,QString>::C4 /
     QValueListPrivate<QString>::C4 / QMapIterator::dec /
     QMapConstIterator::inc 四个弱符号：C4 是 GCC 3.3 模板基对象构造的
     内部 ABI 变体，显式实例化不生成；这四个符号是 private 实现类成员，
     外部库只能经 QMap/QValueList 公共接口间接触达，重建库已导出对应
     公共接口方法（find/insertSingle/copy/clear 等），运行期可解析。
     故不对这四个符号做臆测性合成，遵循“保留占位优于臆测”原则。
*/

#include <qcache.h>
#include <qmap.h>
#include <qstring.h>

/* 显式实例化 QCache<int>：键类型 QString（QCache 固定用 QString 作键索引），
   值类型 int（与原库 QCacheIiE mangled 名一致）。
   该实例化导出 clear/count/deleteItem/~QCache(D0/D1) 等弱符号。 */
template class QCache<int>;

/* 显式实例化 QMap<QString,QString> 的迭代器：
   原库导出了 QMapIterator<QString,QString>::dec 和
   QMapConstIterator<QString,QString>::inc 两个弱符号（证据：
   lookUpAppName/makeNewRecord/createObject 内部用临时 QMap<QString,QString>
   做 --it/--it 反向遍历，触发 dec/inc 实例化导出，见 all_decompiled.c
   12806/13857/17670/30672 行）。显式实例化补齐这两个（连同 inc/dec 对称成员）。 */
template class QMapIterator<QString, QString>;
template class QMapConstIterator<QString, QString>;

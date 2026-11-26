/*
 * DmtExternal.h - Motorola EZX DMT 平台类的最小外部声明
 *
 * 这些类由平台库 libezx-dmt.so / libezx-dm.so 提供，本库仅调用其接口。
 * 原库 symbols.json 中 DmtTreeFactory / DmtData / DMString 等均为 undefined
 * （运行期由平台解析）。
 *
 * 重要：本函数 getAppAutoStartStatusFromDmt 内部通过平台对象的虚表
 * (vtable+0x1c 等) 派发调用 JemSmartPtr/DmtData，这些平台类的虚表布局
 * 无公开头文件，无法在不臆造 ABI 的前提下逐条重建虚调用。因此保留为
 * ABI 正确（返回 bool）的最小实现：
 *   - 调用真实的平台入口 DmtTreeFactory::Initialize()/Uninitialize()
 *     （这两个符号在原库同为 undefined，调用约定已知，运行期解析）
 *   - 子树查询/取值路径返回 false（等价于原库 Initialize 失败时的返回）
 * 这样调用方 loadRegistry 拿到的返回类型正确(bool)，不会读到未定义垃圾值。
 */
#ifndef DMT_EXTERNAL_H
#define DMT_EXTERNAL_H

#include <qstring.h>

/* DMString：平台字符串（析构/赋值由平台提供） */
class DMString
{
public:
    DMString();
    ~DMString();
    DMString& operator=(const char* s);
private:
    void* m_imp;    /* 平台内部表示，不直接访问 */
};

/* DmtTreeFactory：DMT 树工厂（静态接口，符号由平台库提供） */
class DmtTreeFactory
{
public:
    static bool Initialize();
    static void Uninitialize();
};

#endif /* DMT_EXTERNAL_H */

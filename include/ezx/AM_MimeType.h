/* AM_MimeType.h - 外部平台类声明（libezx-mime 运行期提供）
 * 逆向重建 AM_RegistryManager::getOpenWithApps 所需的最小接口。
 *
 * 来源：原库 libezxam.so.1.0.0 的 undefined 导入符号（Ghidra + symbols.json）：
 *   _ZN11AM_MimeType8instanceEv              -> AM_MimeType::instance()
 *   _ZN11AM_MimeType14getMimeIdByExtERK7QString -> getMimeIdByExt(const QString&)
 *   _ZN11AM_MimeType17getMimeTypeDataExERK7QString -> getMimeTypeDataEx(const QString&)
 * 三者在原库均为 UND，运行期由平台 MIME 库解析，此处仅声明接口产生忠实外部引用。
 */
#ifndef AM_MIMETYPE_H
#define AM_MIMETYPE_H

#include <qstring.h>
#include <quuid.h>

/* getMimeTypeDataEx 返回的 MIME 类型数据块布局（由反编译偏移推断）：
 *   +0x00  uint  refcount（引用计数，本函数未消费）
 *   +0x04  uint* pRef（自引用，本函数未消费）
 *   +0x08  uint  uuid.data1（4 字节）
 *   +0x0C  uint  uuid.data2（低 16 位）+ uuid.data3（高 16 位）
 *   +0x10  uint  uuid.data4[0..3]（8 字节的前 4 字节）
 *   +0x14  uint  uuid.data4[4..7]（8 字节的后 4 字节）
 * 调用方仅读取 UUID 字段用于匹配，故以最简结构体描述。 */
struct AM_MimeData {
    unsigned int refcount;
    void*        pRef;
    unsigned int uuidData1;  /* +0x08：QUuid.data1 */
    unsigned int uuidData2;  /* +0x0C：低 16 位=data2，高 16 位=data3 */
    unsigned int uuidData3;  /* +0x10：data4[0..3] */
    unsigned int uuidData4;  /* +0x14：data4[4..7] */

    /* 构造 QUuid（按小端字节序展开 data2/3/4） */
    QUuid toQUuid() const {
        return QUuid(
            (ulong)uuidData1,
            (ushort)(uuidData2 & 0xFFFF),
            (ushort)((uuidData2 >> 16) & 0xFFFF),
            (uchar)(uuidData3 & 0xFF),
            (uchar)((uuidData3 >> 8) & 0xFF),
            (uchar)((uuidData3 >> 16) & 0xFF),
            (uchar)((uuidData3 >> 24) & 0xFF),
            (uchar)(uuidData4 & 0xFF),
            (uchar)((uuidData4 >> 8) & 0xFF),
            (uchar)((uuidData4 >> 16) & 0xFF),
            (uchar)((uuidData4 >> 24) & 0xFF)
        );
    }
};

class AM_MimeType
{
public:
    /* 单例（平台 MIME 数据库入口） */
    static AM_MimeType* instance();

    /* 按扩展名取 MIME 类型 ID 串（如 "application/vnd.oma.drm.content"） */
    QString getMimeIdByExt(const QString& ext);

    /* 按 MIME ID 串取类型数据块（含 MIME UUID），无匹配返回 NULL */
    AM_MimeData* getMimeTypeDataEx(const QString& mimeId);
};

#endif /* AM_MIMETYPE_H */

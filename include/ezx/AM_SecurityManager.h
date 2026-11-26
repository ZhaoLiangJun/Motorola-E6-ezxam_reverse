/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 构造函数实证）
AM_SecurityManager:非 QObject，无虚表，sizeof=0x2C
成员（构造函数 0x41b0ee70 实证，8 个 QString + 2 个 int）：
  m_status       @ 0x00 (int)  CM_SEC 证书内存链头（非0则析构 FreeCertPathMem）
  m_error        @ 0x04 (int)  EzXApp-Exec-RSA-SHA1 节点（非0则析构 free）
  m_execPath     @ 0x08 (QString) .desktop 的 Exec 拼出的全路径（m_path + "/" + Exec）
  m_signedData   @ 0x0C (QString) EzXApp-Exec-RSA-SHA1 的值
  m_key1         @ 0x10 (QString)
  m_key2         @ 0x14 (QString)
  m_key3         @ 0x18 (QString)
  m_dirSize      @ 0x1C (int)  目录大小 (getDirSize)
  m_key4         @ 0x20 (QString)
  m_path         @ 0x24 (QString) 验证路径 absPath
  m_config       @ 0x28 (QString) 共享 null（构造默认，非 ZConfig*）
方法：
  verifySecurityByLaunch(path, cert, result):QFileInfo+QDir 取 absPath → CM_SEC + DRM
  verifySecurityByInstall(path, sigPath):QDir::absPath → CM_SEC + DRM, 返回 sigPath
  verifySecurity(path, cert, result, extra):先 parseSignedFile()，再 CM_SEC
  parseSignedFile(path):ZConfig 打开，遍历 Desktop Entry 组键名，提取签名
外部依赖：
  CM_SEC_VerifyFileSignatureByPathGetAllow / CM_SEC_FreeCertPathMem（libezx-cmseccert.so）
  DRM_SP_ValidateNativeAppRights / DRM_SP_ActivateProtectedApp（libezx-drm.so）
  ZConfig, QDir, QFileInfo, AM_Global::getDirSize
*/
#ifndef AM_SECURITYMANAGER
#define AM_SECURITYMANAGER

#include <qstring.h>

class ZConfig;

class AM_SecurityManager
{
public :
  AM_SecurityManager();
  ~AM_SecurityManager();

  /* 验证启动安全：path=文件路径，cert=证书输出，result=结果 */
  int  verifySecurityByLaunch(QString const& path, char* cert, int* result);
  /* 验证安装安全：path=安装路径，sigPath=签名文件路径 */
  int  verifySecurityByInstall(QString const& path, QString& sigPath);
  /* 通用安全验证：path=文件，cert=证书，result=结果，extra=额外参数 */
  int  verifySecurity(QString const& path, char* cert, int* result, int* extra);
  /* 解析已签名的 .desktop 文件，提取签名节 */
  int  parseSignedFile(QString const& path);

private :
  /* v46 内部：单遍扫描 .desktop 的 [Desktop Entry] 组并按原库 if/else 链填成员。
     返回命中条数，-1 = 打不开。非虚、不占对象空间。 */
  int      amScanDesktopEntry(const char* path);
  void*    m_status;       /* 0x00 CM_SEC 证书内存链头（非0则 FreeCertPathMem） */
  void*    m_error;        /* 0x04 RSA-SHA1 签名节点（非0则 free） */
  QString  m_execPath;     /* 0x08 m_path + "/" + Exec（反编译 0x41b10a18 实证） */
  QString  m_signedData;   /* 0x0C */
  QString  m_key1;         /* 0x10 */
  QString  m_key2;         /* 0x14 */
  QString  m_key3;         /* 0x18 */
  int      m_dirSize;      /* 0x1C */
  QString  m_key4;         /* 0x20 */
  QString  m_path;         /* 0x24  absPath */
  QString  m_config;       /* 0x28  路径/默认证书（原版 QString，非 ZConfig*） */
};

#endif

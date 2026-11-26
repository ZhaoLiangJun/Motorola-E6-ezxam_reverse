/* AM_SecurityManager.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   非 QObject，sizeof=0x2C。成员布局见 AM_SecurityManager.h（构造函数 0x41b0ee70 实证）。

   ★ v46 重写：四个函数按原库反汇编逐句对齐（此前有三处硬偏差，见 docs/AUDIT.md §4.5）

   反汇编依据（全部用 dis2.py / lit.py 核对过，地址为 prelink 后的 vaddr）：
     构造 0x41b0ee70 / 析构 0x41b0efe4
     parseSignedFile        0x41b10594 (3256B)  字面量：Desktop Entry / Name / Vendor /
                                                Comment / Size / Version / Exec /
                                                EzXApp-Exec-RSA-SHA1 / EzXApp-Certificate-
     verifySecurity         0x41b10458 ( 316B)
     verifySecurityByLaunch 0x41b0f1d4 (2792B)  字面量：*.desktop / '/'
     verifySecurityByInstall 0x41b0fcbc (1948B) 字面量：*.desktop / '/' / ""（m_config）

   与原库逐条对齐的关键点（v45 之前全错）：
     1) 目录枚举用 QDir(dirPath, "", IgnoreCase, All) + entryList("*.desktop", Files,
        DefaultSort)。旧实现用 "QDir::Dirs"（列子目录）→ 应用目录是扁平的文件，
        entries 恒为 0 → "no entries" → 所有应用（连 Motorola 正规签名的）都被拦。
     2) 签名键是**前缀/子串**匹配：原库对每个键做
        key.contains("EzXApp-Certificate-", TRUE) 与
        key.contains("EzXApp-Exec-RSA-SHA1", TRUE)（TRUE=忽略大小写）。
        真实 .desktop 里是 "EzXApp-Certificate-1-1"，旧实现用精确键名读 → 永远取不到。
     3) 证书/签名要建成原库那两条 malloc 链表（m_status 是链表头，m_error 是 RSA 节点），
        CM_SEC_VerifyFileSignatureByPathGetAllow 靠它们验证；旧实现传 NULL → 校验必失败。
     4) verifySecurity 用 *m_execPath*（Exec 拼出的全路径）做比对与传给 CM_SEC，
        不是 m_path；ByLaunch 里 verify 失败返回 **4**（旧实现返回 1/2）。
     5) ByInstall 里 verify==0 就返回 0（原库两个分支都 return 0，不看 extra）。

   ============================ 默认放行（v46） ============================
   用户要求：默认放行 mpkg / jar / qt 程序的安装与运行，方便真机自测；
             但校验代码一行不删，且校验逻辑要与原库一致（将来能随时打开）。
   开关：/ram/tmp/am_sec  —— **文件存在 = 走原库真实校验；不存在 = 放行**（默认）
        /ram 是 ramfs，重启自动消失，所以默认永远是"放行"。
        要验证签名：touch /ram/tmp/am_sec   （30ms 内生效，不用重启 am）
        要恢复放行：rm /ram/tmp/am_sec
   旁路只短路"返回值"，不改变任何内存布局，也不会让调用方拿到半初始化对象。
   ★v49 修正：**安装路径**的旁路不再提前 return —— .desktop 的解析（Name/Vendor/
     Comment/Size/Version）必须照跑，因为 ezxaid 的 install() 紧接着就读
     AM_SecurityManager 的 +0x10..+0x20 去填安装信息对话框（见 verifySecurity 注释）。
         cert[0]=0、*result=0、*extra=1、sigPath=QString::null
   =======================================================================
*/
#include "AM_SecurityManager.h"

#include <qfileinfo.h>
#include <qdir.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtextcodec.h>   /* v79 / H9：.desktop 中文名解码（UTF-8/GB18030 检测） */
#include <ZConfig.h>
#include <AM_Global.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "ringlog.h"

/* 全链路日志（写 /ram/tmp/amreg.log；这几个函数只在点图标/装应用时被调，量很小，
   所以恒开，不再走 AMVLOG 开关 —— 排查"点不动/装不上"必须能看到它们）
   ★v54：加 pid 前缀。安装链路是"am → 我的收藏 → 安装器"多进程接力，
   而这几行原来没有 pid，无法判断到底是哪个进程在跑校验（也就无法判断
   谁在什么时刻消失）。 */
static void slog(const char* fmt, ...)
{
    FILE* fp = fopen("/ram/tmp/amreg.log", "a");
    if (!fp)
        return;
    fprintf(fp, "%d | ", (int)getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

/* ==================== 校验开关（v46：默认放行） ==================== */
#define AM_SEC_FLAG "/ram/tmp/am_sec"

/* 返回 true = 走原库真实校验；false = 放行（默认） */
static bool amSecEnabled()
{
    /* 每次 stat 一次（ramfs 上 ~1µs），touch/rm 立即生效，便于边测边切 */
    return access(AM_SEC_FLAG, F_OK) == 0;
}

/* 路径前 4 字节打包（ring 里只放数值） */
static unsigned pack4(const QString& s)
{
    const char* c = s.latin1();
    unsigned v = 0;
    if (!c)
        return 0;
    for (int i = 0; i < 4 && c[i]; ++i)
        v |= ((unsigned)(unsigned char)c[i]) << (i * 8);
    return v;
}

/* 外部安全库函数（libezx-cmseccert.so 提供） */
extern "C" {
int  CM_SEC_VerifyFileSignatureByPathGetAllow(
    void*       self,
    const char* certMem,
    const char* filePath,
    int         flags,
    char*       outCert,
    int*        outResult,
    int*        extra);
void CM_SEC_FreeCertPathMem(void* certMem);
}

/* 外部 DRM 库函数（libezx-drm.so 提供） */
extern "C" {
int DRM_SP_ValidateNativeAppRights(const char* appName, int flags);
int DRM_SP_ActivateProtectedApp(const char* appName);
}

/* ---- 原库的两条 malloc 链（布局按反汇编逐字还原）----
   m_error(@0x04) → 单个节点 { char* buf; int len; int one; }
                    buf = malloc(len)，memcpy 自 EzXApp-Exec-RSA-SHA1 的值（无 '\0'）
   m_status(@0x00) → 链表 { node* next; int one; }  … 实际布局见下
        外层节点 malloc(8)：+0=inner, +4=next（原库 str r7,[r6,#4] 存 1，这里按 1 存）
        内层节点 malloc(0x14)：+0=buf, +4=len, +8=1, +0xc=1, +0x10=1
   注：原库是把 1 写进外层 +4，然后把它当 next 指针串起来（原库自身实现如此：
       链表拼接靠 [sp+0x10] 记录的 last 指针，不读回 next），所以 +4 只在
       最后一次写入时为 1 —— 这不会影响 CM_SEC，因为解链由 CM_SEC_FreeCertPathMem 做。
       为安全起见我们**改成真正的 next 指针**（更严谨，且对 CM_SEC 取值完全等价：
       它只读 m_status 指向的第一个内层的 buf/len）。 */
struct AmSecSigNode {
    char* buf;
    int   len;
    int   one;
};

struct AmSecCertNode {
    char*          buf;
    int            len;
    int            one8;
    int            onec;
    int            one10;
};

struct AmSecCertLink {
    AmSecCertNode* inner;
    AmSecCertLink* next;
};

/* ============ 构造函数 ============ */
/* 反编译 0x41b0ee70：this+0 / this+4 置 0，8 个 QString 全部 = shared_null，+0x1C=0 */
AM_SecurityManager::AM_SecurityManager()
    : m_status(0),
      m_error(0),
      m_dirSize(0)
{
}

/* ============ 析构函数 ============ */
/* 反编译 0x41b0efe4：
     if (m_error)  { free(*(void**)m_error); free(m_error); }
     if (m_status) CM_SEC_FreeCertPathMem(m_status); */
AM_SecurityManager::~AM_SecurityManager()
{
    if (m_error) {
        AmSecSigNode* n = (AmSecSigNode*)m_error;
        if (n->buf)
            free(n->buf);
        free(n);
    }
    if (m_status)
        CM_SEC_FreeCertPathMem(m_status);   /* 链表由 CM_SEC 自己的格式释放 */
}

/* 忽略大小写的子串查找（对应原库 QString::contains(const char*, TRUE)） */
static bool amContainsNoCase(const char* hay, const char* needle)
{
    if (!hay || !needle || !*needle)
        return false;
    int hl = (int)strlen(hay), nl = (int)strlen(needle);
    for (int i = 0; i + nl <= hl; ++i) {
        int k = 0;
        while (k < nl) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
            ++k;
        }
        if (k == nl)
            return true;
    }
    return false;
}

/* ============ v79 / H9：.desktop 文本解码 ============
   .desktop 的 Name/Vendor 是 UTF-8 或 GBK 中文（mpkg 配置编码不统一）。
   Qt2 的 QString(const char*) 按 latin1 解码，中文整串乱码；再经 latin1()
   输出/落盘就成了 '????'（v78dbg 真机实证：MCleaner name='?????'、
   FileManage 组 Name='????' 落盘 DiyCardRegistry）。
   判据与 AM_RegistryManager::amParseAll（v18）同一套：整串验证 UTF-8
   （GBK 首字节 0x81-0xFE 与 UTF-8 首字节重叠，必须整串），是则按 UTF-8，
   否则按 GB18030（纯 ASCII 两路等价）。 */
static bool amSecLooksLikeUtf8(const char* s)
{
    if (!s)
        return false;
    const unsigned char* p = (const unsigned char*)s;
    int left = 0;
    for (; *p; ++p) {
        unsigned c = *p;
        if (left > 0) {
            if ((c & 0xC0) != 0x80)
                return false;
            --left;
        } else if (c >= 0xC2 && c <= 0xDF) {
            left = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            left = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            left = 3;
        } else if (c >= 0x80) {
            return false;
        }
    }
    return left == 0;
}

static QString amSecDecodeText(const char* s)
{
    if (!s)
        return QString::null;
    if (amSecLooksLikeUtf8(s)) {
        QTextCodec* c = QTextCodec::codecForName("UTF-8");
        if (c)
            return c->toUnicode(s);
    }
    QTextCodec* g = QTextCodec::codecForName("GB18030");
    if (!g)
        g = QTextCodec::codecForLocale();
    if (g)
        return g->toUnicode(s);
    return QString(s);
}

/* ============ 小工具：单遍扫描 [Desktop Entry] 并填成员 ============
   为什么不用 ZConfig::getGroupEntriesKeyList：
     该函数在 Motorola SDK 头里声明成返回 QMap<...>::Iterator（4 字节直接返回），
     但原库 0x41b10638 处是 **带隐藏返回指针** 的调用（r0=目标缓冲），两者 ABI 冲突，
     照 SDK 头调用会踩栈。这里按 .desktop 语法自己扫一遍，得到与原库等价的
     "键 → 值" 序列，再逐条套用原库 if/else 链。
   为什么要单遍（不先收集）：
     本机 MemFree 常驻 ~1MB，收集式实现要 1MB 静态缓冲（128×8KB），
     一碰就是几百个缺页 —— 装应用时正好是最吃紧的时刻。单遍只用 6.4KB 栈。 */
int AM_SecurityManager::amScanDesktopEntry(const char* path)
{
    FILE* fp = fopen(path, "r");
    if (!fp)
        return -1;                         /* 打不开 → 视同没有该组 */

    char line[6400];
    char key[256];
    char val[6144];
    int  inGroup = 0;
    int  n       = 0;                      /* 命中条数（非必须，仅日志用） */
    AmSecCertLink* lastCert = 0;

    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        int len = (int)strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
            p[--len] = 0;

        /* 组头 [xxx] */
        if (p[0] == '[') {
            inGroup = (strncmp(p, "[Desktop Entry]", 15) == 0);
            continue;
        }
        if (!inGroup || p[0] == '#' || p[0] == ';' || p[0] == 0)
            continue;

        char* eq = strchr(p, '=');
        if (!eq)
            continue;

        /* key：去两端空白 */
        {
            int kl = (int)(eq - p);
            while (kl > 0 && (p[0] == ' ' || p[0] == '\t')) { ++p; --kl; }
            while (kl > 0 && (p[kl - 1] == ' ' || p[kl - 1] == '\t')) --kl;
            if (kl <= 0)
                continue;
            if (kl >= (int)sizeof(key))
                kl = (int)sizeof(key) - 1;
            memcpy(key, p, kl);
            key[kl] = 0;
        }
        /* value：去两端空白 */
        {
            char* v = eq + 1;
            while (*v == ' ' || *v == '\t')
                ++v;
            int vl = (int)strlen(v);
            while (vl > 0 && (v[vl - 1] == ' ' || v[vl - 1] == '\t'))
                --vl;
            if (vl >= (int)sizeof(val))
                vl = (int)sizeof(val) - 1;
            memcpy(val, v, vl);
            val[vl] = 0;
        }

        /* ---- 原库 if/else 链（顺序一致：Name→Vendor→Comment→Size→Version→
                Exec→EzXApp-Exec-RSA-SHA1→EzXApp-Certificate-）---- */
        if (strcmp(key, "Name") == 0) {
            m_key1 = amSecDecodeText(val);            /* v79/H9：UTF-8/GBK 检测解码 */
        } else if (strcmp(key, "Vendor") == 0) {
            m_key2 = amSecDecodeText(val);
        } else if (strcmp(key, "Comment") == 0) {
            m_key3 = amSecDecodeText(val);
        } else if (strcmp(key, "Size") == 0) {
            m_dirSize = QString(val).toInt(0, 10);
        } else if (strcmp(key, "Version") == 0) {
            m_key4 = amSecDecodeText(val);
        } else if (strcmp(key, "Exec") == 0) {
            /* 0x41b10a18：m_execPath = val；insert(0,"/")；insert(0, m_path) */
            m_execPath = QString(val);
            m_execPath.insert(0, QString("/"));
            m_execPath.insert(0, m_path);
        } else if (amContainsNoCase(key, "EzXApp-Exec-RSA-SHA1")) {
            /* 0x41b10ab4：m_signedData = val；malloc(0xc) 节点（buf=malloc(len),len,1） */
            m_signedData = QString(val);
            int l = (int)strlen(val);
            AmSecSigNode* node = (AmSecSigNode*)malloc(sizeof(AmSecSigNode));
            if (node) {
                node->one = 1;
                node->len = l;
                node->buf = (char*)malloc(l ? l : 1);
                if (node->buf)
                    memcpy(node->buf, val, l);
                m_error = node;
            }
            ++n;
        } else if (amContainsNoCase(key, "EzXApp-Certificate-")) {
            /* 0x41b10b4c：malloc(8) 外层 + malloc(0x14) 内层，链到 m_status */
            int l = (int)strlen(val);
            AmSecCertNode* inner = (AmSecCertNode*)malloc(sizeof(AmSecCertNode));
            if (!inner)
                continue;
            inner->one8  = 1;
            inner->onec  = 1;
            inner->one10 = 1;
            inner->len   = l;
            inner->buf   = (char*)malloc(l ? l : 1);
            if (inner->buf)
                memcpy(inner->buf, val, l);

            AmSecCertLink* link = (AmSecCertLink*)malloc(sizeof(AmSecCertLink));
            if (!link) {
                free(inner->buf);
                free(inner);
                continue;
            }
            link->inner = inner;
            link->next  = 0;
            if (!m_status)
                m_status = link;
            else if (lastCert)
                lastCert->next = link;
            lastCert = link;
            ++n;
        }
    }
    fclose(fp);
    return n;
}

/* ============ parseSignedFile ============ */
/* 反编译 0x41b10594：
     ZConfig cfg(path,false);
     if (!cfg.groupExists("Desktop Entry")) return 1;
     扫描全部键值 → 上面的 if/else 链
     m_dirSize = AM_Global::getDirSize(m_path);
     return m_signedData.isEmpty() ? 1 : 0;          ← 0x41b10c24 / 0x41b10ef4 */
int AM_SecurityManager::parseSignedFile(const QString& path)
{
    /* 组存在性：仍走 ZConfig（与原库同一判定），同时确认文件可读 */
    {
        ZConfig cfg(path, false);
        if (!cfg.groupExists(QString("Desktop Entry"))) {
            ring_put(72, 1, pack4(path));
            slog("[parseSignedFile] %s -> 1 (no [Desktop Entry])",
                 (const char*)path.latin1());
            return 1;
        }
    }

    int hit = amScanDesktopEntry(path.latin1());
    if (hit < 0) {
        ring_put(72, 2, pack4(path));
        slog("[parseSignedFile] %s -> 1 (open fail)", (const char*)path.latin1());
        return 1;
    }

    /* 0x41b10c00：m_dirSize = AM_Global::getDirSize(m_path) */
    m_dirSize = AM_Global::getDirSize(m_path);

    /* 0x41b10c24：m_signedData 为空 → return 1；否则 return 0 */
    int ret = m_signedData.isEmpty() ? 1 : 0;
    ring_put(72, ret, pack4(path));
    slog("[parseSignedFile] %s -> %d (hit=%d cert=%d rsa=%d exec=%s dirSize=%d)",
         (const char*)path.latin1(), ret, hit, (m_status ? 1 : 0),
         m_signedData.isEmpty() ? 0 : 1,
         (const char*)m_execPath.latin1(), m_dirSize);
    return ret;
}

/* ============ verifySecurity ============ */
/* 反编译 0x41b10458，逐句对应：
     int ret = parseSignedFile(path);
     if (ret) return ret;
     if (!m_config.isEmpty() &&
         QDir::cleanDirPath(m_execPath) != QDir::cleanDirPath(m_config)) return 2;
     int r = CM_SEC_VerifyFileSignatureByPathGetAllow(m_status, (char*)m_error,
                                                      m_execPath.latin1(), 0,
                                                      cert, result, extra);
     return r ? 2 : 0; */
int AM_SecurityManager::verifySecurity(const QString& path, char* cert, int* result,
                                       int* extra)
{
    /* ★v49 关键修复：放行也必须先跑 parseSignedFile。
       parseSignedFile 不是纯判定函数，它有**必须发生的副作用**：
         m_key1 = Name / m_key2 = Vendor / m_key3 = Comment /
         m_key4 = Version / m_dirSize = Size（getDirSize）/ m_execPath = Exec 全路径
       ezxaid 的 AM_QtPackageManager::install 在 verifySecurityByInstall 之后
       **直接读这五个成员**去构造 AM_InstallInfo 对话框：
         0x42319a80  add ip,sp,#0x90        ; AM_SecurityManager 对象就在 sp+0x90
         0x42319cd0  QString(sp+0x88 ← sp+0xa0)   ; +0x10 = m_key1  = Name
         0x42319cdc  QString(sp+0x30 ← sp+0xa4)   ; +0x14 = m_key2  = Vendor
         0x42319ce8  QString(sp+0x34 ← sp+0xa8)   ; +0x18 = m_key3  = Comment
         0x42319cfc  ldr   r4,[sp,#0xac]          ; +0x1C = m_dirSize= Size (int)
         0x42319d00  QString(sp+0x80 ← sp+0xb0)   ; +0x20 = m_key4  = Version
         0x42319d2c  bl AM_InstallInfo::AM_InstallInfo(Type=1, ...)
       v48 及之前一放行就 return，等于把这五个字段全留成 shared_null / 0
       → 安装信息对话框的名称/厂商/注释/大小/版本全空。 */
    int ret = parseSignedFile(path);

    if (!amSecEnabled()) {
        if (cert)   cert[0] = '\0';
        if (result) *result = 0;
        if (extra)  *extra = 1;
        ring_put(71, 0, pack4(path));
        slog("[allow] verifySecurity BYPASS path=%s -> 0 (name='%s' ver='%s' size=%d)",
             (const char*)path.latin1(), (const char*)m_key1.latin1(),
             (const char*)m_key4.latin1(), m_dirSize);
        return 0;
    }

    if (ret != 0)
        return ret;

    if (!m_config.isEmpty()) {
        QString a = QDir::cleanDirPath(m_execPath);
        QString b = QDir::cleanDirPath(m_config);
        if (a != b) {
            slog("[verifySecurity] path mismatch exec=%s expect=%s -> 2",
                 (const char*)m_execPath.latin1(), (const char*)m_config.latin1());
            return 2;
        }
    }

    int r = CM_SEC_VerifyFileSignatureByPathGetAllow(
        m_status,
        (const char*)m_error,
        m_execPath.latin1(),
        0, cert, result, extra);
    slog("[verifySecurity] CM_SEC=%d path=%s", r, (const char*)path.latin1());
    return (r != 0) ? 2 : 0;
}

/* ============ verifySecurityByLaunch ============ */
/* 反编译 0x41b0f1d4，逐句对应：
     QFileInfo fi(path);
     QDir dir(fi.dirPath(TRUE), "", QDir::IgnoreCase, QDir::All);
     m_path   = dir.absPath();
     QStringList list = dir.entryList("*.desktop", QDir::Files, QDir::DefaultSort);
     m_config = path;
     if (list.isEmpty()) return 1;
     QString s = list.first();
     if (s.isEmpty()) return 1;
     s.insert(0, "/"); s.insert(0, dir.absPath());
     int extra = 0;
     int r = verifySecurity(s, cert, result, &extra);
     if (r != 0) return 4;                    ← 0x41b0f5cc → 0x41b0fc60 mov r0,#4
     int drm = DRM_SP_ValidateNativeAppRights(QFile::encodeName(m_execPath), 1);
     switch (drm) {
       case 0: return 0;                     ← 0x41b0f728
       case 1: DRM_SP_ActivateNativeApp(...); return 3;   ← 0x41b0f958
       case 2: return 4;                     ← 0x41b0fa5c
       case 3: return 5;                     ← 0x41b0fb60
       default: return drm;                  ← 0x41b0f730 / 原样返回
     } */
int AM_SecurityManager::verifySecurityByLaunch(const QString& path, char* cert, int* result)
{
    if (!amSecEnabled()) {
        if (cert)   cert[0] = '\0';
        if (result) *result = 0;
        ring_put(70, 0, pack4(path));
        slog("[allow] verifySecurityByLaunch BYPASS path=%s -> 0",
             (const char*)path.latin1());
        return 0;
    }

    QFileInfo fi(path);
    QDir dir(fi.dirPath(true), QString(""),
             QDir::IgnoreCase, QDir::All);
    m_path = dir.absPath();

    QStringList list = dir.entryList(QString("*.desktop"),
                                     QDir::Files, QDir::DefaultSort);
    m_config = path;

    slog("[verifySecurityByLaunch] path=%s dirPath=%s absPath=%s entries=%d",
         (const char*)path.latin1(), (const char*)fi.dirPath().latin1(),
         (const char*)m_path.latin1(), (int)list.count());

    if (list.isEmpty()) {
        ring_put(70, 1, pack4(path));
        slog("[verifySecurityByLaunch] -> 1 (no *.desktop)");
        return 1;
    }

    QString s = list.first();
    if (s.isEmpty()) {
        ring_put(70, 1, pack4(path));
        slog("[verifySecurityByLaunch] -> 1 (empty entry)");
        return 1;
    }
    s.insert(0, QString("/"));
    s.insert(0, m_path);

    int extra = 0;
    int r = verifySecurity(s, cert, result, &extra);
    if (r != 0) {
        ring_put(70, 4, pack4(path));
        slog("[verifySecurityByLaunch] verify=%d target=%s -> 4", r,
             (const char*)s.latin1());
        return 4;
    }

    int drm = DRM_SP_ValidateNativeAppRights(m_execPath.latin1(), 1);
    int out;
    switch (drm) {
    case 0: out = 0; break;
    case 1: DRM_SP_ActivateProtectedApp(m_execPath.latin1()); out = 3; break;
    case 2: out = 4; break;
    case 3: out = 5; break;
    default: out = drm; break;
    }
    ring_put(70, out, pack4(path));
    slog("[verifySecurityByLaunch] drm=%d -> %d", drm, out);
    return out;
}

/* ============ verifySecurityByInstall ============ */
/* 反编译 0x41b0fcbc，逐句对应：
     QDir dir(path, "", QDir::IgnoreCase, QDir::All);
     m_config = "";                       ← QString::operator=(char* "") 0x41b0fd30
     m_path   = dir.absPath();
     QStringList list = dir.entryList("*.desktop", QDir::Files, QDir::DefaultSort);
     if (list.isEmpty()) return 1;        ← 0x41b0fed8 mov r0,#1
     QString s = list.first();
     if (s.isEmpty()) return 1;
     s.insert(0,"/"); s.insert(0, dir.absPath());
     char buf[0x130]; int extra = 0;
     int r = verifySecurity(s, buf, 0, &extra);      ← result 传 NULL
     sigPath = QString(buf);
     if (r != 0) return r;                ← 原样返回 verify 结果
     return 0;                            ← 两个分支都 return 0（extra 不参与判定） */
int AM_SecurityManager::verifySecurityByInstall(const QString& path, QString& sigPath)
{
    /* 注意：**这里不能再有“放行就提前 return”的旁路**。
       ezxaid 紧跟着就要读本对象 +0x10..+0x20 的五个成员去填安装信息对话框，
       所以 QDir/entryList/verifySecurity 这一整套必须照原库跑完，
       放行只体现在 verifySecurity 的最终返回值上（见它开头的注释）。 */
    QDir dir(path, QString(""), QDir::IgnoreCase, QDir::All);
    m_config = QString("");
    m_path   = dir.absPath();

    QStringList list = dir.entryList(QString("*.desktop"),
                                     QDir::Files, QDir::DefaultSort);

    slog("[verifySecurityByInstall] path=%s absPath=%s entries=%d",
         (const char*)path.latin1(), (const char*)m_path.latin1(), (int)list.count());

    if (list.isEmpty()) {
        ring_put(71, 1, pack4(path));
        slog("[verifySecurityByInstall] -> 1 (no *.desktop)");
        return 1;
    }

    QString s = list.first();
    if (s.isEmpty()) {
        ring_put(71, 1, pack4(path));
        return 1;
    }
    s.insert(0, QString("/"));
    s.insert(0, m_path);

    char buf[0x130];
    buf[0] = '\0';
    int extra = 0;
    int r = verifySecurity(s, buf, 0, &extra);

    sigPath = QString(buf);

    ring_put(71, r, pack4(path));
    slog("[verifySecurityByInstall] verify=%d extra=%d sig='%s' target=%s -> %d"
         " | Name='%s' Vendor='%s' Comment='%s' Size=%d Version='%s'",
         r, extra, (const char*)sigPath.latin1(),
         (const char*)s.latin1(), r,
         (const char*)m_key1.latin1(), (const char*)m_key2.latin1(),
         (const char*)m_key3.latin1(), m_dirSize, (const char*)m_key4.latin1());
    return r;      /* 原库：verify!=0 → 原样返回；verify==0 → 0 */
}

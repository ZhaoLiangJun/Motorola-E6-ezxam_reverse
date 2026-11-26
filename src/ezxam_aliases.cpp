/*
 * ezxam_aliases.cpp - 补齐重建库缺失的 C4/D4/D0 符号 (tail-call 转发, __asm__ 标签定名)
 *
 * 背景:
 *   原库导出 Itanium ABI 完整构造 C4 / 基础析构 D4 / deleting 析构 D0。
 *   重建库把真实实现放在 C1/D1 上, 缺 C4/D4/D0 导出。
 *   动态链接器按符号名解析, 缺失会导致 dlopen / 应用加载失败。
 *
 * 解法:
 *   Itanium ABI 中 C4≡C1, D4≡D1, D0(deleting)≡D1(E6 EZX C++03 无异常/RTTI,
 *   析构体内部已含 operator delete 分支)。用 __asm__("原名") 给转发函数强制
 *   符号名, 编译器不再 mangle; 通过与其它 TU 相同的 C++ 编译流程,
 *   产出属性一致的软浮点 .o, tail-call 转发零开销。
 */

extern "C" {

/* ---- Idle_Setup ---- */
extern void _ZN10Idle_SetupC1Ev(void*);
extern void _ZN10Idle_SetupD1Ev(void*);
extern "C" void ezx_idle_setup_c4(void*) __asm__("_ZN10Idle_SetupC4Ev");
void ezx_idle_setup_c4(void* t){ _ZN10Idle_SetupC1Ev(t); }
extern "C" void ezx_idle_setup_d4(void*) __asm__("_ZN10Idle_SetupD4Ev");
void ezx_idle_setup_d4(void* t){ _ZN10Idle_SetupD1Ev(t); }

/* ---- AM_Shortcut ---- */
extern void _ZN11AM_ShortcutC1Ev(void*);
extern void _ZN11AM_ShortcutD1Ev(void*);
extern "C" void ezx_sc_c4(void*) __asm__("_ZN11AM_ShortcutC4Ev");
void ezx_sc_c4(void* t){ _ZN11AM_ShortcutC1Ev(t); }
extern "C" void ezx_sc_d4(void*) __asm__("_ZN11AM_ShortcutD4Ev");
void ezx_sc_d4(void* t){ _ZN11AM_ShortcutD1Ev(t); }
/* D0 别名已移除：AM_AppObject 改虚析构后编译器自动生成 D0，不再需要转发 */

/* ---- AM_AppObject ---- */
extern void _ZN12AM_AppObjectC1Ev(void*);
extern void _ZN12AM_AppObjectC1ENS_4TypeE(void*,int);
extern void _ZN12AM_AppObjectD1Ev(void*);
extern "C" void ezx_ao_c4(void*) __asm__("_ZN12AM_AppObjectC4Ev");
void ezx_ao_c4(void* t){ _ZN12AM_AppObjectC1Ev(t); }
extern "C" void ezx_ao_c4t(void*,int) __asm__("_ZN12AM_AppObjectC4ENS_4TypeE");
void ezx_ao_c4t(void* t,int ty){ _ZN12AM_AppObjectC1ENS_4TypeE(t,ty); }
extern "C" void ezx_ao_d4(void*) __asm__("_ZN12AM_AppObjectD4Ev");
void ezx_ao_d4(void* t){ _ZN12AM_AppObjectD1Ev(t); }
/* D0 别名已移除：编译器自动生成 */

/* ---- AM_AppFactory ---- */
extern void _ZN13AM_AppFactoryC1Ev(void*);
extern void _ZN13AM_AppFactoryD1Ev(void*);
extern "C" void ezx_af_c4(void*) __asm__("_ZN13AM_AppFactoryC4Ev");
void ezx_af_c4(void* t){ _ZN13AM_AppFactoryC1Ev(t); }
extern "C" void ezx_af_d4(void*) __asm__("_ZN13AM_AppFactoryD4Ev");
void ezx_af_d4(void* t){ _ZN13AM_AppFactoryD1Ev(t); }

/* ---- AM_StandardDlg ---- */
extern void _ZN14AM_StandardDlgC1EP7QWidgetPKc(void*,void*,const char*);
extern void _ZN14AM_StandardDlgD1Ev(void*);
extern "C" void ezx_sd_c4(void*,void*,const char*) __asm__("_ZN14AM_StandardDlgC4EP7QWidgetPKc");
void ezx_sd_c4(void* t,void* p,const char* n){ _ZN14AM_StandardDlgC1EP7QWidgetPKc(t,p,n); }
extern "C" void ezx_sd_d4(void*) __asm__("_ZN14AM_StandardDlgD4Ev");
void ezx_sd_d4(void* t){ _ZN14AM_StandardDlgD1Ev(t); }

/* ---- AM_Carrier_Info ---- */
extern void _ZN15AM_Carrier_InfoC1EP7QObject(void*,void*);
extern void _ZN15AM_Carrier_InfoD1Ev(void*);
extern "C" void ezx_ci_c4(void*,void*) __asm__("_ZN15AM_Carrier_InfoC4EP7QObject");
void ezx_ci_c4(void* t,void* p){ _ZN15AM_Carrier_InfoC1EP7QObject(t,p); }
extern "C" void ezx_ci_d4(void*) __asm__("_ZN15AM_Carrier_InfoD4Ev");
void ezx_ci_d4(void* t){ _ZN15AM_Carrier_InfoD1Ev(t); }

/* ---- AM_EmbeddedLink ---- */
extern void _ZN15AM_EmbeddedLinkC1Ev(void*);
extern void _ZN15AM_EmbeddedLinkD1Ev(void*);
extern "C" void ezx_el_c4(void*) __asm__("_ZN15AM_EmbeddedLinkC4Ev");
void ezx_el_c4(void* t){ _ZN15AM_EmbeddedLinkC1Ev(t); }
extern "C" void ezx_el_d4(void*) __asm__("_ZN15AM_EmbeddedLinkD4Ev");
void ezx_el_d4(void* t){ _ZN15AM_EmbeddedLinkD1Ev(t); }
/* D0 别名已移除：编译器自动生成 */

/* ---- AM_SystemConfig ---- */
extern void _ZN15AM_SystemConfigC1Ev(void*);
extern void _ZN15AM_SystemConfigD1Ev(void*);
extern "C" void ezx_syc_c4(void*) __asm__("_ZN15AM_SystemConfigC4Ev");
void ezx_syc_c4(void* t){ _ZN15AM_SystemConfigC1Ev(t); }
extern "C" void ezx_syc_d4(void*) __asm__("_ZN15AM_SystemConfigD4Ev");
void ezx_syc_d4(void* t){ _ZN15AM_SystemConfigD1Ev(t); }

/* ---- AM_AppLockManager ---- */
extern void _ZN17AM_AppLockManagerC1EP7QObjectPKc(void*,void*,const char*);
extern void _ZN17AM_AppLockManagerD1Ev(void*);
extern "C" void ezx_alm_c4(void*,void*,const char*) __asm__("_ZN17AM_AppLockManagerC4EP7QObjectPKc");
void ezx_alm_c4(void* t,void* p,const char* n){ _ZN17AM_AppLockManagerC1EP7QObjectPKc(t,p,n); }
extern "C" void ezx_alm_d4(void*) __asm__("_ZN17AM_AppLockManagerD4Ev");
void ezx_alm_d4(void* t){ _ZN17AM_AppLockManagerD1Ev(t); }

/* ---- AM_RegistryManager ---- */
extern void _ZN18AM_RegistryManagerC1EP7QObjectPKc(void*,void*,const char*);
extern void _ZN18AM_RegistryManagerD1Ev(void*);
extern "C" void ezx_rm_c4(void*,void*,const char*) __asm__("_ZN18AM_RegistryManagerC4EP7QObjectPKc");
void ezx_rm_c4(void* t,void* p,const char* n){ _ZN18AM_RegistryManagerC1EP7QObjectPKc(t,p,n); }
extern "C" void ezx_rm_d4(void*) __asm__("_ZN18AM_RegistryManagerD4Ev");
void ezx_rm_d4(void* t){ _ZN18AM_RegistryManagerD1Ev(t); }

/* ---- AM_SecurityManager ---- */
extern void _ZN18AM_SecurityManagerC1Ev(void*);
extern void _ZN18AM_SecurityManagerD1Ev(void*);
extern "C" void ezx_sm_c4(void*) __asm__("_ZN18AM_SecurityManagerC4Ev");
void ezx_sm_c4(void* t){ _ZN18AM_SecurityManagerC1Ev(t); }
extern "C" void ezx_sm_d4(void*) __asm__("_ZN18AM_SecurityManagerD4Ev");
void ezx_sm_d4(void* t){ _ZN18AM_SecurityManagerD1Ev(t); }

/* ---- AM_AppDataInterface ---- */
extern void _ZN19AM_AppDataInterfaceC1EP18AM_RegistryManagerP7QObjectPKc(void*,void*,void*,const char*);
extern void _ZN19AM_AppDataInterfaceD1Ev(void*);
extern "C" void ezx_adi_c4(void*,void*,void*,const char*) __asm__("_ZN19AM_AppDataInterfaceC4EP18AM_RegistryManagerP7QObjectPKc");
void ezx_adi_c4(void* t,void* r,void* p,const char* n){ _ZN19AM_AppDataInterfaceC1EP18AM_RegistryManagerP7QObjectPKc(t,r,p,n); }
extern "C" void ezx_adi_d4(void*) __asm__("_ZN19AM_AppDataInterfaceD4Ev");
void ezx_adi_d4(void* t){ _ZN19AM_AppDataInterfaceD1Ev(t); }

/* ---- AM_MainMenuInterface ---- */
extern void _ZN20AM_MainMenuInterfaceC1Ev(void*);
extern void _ZN20AM_MainMenuInterfaceD1Ev(void*);
extern "C" void ezx_mmi_c4(void*) __asm__("_ZN20AM_MainMenuInterfaceC4Ev");
void ezx_mmi_c4(void* t){ _ZN20AM_MainMenuInterfaceC1Ev(t); }
extern "C" void ezx_mmi_d4(void*) __asm__("_ZN20AM_MainMenuInterfaceD4Ev");
void ezx_mmi_d4(void* t){ _ZN20AM_MainMenuInterfaceD1Ev(t); }

/* ---- AM_GroupListScrollView ---- */
extern void _ZN22AM_GroupListScrollViewC1E5QListI9AM_FolderE7QStringP7QWidget(void*,void*,void*,void*);
extern void _ZN22AM_GroupListScrollViewD1Ev(void*);
extern "C" void ezx_glsv_c4(void*,void*,void*,void*) __asm__("_ZN22AM_GroupListScrollViewC4E5QListI9AM_FolderE7QStringP7QWidget");
void ezx_glsv_c4(void* t,void* l,void* s,void* p){ _ZN22AM_GroupListScrollViewC1E5QListI9AM_FolderE7QStringP7QWidget(t,l,s,p); }
extern "C" void ezx_glsv_d4(void*) __asm__("_ZN22AM_GroupListScrollViewD4Ev");
void ezx_glsv_d4(void* t){ _ZN22AM_GroupListScrollViewD1Ev(t); }

/* ---- AM_GroupListScrollViewItem ---- */
extern void _ZN26AM_GroupListScrollViewItemC1E7QStringP9QListViewRKS0_N14QCheckListItem4TypeE(void*,void*,void*,void*,int);
extern void _ZN26AM_GroupListScrollViewItemD1Ev(void*);
extern "C" void ezx_glsvi_c4(void*,void*,void*,void*,int) __asm__("_ZN26AM_GroupListScrollViewItemC4E7QStringP9QListViewRKS0_N14QCheckListItem4TypeE");
void ezx_glsvi_c4(void* t,void* a,void* b,void* c,int d){ _ZN26AM_GroupListScrollViewItemC1E7QStringP9QListViewRKS0_N14QCheckListItem4TypeE(t,a,b,c,d); }
extern "C" void ezx_glsvi_d4(void*) __asm__("_ZN26AM_GroupListScrollViewItemD4Ev");
void ezx_glsvi_d4(void* t){ _ZN26AM_GroupListScrollViewItemD1Ev(t); }

/* ---- AM_AppLnk ---- */
extern void _ZN9AM_AppLnkC1Ev(void*);
extern void _ZN9AM_AppLnkD1Ev(void*);
extern "C" void ezx_al_c4(void*) __asm__("_ZN9AM_AppLnkC4Ev");
void ezx_al_c4(void* t){ _ZN9AM_AppLnkC1Ev(t); }
extern "C" void ezx_al_d4(void*) __asm__("_ZN9AM_AppLnkD4Ev");
void ezx_al_d4(void* t){ _ZN9AM_AppLnkD1Ev(t); }
/* D0 别名已移除：编译器自动生成 */

/* ---- AM_Folder ---- */
extern void _ZN9AM_FolderC1Ev(void*);
extern void _ZN9AM_FolderD1Ev(void*);
extern "C" void ezx_fo_c4(void*) __asm__("_ZN9AM_FolderC4Ev");
void ezx_fo_c4(void* t){ _ZN9AM_FolderC1Ev(t); }
extern "C" void ezx_fo_d4(void*) __asm__("_ZN9AM_FolderD4Ev");
void ezx_fo_d4(void* t){ _ZN9AM_FolderD1Ev(t); }
/* D0 别名已移除：编译器自动生成 */

/* ---- Qt2 模板实例（v51 补齐）----
   原库 .dynsym 里有这两个 C4（allocating 构造），我们的实例化只生成了 C1：
     _ZN11QMapPrivateI7QStringS0_EC4EPKS1_           QMapPrivate<QString,QString>::C4(C* const)
     _ZN17QValueListPrivateI7QStringEC4ERKS1_        QValueListPrivate<QString>::C4(const&)
   两个类都是非多态、无虚基的纯数据容器，C4≡C1，转发即可。
   缺了它们，按原库原型调用这两个构造的固件库（例如 mystuff 自己的
   QValueListPrivate 拷贝构造）在解析期就会拿不到符号。 */
extern void _ZN11QMapPrivateI7QStringS0_EC1EPKS1_(void*, const void*);
extern "C" void ezx_qmp_c4(void*, const void*)
    __asm__("_ZN11QMapPrivateI7QStringS0_EC4EPKS1_");
void ezx_qmp_c4(void* t, const void* o){ _ZN11QMapPrivateI7QStringS0_EC1EPKS1_(t, o); }

extern void _ZN17QValueListPrivateI7QStringEC1ERKS1_(void*, const void*);
extern "C" void ezx_qvlp_c4(void*, const void*)
    __asm__("_ZN17QValueListPrivateI7QStringEC4ERKS1_");
void ezx_qvlp_c4(void* t, const void* o){ _ZN17QValueListPrivateI7QStringEC1ERKS1_(t, o); }

} /* extern "C" */

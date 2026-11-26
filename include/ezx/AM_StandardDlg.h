/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译）
AM_StandardDlg : public UTIL_Dialog (QDialog) — Q_OBJECT 信号槽
虚表证据：_ZTV14AM_StandardDlg @ 0x41b22c30；析构 thunk 在 -36/-48（多重继承 UTIL_Dialog）
成员：
  m_pVMainLayout  @ 0xD0 (QVBoxLayout*)
  m_pVSubLayout   @ 0xD4 (QVBoxLayout*)
  m_pHBtnLayout   @ 0xD8 (QHBoxLayout*)
  m_pOkBtn        @ 0xE0 (UTIL_PushButton*)
  m_nSelect       @ 0xE4 (int：0=取消 1=确定)
方法逻辑：
  preInit()  : new QVBoxLayout(this) -> 0xD0; new QVBoxLayout(0xD0) -> 0xD4
  postInit() : new QHBoxLayout(0xD0) -> 0xD8; new UTIL_PushButton(this,2) -> 0xE0; tr(确定/取消)
  getMainLayout() : return 0xD4
  slotOk()    : m_nSelect=1; accept()
  slotCancel(): m_nSelect=0; reject()
  getSelect() : return m_nSelect
*/
#ifndef AM_STANDARDDLG_H
#define AM_STANDARDDLG_H

#include <qdialog.h>
#include <qstring.h>

class QLayout;
class QVBoxLayout;
class QHBoxLayout;
class UTIL_PushButton;

class AM_StandardDlg : public QDialog
{
    Q_OBJECT
public:
    AM_StandardDlg(QWidget* parent = 0, const char* name = 0);
    virtual ~AM_StandardDlg();

    /* 原库虚表证据：init/slotOk/slotCancel 为虚函数（虚表槽 108/109/110），
       重建必须声明为 virtual，否则虚表少 3 槽导致槽位错位崩溃。 */
    virtual void   init();
    void   preInit();
    void   postInit();
    QLayout* getMainLayout();
    int    getSelect();

public slots:
    virtual void slotOk();
    virtual void slotCancel();

private:
    QVBoxLayout*     m_pVMainLayout;    /* 0xD0 */
    QVBoxLayout*     m_pVSubLayout;     /* 0xD4 */
    QHBoxLayout*     m_pHBtnLayout;     /* 0xD8 */
    UTIL_PushButton* m_pCancelBtn;      /* 0xDC  ← 反编译发现，原先遗漏 */
    UTIL_PushButton* m_pOkBtn;          /* 0xE0 */
    int              m_nSelect;         /* 0xE4 */
};

#endif // AM_STANDARDDLG_H

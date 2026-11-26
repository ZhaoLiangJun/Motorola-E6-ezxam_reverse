/* AM_StandardDlg.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   继承 UTIL_Dialog(QDialog)；虚析构（thunk 在 -36/-48，多重继承）。
   成员：m_pVMainLayout@0xD0, m_pVSubLayout@0xD4, m_pHBtnLayout@0xD8,
         m_pOkBtn@0xE0(UTIL_PushButton*), m_nSelect@0xE4
   依赖外部：UTIL_PushButton（libezxutil 提供）。
*/
#include "AM_StandardDlg.h"

#include <qdialog.h>
#include <qlayout.h>
#include <qpushbutton.h>
#include <ZGlobal.h>
#include <UTIL_PushButton.h>

/* ============ 构造函数 ============ */
AM_StandardDlg::AM_StandardDlg(QWidget* parent, const char* name)
    : QDialog(parent, name, true),   /* 模态对话框 */
      m_pVMainLayout(0),
      m_pVSubLayout(0),
      m_pHBtnLayout(0),
      m_pCancelBtn(0),               /* 0xDC */
      m_pOkBtn(0),                   /* 0xE0 */
      m_nSelect(0)
{
    init();
    preInit();
    postInit();
}

/* ============ 析构函数（虚） ============ */
AM_StandardDlg::~AM_StandardDlg()
{
    /* Qt 父子关系负责子对象清理 */
}

/* ============ init ============ */
/* 反编译为空函数 */
void AM_StandardDlg::init()
{
}

/* ============ preInit ============ */
/* 反编译：
     0xD0 = new QVBoxLayout(this, 0, -1, 0)
     0xD4 = new QVBoxLayout(0xD0, -1, 0) */
void AM_StandardDlg::preInit()
{
    m_pVMainLayout = new QVBoxLayout(this, 0, -1, 0);
    m_pVSubLayout  = new QVBoxLayout(m_pVMainLayout, -1, 0);
}

/* ============ postInit ============ */
/* 反编译 0x41b0dd90 完整逻辑：
   1) this+0xd8 = new QHBoxLayout(this+0xd0, -1, 0)
   2) this+0xe0 = new UTIL_PushButton(this, 2, -1, -1)  ← OK 按钮 (flag=2 JoinRight)
      tr(TXT_RID_D2BUTTON_OK) → setText()
   3) this+0xdc = new UTIL_PushButton(this, 1, -1, -1)  ← Cancel 按钮 (flag=1 JoinLeft)
      tr(TXT_RID_D2BUTTON_CANCEL) → setText()
   4) addWidget(cancel@0xdc) / addWidget(ok@0xe0) → 先Cancel后OK 加入 HBtnLayout
   5) connect(cancel, clicked(), this, slotCancel()); connect(ok, clicked(), this, slotOk())
   6) 虚表 this+0x4c(this,2)  ← 原库基类虚方法（带参数2）
   7) ZGlobal::getDesktopRect() → setFixedWidth(desktop.width())
      ZGlobal::getGNBRect() → 计算 desktop.height()-gnb.height()+3 → setFixedHeight
      虚表 this+0xb4(this,0,h) ← 底部定位（move 语义）

   ABI 注记：原库 AM_StandardDlg 继承 UTIL_Dialog(ZBaseDialog)，而本重建头文件
   继承 QDialog（重建基类链不同）。原库虚表槽 this+0x4c / this+0xb4 在 QDialog
   虚表布局下偏移不同，无法在不臆造基类链的前提下 1:1 复刻这两处虚调用。
   故忠实还原其余全部逻辑（按钮/布局/信号/ZGlobal 尺寸计算），虚表调用以
   语义等价的 QWidget API 表达：move() 为 QWidget 虚方法，行为等价于底部定位。 */
void AM_StandardDlg::postInit()
{
    /* 1) 按钮栏水平布局 */
    m_pHBtnLayout = new QHBoxLayout(m_pVMainLayout, -1, 0);

    /* 2) OK 按钮：flag=2(JoinRight)，文本 TXT_RID_D2BUTTON_OK */
    m_pOkBtn = new UTIL_PushButton(this, 2, -1, -1);
    m_pOkBtn->setText(tr("TXT_RID_D2BUTTON_OK"));

    /* 3) Cancel 按钮：flag=1(JoinLeft)，文本 TXT_RID_D2BUTTON_CANCEL */
    m_pCancelBtn = new UTIL_PushButton(this, 1, -1, -1);
    m_pCancelBtn->setText(tr("TXT_RID_D2BUTTON_CANCEL"));

    /* 4) Cancel 先加入（左），OK 后（右） */
    m_pHBtnLayout->addWidget(m_pCancelBtn, 0);
    m_pHBtnLayout->addWidget(m_pOkBtn, 0);

    /* 5) 连接信号槽：Cancel→slotCancel，OK→slotOk */
    connect(m_pCancelBtn, SIGNAL(clicked()), this, SLOT(slotCancel()));
    connect(m_pOkBtn,     SIGNAL(clicked()), this, SLOT(slotOk()));

    /* 7) 依 ZGlobal 桌面/GNB 矩形设置固定尺寸并底部定位 */
    QRect desktop = ZGlobal::getDesktopRect();
    setFixedWidth(desktop.width());
    QRect gnb = ZGlobal::getGNBRect();
    int bottomH = desktop.height() - gnb.height() + 3;
    setFixedHeight(bottomH);
    /* 虚表 this+0xb4(this,0,bottomH) = 底部定位，move() 语义等价 */
    move(desktop.x(), bottomH);
}

/* ============ getMainLayout ============ */
QLayout* AM_StandardDlg::getMainLayout()
{
    return m_pVSubLayout;   /* 返回 0xD4 */
}

/* ============ slotOk ============ */
/* 反编译：m_nSelect=1; QDialog::accept() */
void AM_StandardDlg::slotOk()
{
    m_nSelect = 1;
    accept();
}

/* ============ slotCancel ============ */
/* 反编译：m_nSelect=0; QDialog::reject() */
void AM_StandardDlg::slotCancel()
{
    m_nSelect = 0;
    reject();
}

/* ============ getSelect ============ */
/* 反编译：return m_nSelect */
int AM_StandardDlg::getSelect()
{
    return m_nSelect;
}

#include "AM_StandardDlg.moc"

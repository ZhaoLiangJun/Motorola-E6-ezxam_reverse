/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 虚表分析）
AM_GroupListScrollView : public QListView（Q_OBJECT）
  虚表 slot: 析构 thunk 在 -36/-48（无多重继承，thunk 对应 QListView 虚析构 + QPaintDevice 虚函数）
AM_GroupListScrollViewItem : public QCheckListItem（→ QListViewItem → QListViewItem）
  Item 成员（从 getSelectedGroup）：m_checked @ 0x28(char)、m_groupName @ 0x34(QString)
  Item 构造：QString group, QListView* parent, QString const& text, QCheckListItem::Type
构造：AM_GroupListScrollView(QList<AM_Folder>, QString, QWidget*)
方法：
  getSelectedGroup()：QListViewItemIterator 遍历，找到 isChecked 的项，返回其 text(0)
  slotClicked(item) ：遍历所有 item，设置 checked=item==clicked
虚表证据：_ZTV22AM_GroupListScrollView @ 0x41b22c90
*/
#ifndef AM_GROUPLISTSCROLLVIEW
#define AM_GROUPLISTSCROLLVIEW

#include <qobject.h>
#include <qlistview.h>
#include <qstring.h>
#include <qlist.h>
#include <AM_Folder.h>

/* ============ AM_GroupListScrollViewItem ============ */
class AM_GroupListScrollViewItem : public QCheckListItem
{
public:
    AM_GroupListScrollViewItem(QString group,
                               QListView* parent,
                               const QString& text,
                               QCheckListItem::Type type);
    ~AM_GroupListScrollViewItem();

    bool isChecked() const { return m_checked; }
    void setChecked(bool checked) { m_checked = checked; }
    QString groupName() const { return m_groupName; }

private:
    bool    m_checked;      /* 0x28 */
    QString m_groupName;    /* 0x34 */
};

/* ============ AM_GroupListScrollView ============ */
class AM_GroupListScrollView : public QListView
{
    Q_OBJECT
public :
    AM_GroupListScrollView(QList<AM_Folder> folders, QString title, QWidget* parent);
    ~AM_GroupListScrollView();

    QString  getSelectedGroup();

public slots:
    /* v32：原库元对象槽表仅 1 项 slotClicked(QListViewItem*)
       （_ZN22AM_GroupListScrollView16staticMetaObjectEv @0x41b17a38）。 */
    void     slotClicked(QListViewItem* item);

private :
    QList<AM_Folder> m_folders;         /* 文件夹列表 */
    QString          m_title;           /* 对话框标题 */
    QString          m_selectedGroup;   /* 当前选中组名 */
};

#endif

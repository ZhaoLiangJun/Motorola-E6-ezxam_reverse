/* AM_GroupListScrollView.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   继承 QListView；Item 继承 QCheckListItem。
   getSelectedGroup：QListViewItemIterator 遍历，找 isChecked 的项，返回其 groupName。
   slotClicked：遍历所有 item，setChecked(item==clicked)。
*/
#include "AM_GroupListScrollView.h"

#include <qlistview.h>
#include <qlist.h>
#include <qstring.h>
#include <qheader.h>
#include <AM_Global.h>   /* AM_Global::CMCCFlexBitEnable() 平台静态方法 */

/* ============ AM_GroupListScrollViewItem 构造 ============ */
AM_GroupListScrollViewItem::AM_GroupListScrollViewItem(
    QString group,
    QListView* parent,
    const QString& text,
    QCheckListItem::Type type)
    : QCheckListItem(parent, text, type),
      m_checked(false),
      m_groupName(group)
{
}

/* ============ AM_GroupListScrollViewItem 析构 ============ */
AM_GroupListScrollViewItem::~AM_GroupListScrollViewItem()
{
}

/* ============ AM_GroupListScrollView 构造 ============ */
/* 反编译 0x41b0e1f8：
   QListView(parent, 0);
   addColumn(<colTitle rodata>);
   setAllColumnsShowFocus(true); setHScrollBarMode(AlwaysOff=1);
   setSelectionMode(Multi=3); setSorting(true);
   header()->vcall(0xa0/4);
   遍历 folders：
     if (!AM_Global::CMCCFlexBitEnable()) {   // 仅 CMCC flex bit 关闭时建条目
        item = new AM_GroupListScrollViewItem(folder->getName(), this, <colTitle>, 0);
        item->setText(0, obj->getName());
        if (title == folderName) item->setChecked(true);
        排序：while(itemBelow) moveItem(item);
     }
   connect(this, clicked(QListViewItem*), this, slotClicked(QListViewItem*)); */
AM_GroupListScrollView::AM_GroupListScrollView(
    QList<AM_Folder> folders,
    QString title,
    QWidget* parent)
    : QListView(parent),
      m_folders(folders),
      m_title(title),
      m_selectedGroup()
{
    addColumn(title);
    setAllColumnsShowFocus(true);
    setHScrollBarMode(QScrollView::AlwaysOff);
    setSelectionMode(QListView::Multi);
    setSorting(true);
    if (header())
        header()->setClickEnabled(true);

    /* 关键：忠实调用平台 AM_Global::CMCCFlexBitEnable()，
       仅当返回 false 时为每个 folder 创建选择条目（反编译第49行）。 */
    for (AM_Folder* folder = m_folders.first(); folder; folder = m_folders.next()) {
        if (AM_Global::CMCCFlexBitEnable())
            continue;   /* CMCC flex bit 开启则跳过建条目 */
        QString groupName = folder->getName();
        AM_GroupListScrollViewItem* item =
            new AM_GroupListScrollViewItem(groupName, this, title, QCheckListItem::CheckBox);
        item->setText(0, groupName);
        if (title == groupName)
            item->setChecked(true);
        /* 反编译 96-99 行：while(itemBelow) item->moveItem(itemBelow)
           把新建条目移到列表底部（保持插入顺序） */
        while (QListViewItem* below = item->itemBelow())
            item->moveItem(below);
    }

    connect(this, SIGNAL(clicked(QListViewItem*)),
            this, SLOT(slotClicked(QListViewItem*)));
}

/* ============ AM_GroupListScrollView 析构 ============ */
AM_GroupListScrollView::~AM_GroupListScrollView()
{
}

/* ============ getSelectedGroup ============ */
/* 反编译：QListViewItemIterator 遍历，
     若 item->isChecked() 则返回 item->groupName()。*/
QString AM_GroupListScrollView::getSelectedGroup()
{
    QListViewItemIterator it(this);
    while (QListViewItem* item = it.current()) {
        if (static_cast<AM_GroupListScrollViewItem*>(item)->isChecked()) {
            m_selectedGroup = static_cast<AM_GroupListScrollViewItem*>(item)->groupName();
            return m_selectedGroup;
        }
        ++it;
    }
    return QString::null;
}

/* ============ slotClicked ============ */
/* 反编译：遍历所有 item，setChecked(item==clicked)。*/
void AM_GroupListScrollView::slotClicked(QListViewItem* item)
{
    if (item == 0)
        return;

    QListViewItemIterator it(this);
    while (QListViewItem* cur = it.current()) {
        static_cast<AM_GroupListScrollViewItem*>(cur)->setChecked(cur == item);
        ++it;
    }
}

#include "AM_GroupListScrollView.moc"

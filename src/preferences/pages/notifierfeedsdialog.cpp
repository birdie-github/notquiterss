// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifierfeedsdialog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHeaderView>
#include <QQueue>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVector>
#include <utility>

namespace {
QString optTr(const char *text)
{
  return QCoreApplication::translate("OptionsDialog", text);
}
} // namespace

NotifierFeedsDialog::NotifierFeedsDialog(QWidget *parent)
  : Dialog(parent)
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(optTr("Selection of feeds"));
  setMinimumSize(300, 300);

  feedsTreeNotify_ = new QTreeWidget(this);
  feedsTreeNotify_->setObjectName(QStringLiteral("feedsTreeNotify_"));
  feedsTreeNotify_->setColumnCount(2);
  feedsTreeNotify_->setColumnHidden(1, true);
  feedsTreeNotify_->header()->hide();
  feedsTreeNotify_->setHeaderLabels({QStringLiteral("Feeds"), QStringLiteral("Id")});

  auto *allFeeds = new QTreeWidgetItem(QStringList{optTr("All Feeds"), QStringLiteral("0")});
  allFeeds->setCheckState(0, Qt::Checked);
  feedsTreeNotify_->addTopLevelItem(allFeeds);

  pageLayout->addWidget(feedsTreeNotify_);
  buttonBox->addButton(QDialogButtonBox::Close);

  connect(feedsTreeNotify_, &QTreeWidget::itemChanged,
          this, &NotifierFeedsDialog::itemChanged);
}

void NotifierFeedsDialog::load(QSqlDatabase &db, bool defaultIconFeeds)
{
  if (loadNotifierOk_) return;
  QElapsedTimer timer;
  timer.start();
  loadNotifierOk_ = true;
  itemNotChecked_ = true;

  QHash<int, int> notifyByFeed;
  {
    QSqlQuery qNotify(db);
    qNotify.setForwardOnly(true);
    if (qNotify.exec(QStringLiteral("SELECT feedId, value FROM feeds_ex WHERE name='showNotification'"))) {
      while (qNotify.next()) {
        notifyByFeed.insert(qNotify.value(0).toInt(), qNotify.value(1).toInt());
      }
    }
  }

  QVector<int> missingFeedIds;
  QSqlQuery q(db);
  QQueue<int> parentIds;
  parentIds.enqueue(0);
  while (!parentIds.empty()) {
    const int parentId = parentIds.dequeue();
    const QString qStr = QStringLiteral("SELECT text, id, image, xmlUrl FROM feeds WHERE parentId='%1' ORDER BY rowToParent")
        .arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      const QString feedText = q.value(0).toString();
      const QString feedId = q.value(1).toString();
      const int feedIdInt = feedId.toInt();
      const QByteArray byteArray = q.value(2).toByteArray();
      const QString xmlUrl = q.value(3).toString();

      auto *treeWidgetItem = new QTreeWidgetItem(QStringList{feedText, feedId});
      treeWidgetItem->setCheckState(0, Qt::Unchecked);
      auto it = notifyByFeed.constFind(feedIdInt);
      if (it != notifyByFeed.constEnd()) {
        if (it.value() == 1) treeWidgetItem->setCheckState(0, Qt::Checked);
      } else {
        missingFeedIds.append(feedIdInt);
      }
      if (treeWidgetItem->checkState(0) == Qt::Unchecked) {
        feedsTreeNotify_->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
      }

      QPixmap iconItem;
      if (xmlUrl.isEmpty()) {
        iconItem.load(QStringLiteral(":/images/folder"));
      } else if (byteArray.isNull() || defaultIconFeeds) {
        iconItem.load(QStringLiteral(":/images/feed"));
      } else {
        iconItem.loadFromData(QByteArray::fromBase64(byteArray));
      }
      treeWidgetItem->setIcon(0, iconItem);

      const QList<QTreeWidgetItem *> treeItems = feedsTreeNotify_->findItems(
          QString::number(parentId), Qt::MatchFixedString | Qt::MatchRecursive, 1);
      treeItems.at(0)->addChild(treeWidgetItem);
      if (xmlUrl.isEmpty()) parentIds.enqueue(feedIdInt);
    }
  }

  if (!missingFeedIds.isEmpty()) {
    db.transaction();
    QSqlQuery qInsert(db);
    qInsert.prepare(QStringLiteral("INSERT INTO feeds_ex(feedId, name, value) VALUES (?, 'showNotification', '0')"));
    for (int feedIdInt : std::as_const(missingFeedIds)) {
      qInsert.addBindValue(feedIdInt);
      qInsert.exec();
    }
    db.commit();
  }
  feedsTreeNotify_->expandAll();
  itemNotChecked_ = false;
  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::loadNotifier: %1 ms").arg(timer.elapsed());
}

void NotifierFeedsDialog::apply(QSqlDatabase &db)
{
  if (!loadNotifierOk_) return;

  feedsTreeNotify_->expandAll();
  QTreeWidgetItem *treeWidgetItem =
      feedsTreeNotify_->itemBelow(feedsTreeNotify_->topLevelItem(0));
  db.transaction();
  while (treeWidgetItem) {
    const int check = treeWidgetItem->checkState(0) == Qt::Checked ? 1 : 0;
    QSqlQuery q(db);
    const QString qStr = QStringLiteral("UPDATE feeds_ex SET value='%1' WHERE feedId='%2' AND name='showNotification'")
        .arg(check).arg(treeWidgetItem->text(1).toInt());
    q.exec(qStr);
    treeWidgetItem = feedsTreeNotify_->itemBelow(treeWidgetItem);
  }
  db.commit();
}

void NotifierFeedsDialog::itemChanged(QTreeWidgetItem *item, int column)
{
  if (column != 0 || itemNotChecked_) return;

  itemNotChecked_ = true;
  if (item->checkState(0) == Qt::Unchecked) {
    setCheckStateRecursive(item, Qt::Unchecked);
    QTreeWidgetItem *parentItem = item->parent();
    while (parentItem) {
      parentItem->setCheckState(0, Qt::Unchecked);
      parentItem = parentItem->parent();
    }
  } else {
    setCheckStateRecursive(item, Qt::Checked);
  }
  itemNotChecked_ = false;
}

void NotifierFeedsDialog::setCheckStateRecursive(QTreeWidgetItem *item, Qt::CheckState state)
{
  for (int i = 0; i < item->childCount(); ++i) {
    QTreeWidgetItem *childItem = item->child(i);
    childItem->setCheckState(0, state);
    setCheckStateRecursive(childItem, state);
  }
}

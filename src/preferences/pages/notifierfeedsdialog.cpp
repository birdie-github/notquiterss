// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifierfeedsdialog.h"
#include "feeds/feedselectiontree.h"
#include "preferences/optionsdialog.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVector>
#include <utility>

NotifierFeedsDialog::NotifierFeedsDialog(QWidget *parent)
  : Dialog(parent)
{
  setWindowTitle(OptionsDialog::tr("Selection of feeds"));
  setMinimumSize(300, 300);

  feedsTreeNotify_ = new QTreeWidget(this);
  FeedSelectionTree::configure(feedsTreeNotify_, QStringLiteral("feedsTreeNotify_"));

  pageLayout->addWidget(feedsTreeNotify_);
  buttonBox->addButton(QDialogButtonBox::Close);

  connect(feedsTreeNotify_, &QTreeWidget::itemChanged,
          this, [this](QTreeWidgetItem *item, int column) {
    if (column == 0) FeedSelectionTree::cascadeChecks(feedsTreeNotify_, item);
  });
}

void NotifierFeedsDialog::load(QSqlDatabase &db, bool defaultIconFeeds)
{
  if (loadNotifierOk_) return;
  QElapsedTimer timer;
  timer.start();
  loadNotifierOk_ = true;

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

  QString treeError;
  if (!FeedSelectionTree::populate(feedsTreeNotify_, db, defaultIconFeeds,
                                   OptionsDialog::tr("All Feeds"), -1, treeError)) {
    qWarning().noquote() << treeError;
  }

  QVector<int> missingFeedIds;
  {
    const QSignalBlocker blocker(feedsTreeNotify_);
    bool allChecked = true;
    QTreeWidgetItem *root = feedsTreeNotify_->topLevelItem(0);
    for (QTreeWidgetItemIterator it(feedsTreeNotify_); *it; ++it) {
      QTreeWidgetItem *item = *it;
      if (item == root) continue;
      const int feedIdInt = item->text(1).toInt();
      const auto found = notifyByFeed.constFind(feedIdInt);
      const bool checked = (found != notifyByFeed.constEnd() && found.value() == 1);
      if (found == notifyByFeed.constEnd()) missingFeedIds.append(feedIdInt);
      item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
      if (!checked) allChecked = false;
    }
    if (root) root->setCheckState(0, allChecked ? Qt::Checked : Qt::Unchecked);
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

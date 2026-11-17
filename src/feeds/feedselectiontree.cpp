// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedselectiontree.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalBlocker>
#include <QQueue>
#include <QSet>

namespace FeedSelectionTree {
bool populate(QTreeWidget *tree, QSqlDatabase db, bool defaultIcons,
              const QString &rootTitle, int precheckedFeed, QString &error)
{
  const QSignalBlocker blocker(tree);
  tree->clear();
  error.clear();
  auto *root = new QTreeWidgetItem(tree, {rootTitle, QStringLiteral("0")});
  root->setCheckState(0, Qt::Unchecked);
  root->setData(0, IsFeedRole, false);
  QQueue<QTreeWidgetItem *> parents;
  QSet<int> seen{0};
  parents.enqueue(root);
  QSqlQuery q(db);
  if (!q.prepare("SELECT text, id, image, xmlUrl FROM feeds WHERE parentId = ? ORDER BY rowToParent")) {
    error = q.lastError().text();
    return false;
  }
  while (!parents.isEmpty()) {
    auto *parent = parents.dequeue();
    const int parentId = parent->text(1).toInt();
    q.bindValue(0, parentId);
    if (!q.exec()) {
      error = q.lastError().text();
      return false;
    }
    while (q.next()) {
      const int id = q.value(1).toInt();
      if (seen.contains(id)) continue;
      seen.insert(id);
      const bool isFeed = !q.value(3).toString().isEmpty();
      auto *item = new QTreeWidgetItem(parent, {q.value(0).toString(), QString::number(id)});
      item->setData(0, IsFeedRole, isFeed);
      item->setCheckState(0, precheckedFeed != -1 &&
          (precheckedFeed == id || precheckedFeed == parentId) ? Qt::Checked : Qt::Unchecked);
      QPixmap icon;
      if (!isFeed) icon.load(":/images/folder");
      else if (q.value(2).isNull() || defaultIcons) icon.load(":/images/feed");
      else icon.loadFromData(QByteArray::fromBase64(q.value(2).toByteArray()));
      item->setIcon(0, icon);
      if (!isFeed) parents.enqueue(item);
    }
    if (q.lastError().isValid()) {
      error = q.lastError().text();
      return false;
    }
    q.finish();
  }
  tree->expandAll();
  return true;
}

QList<int> checkedFeeds(QTreeWidget *tree)
{
  QList<int> ids;
  for (QTreeWidgetItemIterator it(tree); *it; ++it) {
    if ((*it)->data(0, IsFeedRole).toBool() && (*it)->checkState(0) == Qt::Checked)
      ids.append((*it)->text(1).toInt());
  }
  return ids;
}

void updateChecks(QTreeWidget *tree, QTreeWidgetItem *changed)
{
  const QSignalBlocker blocker(tree);
  if (!changed->data(0, IsFeedRole).toBool()) {
    const auto state = changed->checkState(0) == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
    QQueue<QTreeWidgetItem *> queue;
    queue.enqueue(changed);
    while (!queue.isEmpty()) {
      auto *item = queue.dequeue();
      if (item->data(0, IsFeedRole).toBool()) item->setCheckState(0, state);
      for (int i = 0; i < item->childCount(); ++i) queue.enqueue(item->child(i));
    }
  }
  // Sum actual feed leaves bottom-up. Empty folders do not affect a parent's state.
  QList<QTreeWidgetItem *> items;
  for (QTreeWidgetItemIterator it(tree); *it; ++it) items.append(*it);
  QHash<QTreeWidgetItem *, QPair<int, int>> counts;
  for (int i = items.size() - 1; i >= 0; --i) {
    auto *item = items.at(i);
    int total = 0, checked = 0;
    if (item->data(0, IsFeedRole).toBool()) {
      total = 1;
      checked = item->checkState(0) == Qt::Checked ? 1 : 0;
    } else {
      for (int j = 0; j < item->childCount(); ++j) {
        const auto child = counts.value(item->child(j));
        total += child.first;
        checked += child.second;
      }
      item->setCheckState(0, checked == 0 ? Qt::Unchecked :
                         (checked == total ? Qt::Checked : Qt::PartiallyChecked));
    }
    counts.insert(item, qMakePair(total, checked));
  }
}
}

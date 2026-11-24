// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedselectiontree.h"
#include <QHeaderView>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalBlocker>
#include <QQueue>
#include <QSet>

namespace FeedSelectionTree {
namespace {
enum class PopulateMode { FeedsAndFolders, FoldersOnly };

bool populateTree(QTreeWidget *tree, const QSqlDatabase &db, bool defaultIcons,
                  const QString &rootTitle, int targetId, PopulateMode mode, QString &error)
{
  const QSignalBlocker blocker(tree);
  tree->clear();
  error.clear();
  auto *root = new QTreeWidgetItem(tree, {rootTitle, QStringLiteral("0")});
  root->setData(0, IsFeedRole, false);
  if (mode == PopulateMode::FoldersOnly) {
    root->setIcon(0, QIcon(QStringLiteral(":/images/folder")));
    tree->setCurrentItem(root);
  } else {
    root->setCheckState(0, Qt::Unchecked);
  }

  QQueue<QTreeWidgetItem *> parents;
  QSet<int> seen{0};
  parents.enqueue(root);
  QSqlQuery q(db);
  const QString sql = (mode == PopulateMode::FoldersOnly)
      ? QStringLiteral("SELECT text, id, '' AS image, '' AS xmlUrl FROM feeds "
                       "WHERE parentId = ? AND (xmlUrl = '' OR xmlUrl IS NULL)")
      : QStringLiteral("SELECT text, id, image, xmlUrl FROM feeds "
                       "WHERE parentId = ? ORDER BY rowToParent");
  if (!q.prepare(sql)) {
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
      const bool isFeed = (mode == PopulateMode::FeedsAndFolders) && !q.value(3).toString().isEmpty();
      auto *item = new QTreeWidgetItem(parent, {q.value(0).toString(), QString::number(id)});
      item->setData(0, IsFeedRole, isFeed);
      if (mode == PopulateMode::FoldersOnly) {
        item->setIcon(0, QIcon(QStringLiteral(":/images/folder")));
        if (id == targetId) tree->setCurrentItem(item);
        parents.enqueue(item);
      } else {
        item->setCheckState(0, targetId != -1 && (targetId == id || targetId == parentId)
                            ? Qt::Checked : Qt::Unchecked);
        QPixmap icon;
        if (!isFeed) icon.load(QStringLiteral(":/images/folder"));
        else if (q.value(2).isNull() || defaultIcons) icon.load(QStringLiteral(":/images/feed"));
        else icon.loadFromData(QByteArray::fromBase64(q.value(2).toByteArray()));
        item->setIcon(0, icon);
        if (!isFeed) parents.enqueue(item);
      }
    }
    if (q.lastError().isValid()) {
      error = q.lastError().text();
      return false;
    }
    q.finish();
  }

  tree->expandAll();
  if (mode == PopulateMode::FoldersOnly)
    tree->sortByColumn(0, Qt::AscendingOrder);
  return true;
}
} // namespace

void configure(QTreeWidget *tree, const QString &objectName, const QStringList &headers)
{
  if (!objectName.isEmpty()) tree->setObjectName(objectName);
  tree->setColumnCount(2);
  tree->setColumnHidden(1, true);
  if (headers.isEmpty()) {
    tree->setHeaderHidden(true);
  } else {
    tree->setHeaderLabels(headers);
    tree->header()->setSectionsMovable(false);
  }
}

bool populate(QTreeWidget *tree, QSqlDatabase db, bool defaultIcons,
              const QString &rootTitle, int precheckedFeed, QString &error)
{
  return populateTree(tree, db, defaultIcons, rootTitle, precheckedFeed,
                      PopulateMode::FeedsAndFolders, error);
}

bool populateFolders(QTreeWidget *tree, QSqlDatabase db,
                     const QString &rootTitle, int currentFolderId, QString &error)
{
  return populateTree(tree, db, false, rootTitle, currentFolderId,
                      PopulateMode::FoldersOnly, error);
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

void cascadeChecks(QTreeWidget *tree, QTreeWidgetItem *changed)
{
  if (!tree || !changed) return;
  const QSignalBlocker blocker(tree);
  const Qt::CheckState state =
      (changed->checkState(0) == Qt::Unchecked) ? Qt::Unchecked : Qt::Checked;
  QQueue<QTreeWidgetItem *> queue;
  for (int i = 0; i < changed->childCount(); ++i)
    queue.enqueue(changed->child(i));
  while (!queue.isEmpty()) {
    QTreeWidgetItem *item = queue.dequeue();
    item->setCheckState(0, state);
    for (int i = 0; i < item->childCount(); ++i)
      queue.enqueue(item->child(i));
  }
  if (state == Qt::Unchecked) {
    for (QTreeWidgetItem *parent = changed->parent(); parent; parent = parent->parent())
      parent->setCheckState(0, Qt::Unchecked);
  }
}
}

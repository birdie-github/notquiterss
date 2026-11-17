// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDSELECTIONTREE_H
#define FEEDSELECTIONTREE_H
#include <QSqlDatabase>
#include <QTreeWidget>

namespace FeedSelectionTree {
constexpr int IsFeedRole = Qt::UserRole;
bool populate(QTreeWidget *tree, QSqlDatabase db, bool defaultIcons,
              const QString &rootTitle, int precheckedFeed, QString &error);
QList<int> checkedFeeds(QTreeWidget *tree);
// Bulk selection only: Filter Rules retains its existing checkbox semantics.
void updateChecks(QTreeWidget *tree, QTreeWidgetItem *changed);
}
#endif

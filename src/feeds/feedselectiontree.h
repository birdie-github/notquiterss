// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDSELECTIONTREE_H
#define FEEDSELECTIONTREE_H
#include <QSqlDatabase>
#include <QStringList>
#include <QTreeWidget>

namespace FeedSelectionTree {
constexpr int IsFeedRole = Qt::UserRole;
void configure(QTreeWidget *tree, const QString &objectName = QString(),
               const QStringList &headers = {});
bool populate(QTreeWidget *tree, QSqlDatabase db, bool defaultIcons,
              const QString &rootTitle, int precheckedFeed, QString &error);
bool populateFolders(QTreeWidget *tree, QSqlDatabase db,
                     const QString &rootTitle, int currentFolderId, QString &error);
QList<int> checkedFeeds(QTreeWidget *tree);
// Tri-state folder rollup for bulk feed selection.
void updateChecks(QTreeWidget *tree, QTreeWidgetItem *changed);
// Propagate check state to descendants and clear ancestors on uncheck.
void cascadeChecks(QTreeWidget *tree, QTreeWidgetItem *changed);
}
#endif

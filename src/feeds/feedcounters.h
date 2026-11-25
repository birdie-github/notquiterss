// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDCOUNTERS_H
#define FEEDCOUNTERS_H

#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

namespace FeedCounters {

struct FolderCounts {
  int feedId = 0;
  int unreadCount = 0;
  int newCount = 0;
  int undeleteCount = 0;
  QString updated;
};

// Count live (non-deleted), unread, and new articles for a single feed in one scan.
bool countFeedArticles(QSqlDatabase &db, int feedId,
                       int &undeleteCount, int &unreadCount, int &newCount,
                       QSqlError *sqlError = nullptr, QString *error = nullptr);

// Recalculate parent folder counters bottom-up across all affected ancestors.
bool rollupParents(QSqlDatabase &db, const QList<int> &startFolderIds,
                   bool includeUpdated, bool requireParentExists,
                   QList<FolderCounts> *updatedFolders = nullptr,
                   QSqlError *sqlError = nullptr, QString *error = nullptr);

} // namespace FeedCounters

#endif // FEEDCOUNTERS_H

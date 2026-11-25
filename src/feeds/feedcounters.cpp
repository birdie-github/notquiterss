// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedcounters.h"

#include <QCoreApplication>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QSqlQuery>
#include <QVariant>

namespace {
void recordQueryError(const QSqlQuery &query, const QString &fallbackError,
                      QSqlError *sqlError, QString *error)
{
  const QSqlError queryErr = query.lastError();
  if (sqlError) *sqlError = queryErr;
  if (error) {
    *error = queryErr.isValid() ? queryErr.text() : fallbackError;
  }
}
} // namespace

bool FeedCounters::countFeedArticles(QSqlDatabase &db, int feedId,
                                     int &undeleteCount, int &unreadCount, int &newCount,
                                     QSqlError *sqlError, QString *error)
{
  if (sqlError) *sqlError = QSqlError();
  if (error) error->clear();

  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (!q.prepare(QStringLiteral(
          "SELECT count(*), COALESCE(sum(read=0), 0), COALESCE(sum(new=1), 0) "
          "FROM news WHERE feedId=? AND deleted=0"))) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }
  q.addBindValue(feedId);
  if (!q.exec() || !q.next()) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }
  undeleteCount = q.value(0).toInt();
  unreadCount = q.value(1).toInt();
  newCount = q.value(2).toInt();
  if (q.lastError().isValid()) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }
  return true;
}

bool FeedCounters::rollupParents(QSqlDatabase &db, const QList<int> &startFolderIds,
                                 bool includeUpdated, bool requireParentExists,
                                 QList<FolderCounts> *updatedFolders,
                                 QSqlError *sqlError, QString *error)
{
  if (updatedFolders) updatedFolders->clear();
  if (sqlError) *sqlError = QSqlError();
  if (error) error->clear();
  if (startFolderIds.isEmpty()) return true;

  QHash<int, int> parentOf;
  QHash<int, int> pendingChildren;
  {
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.prepare(QStringLiteral("SELECT parentId FROM feeds WHERE id=?"))) {
      recordQueryError(q, QString(), sqlError, error);
      return false;
    }

    for (int startId : startFolderIds) {
      int cur = startId;
      QSet<int> visiting;
      while (cur > 0) {
        if (visiting.contains(cur)) {
          if (error) {
            *error = QCoreApplication::translate(
                "FeedDeletion", "The feed folder hierarchy contains a cycle.");
          }
          return false;
        }
        if (parentOf.contains(cur)) break;
        visiting.insert(cur);

        q.addBindValue(cur);
        if (!q.exec()) {
          recordQueryError(q, QString(), sqlError, error);
          return false;
        }
        int next = 0;
        if (q.next()) {
          next = q.value(0).toInt();
        } else if (q.lastError().isValid()) {
          recordQueryError(q, QString(), sqlError, error);
          return false;
        } else if (requireParentExists) {
          if (error) {
            *error = QCoreApplication::translate(
                "FeedDeletion", "A parent folder no longer exists.");
          }
          return false;
        } else {
          break;
        }
        q.finish();

        parentOf.insert(cur, next);
        if (!pendingChildren.contains(cur)) pendingChildren.insert(cur, 0);
        if (next > 0) pendingChildren[next] = pendingChildren.value(next, 0) + 1;
        cur = next;
      }
    }
  }

  if (parentOf.isEmpty()) return true;

  QQueue<int> ready;
  for (auto it = pendingChildren.cbegin(); it != pendingChildren.cend(); ++it) {
    if (it.value() == 0) ready.enqueue(it.key());
  }

  QSqlQuery sumQuery(db);
  sumQuery.setForwardOnly(true);
  const QString sumSql = includeUpdated
      ? QStringLiteral("SELECT sum(unread), sum(newCount), sum(undeleteCount), max(updated) "
                       "FROM feeds WHERE parentId=?")
      : QStringLiteral("SELECT sum(unread), sum(newCount), sum(undeleteCount) "
                       "FROM feeds WHERE parentId=?");
  if (!sumQuery.prepare(sumSql)) {
    recordQueryError(sumQuery, QString(), sqlError, error);
    return false;
  }

  QSqlQuery updateQuery(db);
  updateQuery.setForwardOnly(true);
  const QString updateSql = includeUpdated
      ? QStringLiteral("UPDATE feeds SET unread=?, newCount=?, undeleteCount=?, updated=? WHERE id=?")
      : QStringLiteral("UPDATE feeds SET unread=?, newCount=?, undeleteCount=? WHERE id=?");
  if (!updateQuery.prepare(updateSql)) {
    recordQueryError(updateQuery, QString(), sqlError, error);
    return false;
  }

  while (!ready.isEmpty()) {
    const int folderId = ready.dequeue();
    sumQuery.addBindValue(folderId);
    if (!sumQuery.exec()) {
      recordQueryError(sumQuery, QString(), sqlError, error);
      return false;
    }
    if (!sumQuery.next()) {
      recordQueryError(sumQuery,
                       QCoreApplication::translate("FeedDeletion", "Cannot recount the parent folder."),
                       sqlError, error);
      return false;
    }

    const int unreadCount = sumQuery.value(0).toInt();
    const int newCount = sumQuery.value(1).toInt();
    const int undeleteCount = sumQuery.value(2).toInt();
    const QString updated = includeUpdated ? sumQuery.value(3).toString() : QString();
    sumQuery.finish();

    updateQuery.addBindValue(unreadCount);
    updateQuery.addBindValue(newCount);
    updateQuery.addBindValue(undeleteCount);
    if (includeUpdated) updateQuery.addBindValue(updated);
    updateQuery.addBindValue(folderId);
    if (!updateQuery.exec()) {
      recordQueryError(updateQuery, QString(), sqlError, error);
      return false;
    }
    updateQuery.finish();

    if (updatedFolders) {
      updatedFolders->append({folderId, unreadCount, newCount, undeleteCount, updated});
    }

    const int next = parentOf.value(folderId, 0);
    if (next > 0 && --pendingChildren[next] == 0) {
      ready.enqueue(next);
    }
  }

  return true;
}

bool FeedCounters::siblingIds(QSqlDatabase &db, int parentId, QList<int> &siblingIds,
                              int excludeFeedId,
                              QSqlError *sqlError, QString *error)
{
  siblingIds.clear();
  if (sqlError) *sqlError = QSqlError();
  if (error) error->clear();

  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (!q.prepare(QStringLiteral("SELECT id FROM feeds WHERE parentId=? ORDER BY rowToParent"))) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }
  q.addBindValue(parentId);
  if (!q.exec()) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }

  while (q.next()) {
    const int id = q.value(0).toInt();
    if (id != excludeFeedId)
      siblingIds.append(id);
  }
  if (q.lastError().isValid()) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }

  return true;
}

bool FeedCounters::writeSiblingOrder(QSqlDatabase &db, const QList<int> &feedIds,
                                     QSqlError *sqlError, QString *error)
{
  if (sqlError) *sqlError = QSqlError();
  if (error) error->clear();
  if (feedIds.isEmpty())
    return true;

  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("UPDATE feeds SET rowToParent=? WHERE id=?"))) {
    recordQueryError(q, QString(), sqlError, error);
    return false;
  }

  for (int row = 0; row < feedIds.size(); ++row) {
    q.addBindValue(row);
    q.addBindValue(feedIds.at(row));
    if (!q.exec()) {
      recordQueryError(q, QString(), sqlError, error);
      return false;
    }
  }

  return true;
}

bool FeedCounters::renumberSiblings(QSqlDatabase &db, int parentId, int excludeFeedId,
                                    QSqlError *sqlError, QString *error)
{
  QList<int> ids;
  if (!siblingIds(db, parentId, ids, excludeFeedId, sqlError, error))
    return false;
  return writeSiblingOrder(db, ids, sqlError, error);
}

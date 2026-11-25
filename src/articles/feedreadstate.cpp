/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#include "feedreadstate.h"
#include <utility>
#include "database/database.h"
#include "feeds/feedcounters.h"
#include "mainwindow/mainwindow.h"
#include <QQueue>
#include <QSet>
#include <QSqlDriver>
#include <QThread>

FeedReadState::FeedReadState(QObject *parent)
  : QObject(parent)
{
}

void FeedReadState::setDatabase(const QSqlDatabase &database)
{
  Q_ASSERT(QThread::currentThread() == thread());
  Q_ASSERT(database.driver()->thread() == thread());
  db_ = database;
}

void FeedReadState::slotRecountCategoryCounts()
{
  auto databaseAccess = Database::backgroundAccess();
  CategoryCounts counts;
  QSqlQuery q(db_);
  q.setForwardOnly(true);
  if (q.exec("SELECT deleted, starred, read, label FROM news WHERE deleted < 2")) {
    while (q.next()) {
      if (q.value(0).toInt() == 1) {
        ++counts.deleted;
        continue;
      }
      if (q.value(0).toInt() != 0) continue;
      const bool unread = q.value(2).toInt() == 0;
      if (q.value(1).toInt() == 1) {
        ++counts.starred;
        if (unread) ++counts.unreadStarred;
      }
      const QStringList labels = q.value(3).toString().split(',', Qt::SkipEmptyParts);
      for (const QString &label : labels) {
        const int id = label.toInt();
        ++counts.labels[id];
        if (unread) ++counts.unreadLabels[id];
      }
    }
    counts.valid = !q.lastError().isValid();
  }
  if (!counts.valid) qWarning() << "Category recount failed:" << q.lastError();
  // Only the small summary crosses to the UI, not one record per article.
  emit signalRecountCategoryCounts(counts);
}

void FeedReadState::slotRecountFeedCounts(int feedId, bool updateViewport)
{
  auto databaseAccess = Database::backgroundAccess();
  if (!db_.transaction()) {
    qWarning() << "Cannot start feed recount transaction:" << db_.lastError();
    return;
  }

  QList<int> rollupStarts;
  QList<FeedCountStruct> pendingUpdates;
  QSqlError sqlError;
  bool ok = true;
  {
    QSqlQuery q(db_);
    q.setForwardOnly(true);

    int feedParId = 0;
    bool isFolder = false;
    int unreadCountOld = 0;
    int newCountOld = 0;
    int undeleteCountOld = 0;
    if (!q.prepare(QStringLiteral(
            "SELECT parentId, xmlUrl, unread, newCount, undeleteCount FROM feeds WHERE id=?"))) {
      ok = false;
      sqlError = q.lastError();
    } else {
      q.addBindValue(feedId);
      if (!q.exec()) {
        ok = false;
        sqlError = q.lastError();
      } else if (q.next()) {
        feedParId = q.value(0).toInt();
        isFolder = q.value(1).toString().isEmpty();
        unreadCountOld = q.value(2).toInt();
        newCountOld = q.value(3).toInt();
        undeleteCountOld = q.value(4).toInt();
      } else if (q.lastError().isValid()) {
        ok = false;
        sqlError = q.lastError();
      }
    }
    q.finish();

    int undeleteCount = 0;
    int unreadCount = 0;
    int newCount = 0;

    if (ok && !isFolder) {
      if (!FeedCounters::countFeedArticles(db_, feedId, undeleteCount, unreadCount, newCount, &sqlError)) {
        ok = false;
      } else if ((unreadCount != unreadCountOld) || (newCount != newCountOld) ||
                 (undeleteCount != undeleteCountOld)) {
        if (!q.prepare(QStringLiteral("UPDATE feeds SET unread=?, newCount=?, undeleteCount=? WHERE id=?"))) {
          ok = false;
          sqlError = q.lastError();
        } else {
          q.addBindValue(unreadCount);
          q.addBindValue(newCount);
          q.addBindValue(undeleteCount);
          q.addBindValue(feedId);
          if (!q.exec()) {
            ok = false;
            sqlError = q.lastError();
          } else {
            FeedCountStruct counts;
            counts.feedId = feedId;
            counts.unreadCount = unreadCount;
            counts.newCount = newCount;
            counts.undeleteCount = undeleteCount;
            pendingUpdates.append(counts);
            if (feedParId > 0) rollupStarts.append(feedParId);
          }
        }
        q.finish();
      }
    } else if (ok) {
      bool listOk = true;
      const QList<int> idList = getIdFeedsInList(db_, feedId, &listOk, &sqlError);
      if (!listOk) {
        ok = false;
      } else if (!idList.isEmpty()) {
        QSqlQuery selectFeed(db_);
        selectFeed.setForwardOnly(true);
        QSqlQuery updateFeed(db_);
        updateFeed.setForwardOnly(true);
        if (!selectFeed.prepare(QStringLiteral(
                "SELECT parentId, unread, newCount, undeleteCount FROM feeds WHERE id=?"))) {
          ok = false;
          sqlError = selectFeed.lastError();
        } else if (!updateFeed.prepare(QStringLiteral(
                       "UPDATE feeds SET unread=?, newCount=?, undeleteCount=? WHERE id=?"))) {
          ok = false;
          sqlError = updateFeed.lastError();
        } else {
          for (int id : idList) {
            int parId = 0;
            int childUnreadOld = 0;
            int childNewOld = 0;
            int childUndeleteOld = 0;
            selectFeed.addBindValue(id);
            if (!selectFeed.exec()) {
              ok = false;
              sqlError = selectFeed.lastError();
              break;
            }
            if (selectFeed.next()) {
              parId = selectFeed.value(0).toInt();
              childUnreadOld = selectFeed.value(1).toInt();
              childNewOld = selectFeed.value(2).toInt();
              childUndeleteOld = selectFeed.value(3).toInt();
            } else if (selectFeed.lastError().isValid()) {
              ok = false;
              sqlError = selectFeed.lastError();
              break;
            }
            selectFeed.finish();

            if (!FeedCounters::countFeedArticles(db_, id, undeleteCount, unreadCount, newCount, &sqlError)) {
              ok = false;
              break;
            }

            if ((unreadCount == childUnreadOld) && (newCount == childNewOld) &&
                (undeleteCount == childUndeleteOld)) {
              continue;
            }

            updateFeed.addBindValue(unreadCount);
            updateFeed.addBindValue(newCount);
            updateFeed.addBindValue(undeleteCount);
            updateFeed.addBindValue(id);
            if (!updateFeed.exec()) {
              ok = false;
              sqlError = updateFeed.lastError();
              break;
            }
            updateFeed.finish();

            if (parId > 0 && !rollupStarts.contains(parId)) {
              rollupStarts.append(parId);
            }

            FeedCountStruct counts;
            counts.feedId = id;
            counts.unreadCount = unreadCount;
            counts.newCount = newCount;
            counts.undeleteCount = undeleteCount;
            pendingUpdates.append(counts);
          }
        }
      }
    }
  }

  if (ok && !pendingUpdates.isEmpty()) {
    QList<FeedCounters::FolderCounts> parentCounts;
    if (!FeedCounters::rollupParents(db_, rollupStarts, true, false, &parentCounts, &sqlError)) {
      ok = false;
    } else {
      for (const auto &folder : std::as_const(parentCounts)) {
        FeedCountStruct counts;
        counts.feedId = folder.feedId;
        counts.unreadCount = folder.unreadCount;
        counts.newCount = folder.newCount;
        counts.undeleteCount = folder.undeleteCount;
        counts.updated = folder.updated;
        pendingUpdates.append(counts);
      }
    }
  }

  if (!ok) {
    qWarning() << "Feed recount failed:" << sqlError;
    if (!db_.rollback())
      qCritical() << "Feed recount rollback failed:" << db_.lastError();
    return;
  }

  if (!db_.commit()) {
    qWarning() << "Feed recount commit failed:" << db_.lastError();
    if (!db_.rollback())
      qCritical() << "Feed recount rollback failed:" << db_.lastError();
    return;
  }

  if (pendingUpdates.isEmpty()) return;

  for (const auto &counts : std::as_const(pendingUpdates))
    emit feedCountsUpdate(counts);

  if (updateViewport) emit signalFeedsViewportUpdate();
}

QString FeedReadState::getIdFeedsString(const QSqlDatabase &db, int idFolder, int idException)
{
  const QList<int> idList = getIdFeedsInList(db, idFolder);
  QString str;
  for (int id : idList) {
    if (id == idException) continue;
    if (!str.isEmpty()) str.append(QLatin1String(" OR "));
    str.append(QStringLiteral("feedId=%1").arg(id));
  }
  return str.isEmpty() ? QStringLiteral("feedId=-1") : str;
}

QString FeedReadState::getIdFeedsString(int idFolder, int idException)
{
  return getIdFeedsString(db_, idFolder, idException);
}

QList<int> FeedReadState::getIdFeedsInList(const QSqlDatabase &db, int idFolder,
                                           bool *ok, QSqlError *sqlError)
{
  if (ok) *ok = true;
  if (sqlError) *sqlError = QSqlError();

  QList<int> idList;
  if (idFolder <= 0) return idList;

  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (!q.prepare(QStringLiteral("SELECT id, xmlUrl FROM feeds WHERE parentId=?"))) {
    if (ok) *ok = false;
    if (sqlError) *sqlError = q.lastError();
    return idList;
  }

  auto fail = [&]() {
    if (ok) *ok = false;
    if (sqlError) *sqlError = q.lastError();
    q.finish();
    return QList<int>{};
  };

  QQueue<int> parentIds;
  QSet<int> visited;
  parentIds.enqueue(idFolder);
  visited.insert(idFolder);
  while (!parentIds.empty()) {
    const int parentId = parentIds.dequeue();
    q.addBindValue(parentId);
    if (!q.exec())
      return fail();
    while (q.next()) {
      const int feedId = q.value(0).toInt();
      if (!q.value(1).toString().isEmpty()) {
        idList << feedId;
      } else if (!visited.contains(feedId)) {
        visited.insert(feedId);
        parentIds.enqueue(feedId);
      }
    }
    if (q.lastError().isValid())
      return fail();
    q.finish();
  }
  return idList;
}

void FeedReadState::slotSetFeedRead(int readType, int feedId, int idException, QList<int> idNewsList,
                                    bool markAllRead, bool markCurrentNewsRead)
{
  QSqlDatabase db = db_;
  QSqlQuery q(db);

  if (readType != FeedReadSwitchingTab) {
    db.transaction();
    QString idFeedsStr = getIdFeedsString(feedId, idException);
    if (markAllRead) {
      if (idFeedsStr == "feedId=-1") {
        q.exec(QString("UPDATE news SET read=2 WHERE feedId='%1' AND read!=2").arg(feedId));
      } else {
        q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read!=2").arg(idFeedsStr));
      }
    } else {
      if (idFeedsStr == "feedId=-1") {
        q.exec(QString("UPDATE news SET read=2 WHERE feedId='%1' AND read=1").arg(feedId));
      } else {
        q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read=1").arg(idFeedsStr));
      }
    }
    if (idFeedsStr == "feedId=-1") {
      q.exec(QString("UPDATE news SET new=0 WHERE feedId='%1' AND new=1").arg(feedId));
    } else {
      q.exec(QString("UPDATE news SET new=0 WHERE (%1) AND new=1").arg(idFeedsStr));
    }
    if (markCurrentNewsRead)
      q.exec(QString("UPDATE news SET read=2 WHERE id IN (SELECT currentNews FROM feeds WHERE id='%1')").arg(feedId));
    db.commit();

    slotRecountFeedCounts(feedId);
    emit requestCategoryCounts();

    if (readType != FeedReadPlaceToTray)
      slotRefreshInfoTray();
  } else {
    QString idStr;
    for (int newsId : std::as_const(idNewsList)) {
      if (!idStr.isEmpty()) idStr.append(" OR ");
      idStr.append(QString("id='%1'").arg(newsId));
    }

    if (!idNewsList.isEmpty()) {
      db.transaction();
      q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read==1").arg(idStr));
      q.exec(QString("UPDATE news SET new=0 WHERE (%1) AND new==1").arg(idStr));
      db.commit();
    }

    if (feedId > -1)
      slotRecountFeedCounts(feedId, false);
  }

  emit signalSetFeedsFilter();
}

void FeedReadState::slotRefreshInfoTray()
{
  auto databaseAccess = Database::backgroundAccess();
  // Calculate new and unread news number
  int newCount = 0;
  int unreadCount = 0;
  QSqlQuery q(db_);
  q.exec("SELECT sum(newCount), sum(unread) FROM feeds WHERE xmlUrl!=''");
  if (q.first()) {
    newCount    = q.value(0).toInt();
    unreadCount = q.value(1).toInt();
  }

  emit signalRefreshInfoTray(newCount, unreadCount);
}

/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
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
#include "mainwindow.h"
#include <QSqlDriver>
#include <QThread>

FeedReadState::FeedReadState(MainWindow *window, QObject *parent)
  : QObject(parent), mainWindow_(window)
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
  QList<int> deletedList;
  QList<int> starredList;
  QList<int> readList;
  QStringList labelList;
  QSqlQuery q(db_);
  q.exec("SELECT deleted, starred, read, label FROM news WHERE deleted < 2");
  while (q.next()) {
    deletedList.append(q.value(0).toInt());
    starredList.append(q.value(1).toInt());
    readList.append(q.value(2).toInt());
    labelList.append(q.value(3).toString());
  }

  emit signalRecountCategoryCounts(deletedList, starredList, readList, labelList);
}

void FeedReadState::slotRecountFeedCounts(int feedId, bool updateViewport)
{
  QSqlQuery q(db_);
  QString qStr;

  db_.transaction();

  int feedParId = 0;
  bool isFolder = false;
  qStr = QString("SELECT parentId, xmlUrl FROM feeds WHERE id=='%1'").
      arg(feedId);
  q.exec(qStr);
  if (q.next()) {
    feedParId = q.value(0).toInt();
    if (q.value(1).toString().isEmpty())
      isFolder = true;
  }

  int undeleteCount = 0;
  int unreadCount = 0;
  int newCount = 0;

  if (!isFolder) {
    // Calculate all news (not mark deleted)
    qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) undeleteCount = q.value(0).toInt();

    // Calculate unread news
    qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) unreadCount = q.value(0).toInt();

    // Calculate new news
    qStr = QString("SELECT count(new) FROM news WHERE feedId=='%1' AND new==1 AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) newCount = q.value(0).toInt();

    int unreadCountOld = 0;
    int newCountOld = 0;
    int undeleteCountOld = 0;
    qStr = QString("SELECT unread, newCount, undeleteCount FROM feeds WHERE id=='%1'").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) {
      unreadCountOld = q.value(0).toInt();
      newCountOld = q.value(1).toInt();
      undeleteCountOld = q.value(2).toInt();
    }

    if ((unreadCount == unreadCountOld) && (newCount == newCountOld) &&
        (undeleteCount == undeleteCountOld)) {
      db_.commit();
      return;
    }

    // Save unread and new news number for feed
    qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3' "
                   "WHERE id=='%4'").
        arg(unreadCount).arg(newCount).arg(undeleteCount).arg(feedId);
    q.exec(qStr);

    // Update view of the feed
    FeedCountStruct counts;
    counts.feedId = feedId;
    counts.unreadCount = unreadCount;
    counts.newCount = newCount;
    counts.undeleteCount = undeleteCount;
    emit feedCountsUpdate(counts);
  } else {
    bool changed = false;
    QList<int> idParList;
    QList<int> idList = getIdFeedsInList(db_, feedId);
    if (idList.count()) {
      foreach (int id, idList) {
        int parId = 0;
        q.exec(QString("SELECT parentId FROM feeds WHERE id=='%1'").arg(id));
        if (q.next())
          parId = q.value(0).toInt();

        if (parId) {
          if (idParList.indexOf(parId) == -1) {
            idParList.append(parId);
          }
        }

        // Calculate all news (not mark deleted)
        qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) undeleteCount = q.value(0).toInt();

        // Calculate unread news
        qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) unreadCount = q.value(0).toInt();

        // Calculate new news
        qStr = QString("SELECT count(new) FROM news WHERE feedId=='%1' AND new==1 AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) newCount = q.value(0).toInt();

        int unreadCountOld = 0;
        int newCountOld = 0;
        int undeleteCountOld = 0;
        qStr = QString("SELECT unread, newCount, undeleteCount FROM feeds WHERE id=='%1'").
            arg(id);
        q.exec(qStr);
        if (q.next()) {
          unreadCountOld = q.value(0).toInt();
          newCountOld = q.value(1).toInt();
          undeleteCountOld = q.value(2).toInt();
        }

        if ((unreadCount == unreadCountOld) && (newCount == newCountOld) &&
            (undeleteCount == undeleteCountOld)) {
          continue;
        }
        changed = true;

        // Save unread and new news number for parent
        qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3' "
                       "WHERE id=='%4'").
            arg(unreadCount).arg(newCount).arg(undeleteCount).arg(id);
        q.exec(qStr);

        // Update view of the parent
        FeedCountStruct counts;
        counts.feedId = id;
        counts.unreadCount = unreadCount;
        counts.newCount = newCount;
        counts.undeleteCount = undeleteCount;
        emit feedCountsUpdate(counts);
      }

      if (!changed) {
        db_.commit();
        return;
      }

      foreach (int l_feedParId, idParList) {
        while (l_feedParId) {
          QString updated;

          qStr = QString("SELECT sum(unread), sum(newCount), sum(undeleteCount), "
                         "max(updated) FROM feeds WHERE parentId=='%1'").
              arg(l_feedParId);
          q.exec(qStr);
          if (q.next()) {
            unreadCount   = q.value(0).toInt();
            newCount      = q.value(1).toInt();
            undeleteCount = q.value(2).toInt();
            updated       = q.value(3).toString();
          }
          qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3', "
                         "updated='%4' WHERE id=='%5'").
              arg(unreadCount).arg(newCount).arg(undeleteCount).arg(updated).
              arg(l_feedParId);
          q.exec(qStr);

          // Update view
          FeedCountStruct counts;
          counts.feedId = l_feedParId;
          counts.unreadCount = unreadCount;
          counts.newCount = newCount;
          counts.undeleteCount = undeleteCount;
          counts.updated = updated;
          emit feedCountsUpdate(counts);

          if (feedId == l_feedParId) break;
          q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(l_feedParId));
          if (q.next()) l_feedParId = q.value(0).toInt();
        }
      }
    }
  }

  // Recalculate counters for all parents
  int l_feedParId = feedParId;
  while (l_feedParId) {
    QString updated;

    qStr = QString("SELECT sum(unread), sum(newCount), sum(undeleteCount), "
                   "max(updated) FROM feeds WHERE parentId=='%1'").
        arg(l_feedParId);
    q.exec(qStr);
    if (q.next()) {
      unreadCount   = q.value(0).toInt();
      newCount      = q.value(1).toInt();
      undeleteCount = q.value(2).toInt();
      updated       = q.value(3).toString();
    }
    qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3', "
                   "updated='%4' WHERE id=='%5'").
        arg(unreadCount).arg(newCount).arg(undeleteCount).arg(updated).
        arg(l_feedParId);
    q.exec(qStr);

    // Update view
    FeedCountStruct counts;
    counts.feedId = l_feedParId;
    counts.unreadCount = unreadCount;
    counts.newCount = newCount;
    counts.undeleteCount = undeleteCount;
    counts.updated = updated;
    emit feedCountsUpdate(counts);

    q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(l_feedParId));
    if (q.next()) l_feedParId = q.value(0).toInt();
  }
  db_.commit();

  if (updateViewport) emit signalFeedsViewportUpdate();
}

QString FeedReadState::getIdFeedsString(int idFolder, int idException)
{
  QList<int> idList = getIdFeedsInList(db_, idFolder);
  if (idList.count()) {
    QString str;
    foreach (int id, idList) {
      if (id == idException) continue;
      if (!str.isEmpty()) str.append(" OR ");
      str.append(QString("feedId=%1").arg(id));
    }
    return str;
  } else {
    return QString("feedId=-1");
  }
}

QList<int> FeedReadState::getIdFeedsInList(QSqlDatabase &db, int idFolder)
{
  QList<int> idList;
  if (idFolder <= 0) return idList;

  QSqlQuery q(db);
  QQueue<int> parentIds;
  parentIds.enqueue(idFolder);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    QString qStr = QString("SELECT id, xmlUrl FROM feeds WHERE parentId='%1'").
        arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      int feedId = q.value(0).toInt();
      if (!q.value(1).toString().isEmpty())
        idList << feedId;
      if (q.value(1).toString().isEmpty())
        parentIds.enqueue(feedId);
    }
  }
  return idList;
}

void FeedReadState::slotSetFeedRead(int readType, int feedId, int idException, QList<int> idNewsList)
{
  QSqlDatabase db = db_;
  QSqlQuery q(db);

  if (readType != FeedReadSwitchingTab) {
    db.transaction();
    QString idFeedsStr = getIdFeedsString(feedId, idException);
    if (((readType == FeedReadSwitchingFeed) && mainWindow_->markReadSwitchingFeed_) ||
        ((readType == FeedReadClosingTab) && mainWindow_->markReadClosingTab_) ||
        ((readType == FeedReadPlaceToTray) && mainWindow_->markReadMinimize_)) {
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
    if (mainWindow_->markNewsReadOn_ && mainWindow_->markPrevNewsRead_)
      q.exec(QString("UPDATE news SET read=2 WHERE id IN (SELECT currentNews FROM feeds WHERE id='%1')").arg(feedId));
    db.commit();

    slotRecountFeedCounts(feedId);
    slotRecountCategoryCounts();

    if (readType != FeedReadPlaceToTray)
      slotRefreshInfoTray();
  } else {
    QString idStr;
    foreach (int newsId, idNewsList) {
      if (!idStr.isEmpty()) idStr.append(" OR ");
      idStr.append(QString("id='%1'").arg(newsId));
    }

    db.transaction();
    q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read==1").arg(idStr));
    q.exec(QString("UPDATE news SET new=0 WHERE (%1) AND new==1").arg(idStr));
    db.commit();

    if (feedId > -1)
      slotRecountFeedCounts(feedId, false);
  }

  emit signalSetFeedsFilter();
}

void FeedReadState::slotRefreshInfoTray()
{
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

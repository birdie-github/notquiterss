// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDREADSTATE_H
#define FEEDREADSTATE_H

#include <QObject>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include "feeds/update/parseobject.h"

class MainWindow;

struct CategoryCounts {
  int starred = 0;
  int unreadStarred = 0;
  int deleted = 0;
  QMap<int, int> labels;
  QMap<int, int> unreadLabels;
  bool valid = false;
};
Q_DECLARE_METATYPE(CategoryCounts)

struct GlobalFeedStats {
  int newCount = 0;
  int unreadCount = 0;
  int allCount = 0;
  int totalFeeds = 0;
  int customScheduledFeeds = 0;
  int globalScheduledFeeds = 0;
};
Q_DECLARE_METATYPE(GlobalFeedStats)

// Shared read/count operations, executed using the owning object's connection.
// The GUI instance handles synchronous navigation; UpdateObject uses its worker's.
class FeedReadState : public QObject
{
  Q_OBJECT
public:
  explicit FeedReadState(QObject *parent = nullptr);
  void setDatabase(const QSqlDatabase &database);
  static GlobalFeedStats queryGlobalStats(const QSqlDatabase &db);
  static QList<int> getIdFeedsInList(const QSqlDatabase &db, int idFolder,
                                     bool *ok = nullptr, QSqlError *sqlError = nullptr);
  static QString getIdFeedsString(const QSqlDatabase &db, int idFolder, int idException = -1);

public slots:
  void slotRecountCategoryCounts();
  void slotRecountFeedCounts(int feedId, bool updateViewport = true);
  void slotSetFeedRead(int readType, int feedId, int idException, QList<int> idNewsList,
                       bool markAllRead = false, bool markCurrentNewsRead = false);
  void slotRefreshInfoTray();

signals:
  void signalRecountCategoryCounts(CategoryCounts counts);
  void requestCategoryCounts();
  void feedCountsUpdate(FeedCountStruct counts);
  void signalFeedsViewportUpdate();
  void signalRefreshInfoTray(GlobalFeedStats stats);
  void signalSetFeedsFilter(bool clicked = false);

protected:
  QString getIdFeedsString(int idFolder, int idException = -1);
  QSqlDatabase db_;
};

#endif

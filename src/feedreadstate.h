// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDREADSTATE_H
#define FEEDREADSTATE_H

#include <QObject>
#include <QMap>
#include <QSqlDatabase>
#include "parseobject.h"

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

// Shared read/count operations, executed using the owning object's connection.
// The GUI instance handles synchronous navigation; UpdateObject uses its worker's.
class FeedReadState : public QObject
{
  Q_OBJECT
public:
  explicit FeedReadState(MainWindow *window, QObject *parent = nullptr);
  void setDatabase(const QSqlDatabase &database);
  static QList<int> getIdFeedsInList(QSqlDatabase &db, int idFolder);

public slots:
  void slotRecountCategoryCounts();
  void slotRecountFeedCounts(int feedId, bool updateViewport = true);
  void slotSetFeedRead(int readType, int feedId, int idException, QList<int> idNewsList);
  void slotRefreshInfoTray();

signals:
  void signalRecountCategoryCounts(CategoryCounts counts);
  void requestCategoryCounts();
  void feedCountsUpdate(FeedCountStruct counts);
  void signalFeedsViewportUpdate();
  void signalRefreshInfoTray(int newCount, int unreadCount);
  void signalSetFeedsFilter(bool clicked = false);

protected:
  QString getIdFeedsString(int idFolder, int idException = -1);
  MainWindow *mainWindow_;
  QSqlDatabase db_;
};

#endif

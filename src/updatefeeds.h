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
#ifndef UPDATEFEEDS_H
#define UPDATEFEEDS_H

#include <QThread>
#include <QtSql>
#include <QQueue>
#include <QSet>
#include <atomic>

#include "requestfeed.h"
#include "parseobject.h"
#include "feedreadstate.h"
#include "faviconobject.h"
#include "newstabwidget.h"

class UpdateObject;
class MainWindow;

class UpdateFeeds : public QObject
{
  Q_OBJECT
public:
  explicit UpdateFeeds(QObject *parent, bool addFeed = false);
  ~UpdateFeeds();

  void disconnectObjects();
  void startSaveTimer();

  UpdateObject *updateObject_;
  RequestFeed *requestFeed_;
  ParseObject *parseObject_;
  FaviconObject *faviconObject_;
  QThread *getFeedThread_;
  QThread *updateFeedThread_;
  QThread *getFaviconThread_;

public slots:
  void saveMemoryDatabase();

signals:
  void signalSaveMemoryDatabase();

private:
  bool addFeed_;
  QTimer *saveMemoryDBTimer_;

};

class UpdateObject : public FeedReadState
{
  Q_OBJECT
public:
  explicit UpdateObject(QObject *parent = 0);
  ~UpdateObject();

  std::atomic_bool isSaveMemoryDatabase;

public slots:
  void slotGetFeedTimer(int feedId);
  void slotGetAllFeedsTimer();
  void slotGetFeed(int feedId, QString feedUrl, QDateTime date, int auth);
  void slotGetFeedsFolder(QString query);
  void slotGetAllFeeds();
  void slotGetAllFeedsStartup();
  void slotImportFeeds(QByteArray xmlData);
  void getUrlDone(int result, int feedId, QString feedUrlStr,
                  QString error, QByteArray data,
                  QDateTime dtReply, QString codecName);
  void finishUpdate(int feedId, bool changed, int newCount, QString status);
  void slotNextUpdateFeed(bool finish);
  void slotMarkFeedRead(int id, bool isFolder, bool openFeed);
  void slotUpdateStatus(int feedId, bool changed);
  void slotMarkAllFeedsRead();
  void slotMarkReadCategory(int type, int idLabel);
  void slotIconSave(QString feedUrl, QByteArray faviconData);
  void slotSqlQueryExec(QString query);
  void slotMarkAllFeedsOld();
  void saveMemoryDatabase();
  void startCleanUp(bool isShutdown, QStringList feedsIdList, QList<int> foldersIdList);
  void cleanUpShutdown();
  void quitApp();

signals:
  void showProgressBar(int value);
  void loadProgress(int value, bool clear = false);
  void signalMessageStatusBar(QString message, int timeout = 0);
  void signalUpdateFeedsModel();
  void signalRequestUrl(int feedId, QString urlString,
                        QDateTime date, QString userInfo);
  void xmlReadyParse(QByteArray data, int feedId,
                     QDateTime dtReply, QString codecName);
  void setStatusFeed(int feedId, QString status);
  void feedUpdated(int feedId, bool changed, int newCount, bool finish);
  void signalUpdateModel(bool checkFilter = true);
  void signalUpdateNews(int refresh = NewsTabWidget::RefreshInsert);
  void signalCountsStatusBar(int unreadCount, int allCount);
  void signalMarkAllFeedsRead(int nextUnread = -1);
  void signalIconUpdate(int feedId, QByteArray faviconData);
  void signalFinishCleanUp(int countDeleted);

private slots:
  bool addFeedInQueue(int feedId, const QString &feedUrl,
                      const QDateTime &date, int auth, bool manual = false);

private:
  void queueAllFeeds(bool manual);

  QSet<int> manualFeeds_;
  QList<int> feedIdList_;
  int updateFeedsCount_;
  QTimer *updateModelTimer_;
  QTimer *timerUpdateNews_;

};

#endif // UPDATEFEEDS_H

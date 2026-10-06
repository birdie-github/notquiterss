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
#include "network/feedurl.h"
#include "databasebackup.h"
#include "feedhealth.h"
#include <QTextCodec>
#include "updatefeeds.h"

#include "mainapplication.h"
#include "database.h"
#include "settings.h"

#include <QDebug>
#include <QRegularExpression>
#include <QUuid>
#include <sqlite3.h>

#define UPDATE_INTERVAL 3000
#define UPDATE_INTERVAL_MIN 500

#include "newsretention.h"

namespace {
// Construct the connection only after this object and its SQL consumers have
// moved to the worker. Destroy consumers before removing their connection.
class UpdateSqlContext final : public QObject
{
public:
  ParseObject *parser = nullptr;
  UpdateObject *updater = nullptr;

  void initialize()
  {
    Q_ASSERT(QThread::currentThread() == thread());
    auto databaseAccess = Database::backgroundAccess();
    database_ = Database::connection(QUuid::createUuid().toString(QUuid::WithoutBraces));
    parser->setDatabase(database_);
    if (updater) updater->setDatabase(database_);
  }

  ~UpdateSqlContext() override
  {
    Q_ASSERT(QThread::currentThread() == thread());
    delete parser;
    delete updater;
    const QString name = database_.connectionName();
    database_.close();
    database_ = QSqlDatabase();
    if (!name.isEmpty()) QSqlDatabase::removeDatabase(name);
  }

private:
  QSqlDatabase database_;
};

void connectReadState(FeedReadState *state, QObject *window)
{
  QObject::connect(state, SIGNAL(signalRecountCategoryCounts(CategoryCounts)),
                   window, SLOT(slotRecountCategoryCounts(CategoryCounts)),
                   Qt::QueuedConnection);
  QObject::connect(state, SIGNAL(feedCountsUpdate(FeedCountStruct)),
                   window, SLOT(slotFeedCountsUpdate(FeedCountStruct)));
  QObject::connect(state, SIGNAL(signalFeedsViewportUpdate()),
                   window, SLOT(slotFeedsViewportUpdate()));
  QObject::connect(state, SIGNAL(signalRefreshInfoTray(int,int)),
                   window, SLOT(slotRefreshInfoTray(int,int)));
  QObject::connect(state, SIGNAL(signalSetFeedsFilter(bool)),
                   window, SLOT(setFeedsFilter(bool)), Qt::QueuedConnection);
}
}

UpdateFeeds::UpdateFeeds(QObject *parent, bool addFeed)
  : QObject(parent)
  , updateObject_(NULL)
  , requestFeed_(NULL)
  , parseObject_(NULL)
  , faviconObject_(NULL)
  , updateFeedThread_(NULL)
  , getFaviconThread_(NULL)
  , addFeed_(addFeed)
  , saveMemoryDBTimer_(NULL)
{
  getFeedThread_ = new QThread();
  getFeedThread_->setObjectName("getFeedThread_");
  updateFeedThread_ = new QThread();
  updateFeedThread_->setObjectName("updateFeedThread_");

  int timeoutRequest = AppSettings::timeoutRequest.get();
  int numberRequests = AppSettings::numberRequest.get();
  int numberRepeats = AppSettings::numberRepeats.get();

  requestFeed_ = new RequestFeed(timeoutRequest, numberRequests, numberRepeats);

  parseObject_ = new ParseObject();
  // Initialize on the GUI thread before moving either parser to its SQL worker.
  // Future updates deliver all related settings together in the parser's thread.
  MainWindow *window = mainApp->mainWindow();
  parseObject_->setArticleSettings(window->markIdenticalNewsRead_, window->avoidOldNews_,
                                   window->avoidedOldNewsDate_);
  connect(window, &MainWindow::articleSettingsChanged,
          parseObject_, &ParseObject::setArticleSettings, Qt::QueuedConnection);
  auto *sqlContext = new UpdateSqlContext();
  sqlContext->parser = parseObject_;
  parseObject_->setParent(sqlContext);

  if (addFeed_) {
    connect(parent, SIGNAL(signalRequestUrl(int,QString,QDateTime,QString)),
            requestFeed_, SLOT(requestUrl(int,QString,QDateTime,QString)));
    connect(requestFeed_, SIGNAL(getUrlDone(int,int,QString,QString,QByteArray,QDateTime,QString)),
            parent, SLOT(getUrlDone(int,int,QString,QString,QByteArray,QDateTime,QString)));

    connect(parent, SIGNAL(xmlReadyParse(QByteArray,int,QDateTime,QString)),
            parseObject_, SLOT(parseXml(QByteArray,int,QDateTime,QString)));
    connect(parseObject_, SIGNAL(signalFinishUpdate(int,bool,int,QString)),
            parent, SLOT(slotUpdateFeed(int,bool,int,QString)));
  } else {
    getFaviconThread_ = new QThread();
    getFaviconThread_->setObjectName("getFaviconThread_");

    updateObject_ = new UpdateObject();
    sqlContext->updater = updateObject_;
    updateObject_->setParent(sqlContext);
    faviconObject_ = new FaviconObject();

    connect(updateObject_, SIGNAL(signalRequestUrl(int,QString,QDateTime,QString)),
            requestFeed_, SLOT(requestUrl(int,QString,QDateTime,QString)));
    connect(requestFeed_, SIGNAL(getUrlDone(int,int,QString,QString,QByteArray,QDateTime,QString)),
            updateObject_, SLOT(getUrlDone(int,int,QString,QString,QByteArray,QDateTime,QString)));
    connect(requestFeed_, SIGNAL(setStatusFeed(int,QString)),
            parent, SLOT(setStatusFeed(int,QString)));
    connect(parent, SIGNAL(signalStopUpdate()),
            requestFeed_, SLOT(stopRequest()));

    connect(parent, SIGNAL(signalGetFeedTimer(int)),
            updateObject_, SLOT(slotGetFeedTimer(int)));
    connect(parent, SIGNAL(signalGetAllFeedsTimer()),
            updateObject_, SLOT(slotGetAllFeedsTimer()));
    connect(parent, SIGNAL(signalGetAllFeedsStartup()),
            updateObject_, SLOT(slotGetAllFeedsStartup()));
    connect(parent, SIGNAL(signalGetAllFeeds()),
            updateObject_, SLOT(slotGetAllFeeds()));
    connect(parent, SIGNAL(signalGetFeed(int,QString,QDateTime,int,bool)),
            updateObject_, SLOT(slotGetFeed(int,QString,QDateTime,int,bool)));
    connect(parent, SIGNAL(signalGetFeedsFolder(QString)),
            updateObject_, SLOT(slotGetFeedsFolder(QString)));
    connect(parent, SIGNAL(signalImportFeeds(QByteArray,bool)),
            updateObject_, SLOT(slotImportFeeds(QByteArray,bool)));
    connect(updateObject_, SIGNAL(showProgressBar(int)),
            parent, SLOT(showProgressBar(int)));
    connect(updateObject_, &UpdateObject::feedProgressQueued,
            mainApp->mainWindow(), &MainWindow::queueFeedProgress);
    connect(updateObject_, &UpdateObject::feedProgressStage,
            mainApp->mainWindow(), &MainWindow::setFeedProgressStage);
    connect(requestFeed_, &RequestFeed::feedProgressStage,
            mainApp->mainWindow(), &MainWindow::setFeedProgressStage);
    connect(updateObject_, &UpdateObject::feedProgressFinished,
            mainApp->mainWindow(), &MainWindow::finishFeedProgress);
    connect(updateObject_, SIGNAL(signalMessageStatusBar(QString,int)),
            parent, SLOT(showMessageStatusBar(QString,int)));
    connect(updateObject_, SIGNAL(signalUpdateFeedsModel()),
            parent, SLOT(feedsModelReload()),
            Qt::BlockingQueuedConnection);

    connect(updateObject_, SIGNAL(xmlReadyParse(QByteArray,int,QDateTime,QString)),
            parseObject_, SLOT(parseXml(QByteArray,int,QDateTime,QString)),
            Qt::QueuedConnection);
    connect(parseObject_, SIGNAL(signalFinishUpdate(int,bool,int,QString)),
            updateObject_, SLOT(finishUpdate(int,bool,int,QString)),
            Qt::QueuedConnection);
    connect(updateObject_, SIGNAL(feedUpdated(int,bool,int,bool)),
            parent, SLOT(slotUpdateFeed(int,bool,int,bool)));
    connect(updateObject_, SIGNAL(setStatusFeed(int,QString)),
            parent, SLOT(setStatusFeed(int,QString)));

    qRegisterMetaType<FeedCountStruct>("FeedCountStruct");
    connect(parseObject_, SIGNAL(feedCountsUpdate(FeedCountStruct)),
            parent, SLOT(slotFeedCountsUpdate(FeedCountStruct)));

    connect(parseObject_, SIGNAL(signalPlaySound(QString)),
            parent, SLOT(slotPlaySound(QString)));
    connect(parseObject_, SIGNAL(signalAddColorList(int,QString)),
            parent, SLOT(slotAddColorList(int,QString)));

    connect(parent, SIGNAL(signalNextUpdate(bool)),
            updateObject_, SLOT(slotNextUpdateFeed(bool)));
    connect(updateObject_, SIGNAL(signalUpdateModel(bool)),
            parent, SLOT(feedsModelReload(bool)));
    connect(updateObject_, SIGNAL(signalUpdateNews(int)),
            parent, SLOT(slotUpdateNews(int)));
    connect(updateObject_, SIGNAL(signalCountsStatusBar(int,int)),
            parent, SLOT(slotCountsStatusBar(int,int)));

    connect(parent, SIGNAL(signalRecountCategoryCounts()),
            updateObject_, SLOT(slotRecountCategoryCounts()));
    qRegisterMetaType<QList<int> >("QList<int>");
    qRegisterMetaType<CategoryCounts>("CategoryCounts");
    connectReadState(updateObject_, parent);
    auto *navigationState = new FeedReadState(mainApp->mainWindow(), this);
    navigationState->setDatabase(QSqlDatabase::database());
    connectReadState(navigationState, parent);
    connect(navigationState, &FeedReadState::requestCategoryCounts,
            mainApp->mainWindow(), &MainWindow::recountCategoryCounts);
    connect(parent, SIGNAL(signalRecountFeedCounts(int,bool)),
            updateObject_, SLOT(slotRecountFeedCounts(int,bool)));
    connect(parent, SIGNAL(signalSetFeedRead(int,int,int,QList<int>)),
            navigationState, SLOT(slotSetFeedRead(int,int,int,QList<int>)),
            Qt::DirectConnection);
    connect(parent, SIGNAL(signalMarkFeedRead(int,bool,bool)),
            updateObject_, SLOT(slotMarkFeedRead(int,bool,bool)));
    connect(parent, SIGNAL(signalRefreshInfoTray()),
            updateObject_, SLOT(slotRefreshInfoTray()));
    connect(parent, SIGNAL(signalUpdateStatus(int,bool)),
            updateObject_, SLOT(slotUpdateStatus(int,bool)));
    connect(parent, SIGNAL(signalMarkAllFeedsRead()),
            updateObject_, SLOT(slotMarkAllFeedsRead()));
    connect(parent, SIGNAL(signalMarkReadCategory(int,int)),
            updateObject_, SLOT(slotMarkReadCategory(int,int)));
    connect(parent, SIGNAL(signalRefreshNewsView(int)),
            updateObject_, SIGNAL(signalMarkAllFeedsRead(int)));
    connect(updateObject_, SIGNAL(signalMarkAllFeedsRead(int)),
            parent, SLOT(slotRefreshNewsView(int)));
    connect(parent, SIGNAL(signalMarkAllFeedsOld()),
            updateObject_, SLOT(slotMarkAllFeedsOld()));

    connect(parent, SIGNAL(signalSetFeedsFilter(bool)),
            updateObject_, SIGNAL(signalSetFeedsFilter(bool)));

    connect(mainApp, SIGNAL(signalSqlQueryExec(QString)),
            updateObject_, SLOT(slotSqlQueryExec(QString)));
    connect(mainApp, SIGNAL(signalRunUserFilter(int, int)),
            parseObject_, SLOT(runUserFilter(int, int)));
    connect(parseObject_, &ParseObject::signalUserFilterApplied,
            updateObject_, [updater = updateObject_](int feedId) {
      // Manual filtering is asynchronous. Recount and refresh only after commit,
      // on the SQL worker, using the same path as other article state changes.
      updater->slotUpdateStatus(feedId, true);
      updater->slotRecountCategoryCounts();
      emit updater->signalUpdateNews(NewsTabWidget::RefreshAll);
    });

    // faviconObject_
    connect(parent, SIGNAL(faviconRequestUrl(QString,QString)),
            faviconObject_, SLOT(requestUrl(QString,QString)));
    connect(faviconObject_, SIGNAL(signalIconRecived(QString,QByteArray,QString)),
            parent, SLOT(slotIconFeedPreparing(QString,QByteArray,QString)));
    connect(parent, SIGNAL(signalIconFeedReady(QString,QByteArray)),
            updateObject_, SLOT(slotIconSave(QString,QByteArray)));
    connect(updateObject_, SIGNAL(signalIconUpdate(int,QByteArray)),
            parent, SLOT(slotIconFeedUpdate(int,QByteArray)));

    connect(parent, SIGNAL(signalQuitApp()),
            updateObject_, SLOT(quitApp()));
    connect(this, SIGNAL(signalSaveMemoryDatabase()),
            updateObject_, SLOT(saveMemoryDatabase()));

    faviconObject_->moveToThread(getFaviconThread_);
  }

  requestFeed_->moveToThread(getFeedThread_);
  connect(getFeedThread_, &QThread::finished, requestFeed_, &QObject::deleteLater);
  sqlContext->moveToThread(updateFeedThread_);
  connect(updateFeedThread_, &QThread::started, sqlContext,
          [sqlContext] { sqlContext->initialize(); }, Qt::DirectConnection);
  connect(updateFeedThread_, &QThread::finished, sqlContext, &QObject::deleteLater);
}

void UpdateFeeds::start()
{
  if (!addFeed_) {
    getFaviconThread_->start(QThread::LowPriority);
    startSaveTimer();
  }
  getFeedThread_->start(QThread::LowPriority);
  updateFeedThread_->start(QThread::LowPriority);
}

UpdateFeeds::~UpdateFeeds()
{
  if (!addFeed_) {
    faviconObject_->deleteLater();

    getFaviconThread_->exit();
    getFaviconThread_->wait();
    delete getFaviconThread_;
  }

  getFeedThread_->exit();
  getFeedThread_->wait();
  delete getFeedThread_;

  updateFeedThread_->exit();
  updateFeedThread_->wait();
  delete updateFeedThread_;
}

void UpdateFeeds::disconnectObjects()
{
  if (saveMemoryDBTimer_) saveMemoryDBTimer_->stop();
  if (!addFeed_) {
    updateObject_->disconnect(updateObject_);
    updateObject_->disconnect(parseObject_);
    updateObject_->disconnect(requestFeed_);
    updateObject_->disconnect(parent());
    faviconObject_->disconnectObjects();
  }

  requestFeed_->disconnectObjects();
  requestFeed_->disconnect(parent());
  parseObject_->disconnectObjects();
}

void UpdateFeeds::startSaveTimer()
{
  if (!mainApp->storeDBMemory()) return;

  if (!saveMemoryDBTimer_) {
    saveMemoryDBTimer_ = new QTimer(this);
    connect(saveMemoryDBTimer_, SIGNAL(timeout()), this, SLOT(saveMemoryDatabase()));
  }

  int saveInterval = AppSettings::saveDBMemFileInterval.get();
  saveMemoryDBTimer_->start(saveInterval*60*1000);
}

void UpdateFeeds::saveMemoryDatabase()
{
  if (mainApp->isClosing() || !mainApp->storeDBMemory()) return;
  if (updateObject_->isSaveMemoryDatabase) return;

  emit signalSaveMemoryDatabase();
}

//------------------------------------------------------------------------------
UpdateObject::UpdateObject(QObject *parent)
  : FeedReadState(mainApp->mainWindow(), parent)
  , isSaveMemoryDatabase(false)
  , updateFeedsCount_(0)
{
  setObjectName("updateObject_");

  updateModelTimer_ = new QTimer(this);
  updateModelTimer_->setSingleShot(true);
  connect(updateModelTimer_, SIGNAL(timeout()), this, SIGNAL(signalUpdateModel()));

  timerUpdateNews_ = new QTimer(this);
  timerUpdateNews_->setSingleShot(true);
  connect(timerUpdateNews_, SIGNAL(timeout()), this, SIGNAL(signalUpdateNews()));

}

UpdateObject::~UpdateObject()
{

}

bool UpdateObject::isFeedInFolder(int feedId, int folderId)
{
  if (folderId < 0) return false;
  QSqlQuery q(db_);
  if (!q.prepare("SELECT parentId FROM feeds WHERE id=?")) {
    qWarning() << "Could not prepare feed parent lookup:" << q.lastError().text();
    return false;
  }
  QSet<int> visited;
  while (feedId > 0) {
    if (visited.contains(feedId)) {
      qWarning() << "Cycle in feed parent hierarchy at ID:" << feedId;
      return false;
    }
    visited.insert(feedId);
    q.bindValue(0, feedId);
    if (!q.exec()) {
      qWarning() << "Feed parent lookup failed for ID:" << feedId << q.lastError().text();
      return false;
    }
    if (!q.next()) {
      if (q.lastError().isValid())
        qWarning() << "Could not read feed parent for ID:" << feedId << q.lastError().text();
      return false;
    }
    const int parentId = q.value(0).toInt();
    q.finish();
    if (parentId == folderId) return true;
    feedId = parentId;
  }
  return false;
}

void UpdateObject::slotGetFeedTimer(int feedId)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.exec(QString("SELECT xmlUrl, lastBuildDate, authentication FROM feeds WHERE id=='%1' AND disableUpdate=0")
         .arg(feedId));
  if (q.next()) {
    addFeedInQueue(feedId, q.value(0).toString(),
                   q.value(1).toDateTime(), q.value(2).toInt());
  }
  emit showProgressBar(updateFeedsCount_);
}

void UpdateObject::slotGetAllFeedsTimer()
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.exec("SELECT id, xmlUrl, lastBuildDate, authentication FROM feeds "
         "WHERE xmlUrl!='' AND disableUpdate=0 "
         "AND (updateIntervalEnable==-1 OR updateIntervalEnable IS NULL)");
  while (q.next()) {
    addFeedInQueue(q.value(0).toInt(), q.value(1).toString(),
                   q.value(2).toDateTime(), q.value(3).toInt());
  }
  emit showProgressBar(updateFeedsCount_);
}

/** @brief Process update feed action
 *---------------------------------------------------------------------------*/
void UpdateObject::slotGetFeed(int feedId, QString feedUrl, QDateTime date, int auth, bool force)
{
  addFeedInQueue(feedId, feedUrl, date, auth, true, force);

  emit showProgressBar(updateFeedsCount_);
}

/** @brief Process update feed in folder action
 *---------------------------------------------------------------------------*/
void UpdateObject::slotGetFeedsFolder(QString query)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.exec(query);
  while (q.next()) {
    addFeedInQueue(q.value(0).toInt(), q.value(1).toString(),
                   q.value(2).toDateTime(), q.value(3).toInt(), true);
  }

  emit showProgressBar(updateFeedsCount_);
}

/** @brief Process update all feeds action
 *---------------------------------------------------------------------------*/
void UpdateObject::slotGetAllFeedsStartup()
{
  queueAllFeeds(false);
}

void UpdateObject::slotGetAllFeeds()
{
  queueAllFeeds(true);
}

void UpdateObject::queueAllFeeds(bool manual)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.exec("SELECT id, xmlUrl, lastBuildDate, authentication FROM feeds WHERE xmlUrl!='' AND disableUpdate=0");
  while (q.next()) {
    addFeedInQueue(q.value(0).toInt(), q.value(1).toString(),
                   q.value(2).toDateTime(), q.value(3).toInt(), manual);
  }
  emit showProgressBar(updateFeedsCount_);
}

/** @brief Import feeds from OPML-file
 *
 * Calls open file system dialog with filter *.opml.
 * Adds all feeds to DB include hierarchy, ignore duplicate feeds
 *---------------------------------------------------------------------------*/
void UpdateObject::slotImportFeeds(QByteArray xmlData, bool upgradeHttp)
{
  auto databaseAccess = Database::backgroundAccess();
  int outlineCount = 0;
  QSqlQuery q(db_);
  QList<int> idsList;
  QList<QString> urlsList;
  QXmlStreamReader xml;
  xml.addData(xmlData);

  QHash<QString, int> knownUrls;
  if (!q.exec("SELECT id, xmlUrl FROM feeds WHERE xmlUrl != ''")) {
    emit signalMessageStatusBar(tr("Could not check existing subscriptions."), 5000);
    return;
  }
  while (q.next()) knownUrls.insert(FeedUrl::identity(FeedUrl::normalize(q.value(1).toString())), q.value(0).toInt());
  q.finish();

  if (!db_.transaction()) {
    emit signalMessageStatusBar(tr("Could not start the import transaction."), 5000);
    return;
  }
  auto failImport = [this, &q] {
    const QString error = q.lastError().text();
    q.finish();
    db_.rollback();
    emit signalMessageStatusBar(tr("Import failed: %1").arg(error), 5000);
  };

  // Store hierarchy of "outline" tags. Next nested outline is pushed to stack.
  // When it closes, pop it out from stack. Top of stack is the root outline.
  QStack<int> parentIdsStack;
  parentIdsStack.push(0);
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      // Search for "outline" only
      if (xml.name() == QLatin1String("outline")) {
        qDebug() << outlineCount << "+:" << xml.prefix().toString()
                 << ":" << xml.name().toString();

        QString textString(xml.attributes().value("text").toString());
        QString titleString(xml.attributes().value("title").toString());
        QString xmlUrlString(xml.attributes().value("xmlUrl").toString());
        if (textString.isEmpty()) textString = titleString;

        //Folder finded
        if (xmlUrlString.isEmpty()) {
          int rowToParent = 0;
          if (!q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
                      arg(parentIdsStack.top()))) { failImport(); return; }
          if (q.next()) rowToParent = q.value(0).toInt();

          q.prepare("INSERT INTO feeds(text, title, xmlUrl, created, f_Expanded, parentId, rowToParent) "
                    "VALUES (:text, :title, :xmlUrl, :feedCreateTime, 0, :parentId, :rowToParent)");
          q.bindValue(":text", textString);
          q.bindValue(":title", textString);
          q.bindValue(":xmlUrl", "");
          q.bindValue(":feedCreateTime",
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
          q.bindValue(":parentId", parentIdsStack.top());
          q.bindValue(":rowToParent", rowToParent);
          if (!q.exec()) { failImport(); return; }
          parentIdsStack.push(q.lastInsertId().toInt());
        }
        // Feed finded
        else {
          QUrl url = FeedUrl::normalize(xmlUrlString);
          if (upgradeHttp) url = FeedUrl::upgrade(url);
          xmlUrlString = url.toString();
          // Compare normalized identities, including HTTP/HTTPS equivalents,
          // without modifying any existing subscription.
          const QString identity = FeedUrl::identity(url);
          const bool isFeedDuplicated = knownUrls.contains(identity);
          if (isFeedDuplicated) {
            qDebug() << "duplicate feed:" << xmlUrlString << textString;
          } else {
            int rowToParent = 0;
            if (!q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
                        arg(parentIdsStack.top()))) { failImport(); return; }
            if (q.next()) rowToParent = q.value(0).toInt();

            q.prepare("INSERT INTO feeds(text, title, description, xmlUrl, htmlUrl, created, parentId, rowToParent) "
                      "VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
            q.addBindValue(textString);
            q.addBindValue(xml.attributes().value("title").toString());
            q.addBindValue(xml.attributes().value("description").toString());
            q.addBindValue(xmlUrlString);
            q.addBindValue(xml.attributes().value("htmlUrl").toString());
            q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            q.addBindValue(parentIdsStack.top());
            q.addBindValue(rowToParent);
            if (!q.exec()) { failImport(); return; }

            knownUrls.insert(identity, q.lastInsertId().toInt());
            idsList.append(q.lastInsertId().toInt());
            urlsList.append(xmlUrlString);
          }
          parentIdsStack.push(knownUrls.value(identity));
        }
      }
    } else if (xml.isEndElement()) {
      if (xml.name() == QLatin1String("outline")) {
        parentIdsStack.pop();
        ++outlineCount;
      }
    }
    qDebug() << parentIdsStack;
  }
  if (xml.error()) {
    QString error = QString("Import error: Line = %1, Column = %2; Error = %3").
        arg(xml.lineNumber()).arg(xml.columnNumber()).arg(xml.errorString());
    qCritical() << error;
    q.finish();
    db_.rollback();
    emit signalMessageStatusBar(error, 3000);
    return;
  }

  q.finish();
  if (!db_.commit()) {
    const QString error = db_.lastError().text();
    db_.rollback();
    emit signalMessageStatusBar(tr("Import failed: %1").arg(error), 5000);
    return;
  }
  DatabaseBackup::subscriptionsChanged("import OPML");
  emit signalMessageStatusBar(tr("Import complete"), 3000);

  // This connection is blocking: the UI must be able to acquire access while
  // rebuilding its feed model. The import transaction and query are finished.
  databaseAccess.unlock();
  emit signalUpdateFeedsModel();

  for (int i = 0; i < idsList.count(); i++) {
    updateFeedsCount_ = updateFeedsCount_ + 2;
    announceFeedProgress(idsList.at(i));
    emit signalRequestUrl(idsList.at(i), urlsList.at(i), QDateTime(), "");
  }
  emit showProgressBar(updateFeedsCount_);
}

void UpdateObject::announceFeedProgress(int feedId)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.prepare("SELECT text FROM feeds WHERE id=?");
  q.addBindValue(feedId);
  const QString name = q.exec() && q.next() ? q.value(0).toString() : QString();
  emit feedProgressQueued(feedId, name);
}

// ----------------------------------------------------------------------------
bool UpdateObject::addFeedInQueue(int feedId, const QString &feedUrl,
                                  const QDateTime &date, int auth, bool manual, bool force)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery enabledQuery(db_);
  enabledQuery.prepare("SELECT disableUpdate FROM feeds WHERE id = ?");
  enabledQuery.addBindValue(feedId);
  if (!enabledQuery.exec()) {
    qWarning() << "Cannot check whether feed updates are disabled:" << enabledQuery.lastError().text();
    return false;
  }
  if (!enabledQuery.next() || (!force && enabledQuery.value(0).toBool()))
    return false;
  enabledQuery.finish();

  int feedIdIndex = feedIdList_.indexOf(feedId);
  if (feedIdIndex > -1) {
    // A manual request can take over a pending automatic refresh without
    // issuing another network request or counting another refresh cycle.
    if (manual) manualFeeds_.insert(feedId);
    return false;
  } else {
    feedIdList_.append(feedId);
    if (manual) manualFeeds_.insert(feedId);
    updateFeedsCount_ = updateFeedsCount_ + 2;
    QString userInfo;
    if (auth == 1) {
      QSqlQuery q(db_);
      QUrl url(feedUrl);
      q.prepare("SELECT username, password FROM passwords WHERE server=?");
      q.addBindValue(url.host());
      q.exec();
      if (q.next()) {
        userInfo = QString("%1:%2").arg(q.value(0).toString()).
            arg(QString::fromUtf8(QByteArray::fromBase64(q.value(1).toByteArray())));
      }
    }
    announceFeedProgress(feedId);
    emit signalRequestUrl(feedId, feedUrl, date, userInfo);
    return true;
  }
}

/** @brief Process network request completion
 *---------------------------------------------------------------------------*/
void UpdateObject::getUrlDone(int result, int feedId, QString feedUrlStr,
                              QString error, QByteArray data, QDateTime dtReply,
                              QString codecName)
{
  qDebug() << "getUrl result = " << result << "error: " << error << "url: " << feedUrlStr;

  if (updateFeedsCount_ > 0) {
    updateFeedsCount_--;
  }

  if (!data.isEmpty()) {
    emit feedProgressStage(feedId, tr("Processing…"));
    emit xmlReadyParse(data, feedId, dtReply, codecName);
  } else {
    QString status = "0";
    if (result == -7) {
      status = "cancelled";
    } else if (result < 0) {
      status = QString("%1 %2").arg(result).arg(error);
      qWarning() << QString("Request failed: result = %1, error - %2, url - %3").
                    arg(result).arg(error).arg(feedUrlStr);
    }
    finishUpdate(feedId, false, 0, status);
  }
}

void UpdateObject::finishUpdate(int feedId, bool changed, int newCount, QString status)
{
  auto databaseAccess = Database::backgroundAccess();
  if (updateFeedsCount_ > 0) {
    updateFeedsCount_--;
  }
  bool finish = false;
  if (updateFeedsCount_ <= 0) {
    finish = true;
  }

  int feedIdIndex = feedIdList_.indexOf(feedId);
  if (feedIdIndex > -1) {
    feedIdList_.takeAt(feedIdIndex);
  }

  QSqlQuery q(db_);
  q.prepare("SELECT status FROM feeds WHERE id=?");
  q.addBindValue(feedId);
  q.exec();
  const QString previous = q.next() ? q.value(0).toString() : QString();
  status = FeedHealth::finish(previous, status, manualFeeds_.remove(feedId) != 0);
  q.finish();
  q.prepare("UPDATE feeds SET status=? WHERE id=?");
  q.addBindValue(status);
  q.addBindValue(feedId);
  q.exec();

  if (changed) {
    if (mainWindow_->currentNewsTab->type_ == NewsTabWidget::TabTypeFeed) {
      const bool folderUpdate = isFeedInFolder(feedId, mainWindow_->currentNewsTab->feedId_);

      // Click on feed if it is displayed to update view
      if ((feedId == mainWindow_->currentNewsTab->feedId_) || folderUpdate) {
        if (!timerUpdateNews_->isActive())
          timerUpdateNews_->start(1000);

        QSqlQuery q(db_);
        int unreadCount = 0;
        int allCount = 0;
        q.exec(QString("SELECT unread, undeleteCount FROM feeds WHERE id=='%1'").
               arg(mainWindow_->currentNewsTab->feedId_));
        if (q.first()) {
          unreadCount = q.value(0).toInt();
          allCount    = q.value(1).toInt();
        }
        emit signalCountsStatusBar(unreadCount, allCount);
      }
    } else if (mainWindow_->currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
      if (!timerUpdateNews_->isActive())
        timerUpdateNews_->start(1000);
    }
  }

  emit feedProgressFinished(feedId);
  emit feedUpdated(feedId, changed, newCount, finish);
  emit setStatusFeed(feedId, status);
}

/** @brief Start timer if feed presents in queue
 *---------------------------------------------------------------------------*/
void UpdateObject::slotNextUpdateFeed(bool finish)
{
  if (!updateModelTimer_->isActive()) {
    if (finish)
      updateModelTimer_->start(UPDATE_INTERVAL_MIN);
    else
      updateModelTimer_->start(UPDATE_INTERVAL);
  }
}

void UpdateObject::slotMarkFeedRead(int id, bool isFolder, bool openFeed)
{
  auto databaseAccess = Database::backgroundAccess();
  db_.transaction();
  QSqlQuery q(db_);
  QString qStr;
  if (isFolder) {
    qStr = QString("UPDATE news SET read=2 WHERE read!=2 AND deleted==0 AND (%1)").
        arg(getIdFeedsString(id));
    q.exec(qStr);
    qStr = QString("UPDATE news SET new=0 WHERE new==1 AND (%1)").
        arg(getIdFeedsString(id));
    q.exec(qStr);
  } else {
    if (openFeed) {
      qStr = QString("UPDATE news SET read=2 WHERE feedId=='%1' AND read!=2 AND deleted==0").
          arg(id);
      q.exec(qStr);
    } else {
      QString qStr = QString("UPDATE news SET read=1 WHERE feedId=='%1' AND read==0").
          arg(id);
      q.exec(qStr);
    }
    qStr = QString("UPDATE news SET new=0 WHERE feedId=='%1' AND new==1").
        arg(id);
    q.exec(qStr);
  }
  db_.commit();

  if (!openFeed || isFolder)
    slotUpdateStatus(id, true);
}

/** @brief Update status of current feed or feed of current tab
 *---------------------------------------------------------------------------*/
void UpdateObject::slotUpdateStatus(int feedId, bool changed)
{
  auto databaseAccess = Database::backgroundAccess();
  if (changed) {
    slotRecountFeedCounts(feedId);
  }
  slotRefreshInfoTray();

  if (feedId > 0) {
    const bool folderUpdate = isFeedInFolder(feedId, mainWindow_->currentNewsTab->feedId_);

    // Click on feed if it is displayed to update view
    if ((feedId == mainWindow_->currentNewsTab->feedId_) || folderUpdate) {
      QSqlQuery q(db_);
      int unreadCount = 0;
      int allCount = 0;
      q.exec(QString("SELECT unread, undeleteCount FROM feeds WHERE id=='%1'").
             arg(mainWindow_->currentNewsTab->feedId_));
      if (q.next()) {
        unreadCount = q.value(0).toInt();
        allCount    = q.value(1).toInt();
      }
      emit signalCountsStatusBar(unreadCount, allCount);
    }
  }
}

void UpdateObject::slotMarkAllFeedsRead()
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);

  q.exec("UPDATE news SET read=2 WHERE read!=2 AND deleted==0");
  q.exec("UPDATE news SET new=0 WHERE new==1 AND deleted==0");

  q.exec("SELECT id FROM feeds WHERE unread!=0");
  while (q.next()) {
    slotRecountFeedCounts(q.value(0).toInt());
  }
  slotRecountCategoryCounts();

  slotRefreshInfoTray();

  emit signalMarkAllFeedsRead();
}

void UpdateObject::slotMarkReadCategory(int type, int idLabel)
{
  auto databaseAccess = Database::backgroundAccess();
  QString qStr;
  switch (type) {
  case NewsTabWidget::TabTypeUnread:
    qStr = "feedId > 0 AND deleted = 0 AND read < 2";
    break;
  case NewsTabWidget::TabTypeStar:
    qStr = "feedId > 0 AND deleted = 0 AND starred = 1";
    break;
  case NewsTabWidget::TabTypeLabel:
    if (idLabel != 0) {
      qStr = QString("feedId > 0 AND deleted = 0 AND label LIKE '%,%1,%'").
          arg(idLabel);
    } else {
      qStr = QString("feedId > 0 AND deleted = 0 AND label!='' AND label!=','");
    }
    break;
  }

  QSqlQuery q(db_);
  q.exec(QString("UPDATE news SET read=1 WHERE %1").arg(qStr));
  q.exec(QString("UPDATE news SET new=0 WHERE %1").arg(qStr));

  QList<int> idList;
  q.exec("SELECT id FROM feeds WHERE unread!=0");
  while (q.next()) {
    idList.append(q.value(0).toInt());
  }
  emit signalMarkAllFeedsRead(0);
  foreach (int id, idList) {
    slotUpdateStatus(id, true);
  }
}

/** @brief Save icon in DB and emit signal to update it
 *----------------------------------------------------------------------------*/
void UpdateObject::slotIconSave(QString feedUrl, QByteArray faviconData)
{
  auto databaseAccess = Database::backgroundAccess();
  int feedId = 0;

  QSqlQuery q(db_);
  q.prepare("SELECT id FROM feeds WHERE xmlUrl LIKE :xmlUrl");
  q.bindValue(":xmlUrl", feedUrl);
  q.exec();
  if (q.next()) {
    feedId = q.value(0).toInt();
  }

  q.prepare("UPDATE feeds SET image = ? WHERE id == ?");
  q.addBindValue(faviconData.toBase64());
  q.addBindValue(feedId);
  q.exec();

  emit signalIconUpdate(feedId, faviconData);
}

void UpdateObject::slotSqlQueryExec(QString query)
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  if (!q.exec(query)) {
    qCritical() << __PRETTY_FUNCTION__ << __LINE__
                << "q.lastError(): " << q.lastError().text();
  }
}

/** @brief Mark all feeds Not New
 *---------------------------------------------------------------------------*/
void UpdateObject::slotMarkAllFeedsOld()
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  q.exec("UPDATE news SET new=0 WHERE new==1 AND deleted==0");

  q.exec("SELECT id FROM feeds WHERE newCount!=0");
  while (q.next()) {
    slotRecountFeedCounts(q.value(0).toInt());
  }
  slotRecountCategoryCounts();

  if ((mainWindow_->currentNewsTab != NULL) && (mainWindow_->currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads)) {
    emit signalUpdateNews(NewsTabWidget::RefreshWithPos);
  }

  slotRefreshInfoTray();
}

void UpdateObject::saveMemoryDatabase()
{
  auto databaseAccess = Database::backgroundAccess();
  if (shutdownPrepared_) return;
  isSaveMemoryDatabase = true;
  QString error;
  if (!Database::sqliteDBMemFile(db_, error))
    emit signalMessageStatusBar(tr("Could not save the in-memory database: %1").arg(error), 30000);
  isSaveMemoryDatabase = false;
}

/** @brief Delete news from the feed by criteria
 *---------------------------------------------------------------------------*/
void UpdateObject::startCleanUp(bool isShutdown, QStringList feedsIdList, QList<int> foldersIdList)
{
  auto databaseAccess = Database::backgroundAccess();
  QString error;
  if (!db_.transaction()) {
    error = tr("Cannot start cleanup: %1").arg(db_.lastError().text());
    if (isShutdown) shutdownCleanupError_ = error;
    else emit signalCleanUpFailed(error);
    return;
  }
  if (isShutdown) {
    shutdownCleanupError_.clear();
    shutdownCleanup_ = ShutdownCleanup::TransactionOpen;
  }

  int countDeleted = 0;
  // All operation queries are destroyed before committing or rolling back.
  bool success = cleanUpArticles(isShutdown, feedsIdList, foldersIdList, countDeleted, error);
  if (success && !db_.commit()) {
    error = db_.lastError().text();
    success = false;
  }
  if (!success) {
    qWarning().noquote() << "Cleanup failed:" << error;
    if (isShutdown) {
      shutdownCleanupWarning_ = tr("Shutdown cleanup failed and was skipped. "
                                  "Previously committed changes from this session are preserved.\n%1")
                                  .arg(error);
      // The exit recovery path resolves this transaction before copying.
      return;
    }
    QString rollbackError;
    while (!rollbackCleanUp(rollbackError)) {
      emit signalCleanUpRollbackFailed(rollbackError);
      // Keep this SQL thread and the database access lock inside cleanup.
      // Processing queued work here could commit the partial cleanup through
      // another operation. Only the GUI's semaphore wakeup can retry rollback.
      cleanupRollbackRetry_.acquire();
    }
    emit signalCleanUpFailed(error);
    return;
  }
  if (isShutdown) shutdownCleanup_ = ShutdownCleanup::Finished;

  Settings settings("Settings");
  const bool vacuum = !isShutdown || (!mainApp->storeDBMemory() &&
      settings.value("cleanupOnShutdown", true).toBool() &&
      settings.value("optimizeDB", false).toBool());
  QString warning;
  if (vacuum) {
    QSqlQuery optimize(db_);
    if (!optimize.exec("VACUUM")) {
      warning = tr("Cleanup completed, but database optimization failed: %1")
                  .arg(optimize.lastError().text());
      qWarning().noquote() << warning;
    }
  }
  if (isShutdown && !warning.isEmpty()) shutdownCleanupWarning_ = warning;
  emit signalFinishCleanUp(countDeleted, warning);
}

void UpdateObject::retryCleanUpRollback()
{
  cleanupRollbackRetry_.release();
}

bool UpdateObject::rollbackCleanUp(QString &error)
{
  error.clear();
  QVariant value = db_.driver() ? db_.driver()->handle() : QVariant();
  sqlite3 *handle = value.isValid() && qstrcmp(value.typeName(), "sqlite3*") == 0
      ? *static_cast<sqlite3 **>(value.data()) : nullptr;
  if (!handle) {
    error = tr("The live SQLite connection is unavailable.");
    return false;
  }
  // SQLite may already have rolled back after a fatal error.
  if (sqlite3_get_autocommit(handle) || db_.rollback()) return true;
  error = tr("Cannot roll back cleanup: %1").arg(db_.lastError().text());
  return false;
}

bool UpdateObject::cleanUpArticles(bool isShutdown, const QStringList &feedsIdList,
                                  const QList<int> &foldersIdList, int &countDeleted,
                                  QString &error)
{
  bool cleanupOn = true;
  bool fullCleanUp = false;

  Settings settings(isShutdown ? "Settings" : "CleanUpWizard");
  if (isShutdown) {
    cleanupOn = settings.value("cleanupOnShutdown", true).toBool();
  } else {
    fullCleanUp = settings.value("fullCleanUp", false).toBool();
  }
  int maxDayCleanUp = settings.value("maxDayClearUp", 30).toInt();
  int maxNewsCleanUp = settings.value("maxNewsClearUp", 200).toInt();
  bool dayCleanUpOn = settings.value("dayClearUpOn", true).toBool();
  bool newsCleanUpOn = settings.value("newsClearUpOn", true).toBool();
  bool readCleanUp = settings.value("readClearUp", false).toBool();
  bool neverUnreadCleanUp = settings.value("neverUnreadClearUp", true).toBool();
  bool neverStarCleanUp = settings.value("neverStarClearUp", true).toBool();
  bool neverLabelCleanUp = settings.value("neverLabelClearUp", true).toBool();
  bool cleanUpDeleted = settings.value("cleanUpDeleted", false).toBool();

  QSqlQuery q(db_);
  QString qStr;
  auto queryError = [&](const QSqlQuery &query) {
    error = query.lastError().text();
    if (error.isEmpty()) error = tr("A cleanup query did not return its expected result.");
    return false;
  };
  auto execute = [&](const QString &statement) {
    return q.exec(statement) || queryError(q);
  };
  const QString clearArticle = QStringLiteral("UPDATE news SET description='', content='', received='', "
      "author_name='', author_uri='', author_email='', category='', new='', read='', "
      "starred='', label='', deleteDate='', feedParentId='', deleted=2");
  auto removeArticles = [&](const QList<int> &ids, const QString &feedId,
                            bool hardDelete, bool onlyRead) {
    // Stay below SQLite's older parameter limit and bound each SQL statement.
    constexpr int batchSize = 256;
    QSqlQuery remove(db_);
    for (int offset = 0; offset < ids.size(); offset += batchSize) {
      const int count = qMin(batchSize, int(ids.size()) - offset);
      QStringList placeholders;
      for (int i = 0; i < count; ++i) placeholders.append(QStringLiteral("?"));
      QString statement = (hardDelete ? QStringLiteral("DELETE FROM news") : clearArticle) +
          QStringLiteral(" WHERE feedId=? AND id IN (") + placeholders.join(',') + QLatin1Char(')');
      if (onlyRead) statement += QStringLiteral(" AND read!=0");
      if (!remove.prepare(statement)) return queryError(remove);
      remove.addBindValue(feedId);
      for (int i = 0; i < count; ++i) remove.addBindValue(ids.at(offset + i));
      if (!remove.exec()) return queryError(remove);
      remove.finish();
    }
    return true;
  };

  // Only the automatic maximum-age policy also filters incoming entries.
  // The manual wizard must not purge identities using its independent limits.
  const QDateTime retentionCutoff = isShutdown && cleanupOn && dayCleanUpOn
      ? NewsRetention::cutoff(maxDayCleanUp) : QDateTime();

  if (isShutdown) {
    if (!execute("UPDATE news SET new=0 WHERE new==1")) return false;
    if (!execute("UPDATE news SET read=2 WHERE read==1")) return false;
    if (!execute("UPDATE feeds SET newCount=0 WHERE newCount!=0")) return false;
  }

  if (cleanupOn) {
    if (!isShutdown) {
      if (!execute("SELECT count(id) FROM news WHERE deleted < 2")) return false;
      if (!q.next()) return queryError(q);
      countDeleted = q.value(0).toInt();
    }

    // Run Cleanup for all feeds, except categories
    foreach (QString feedId, feedsIdList) {
      int countDelNews = 0;
      int countAllNews = 0;

      if (retentionCutoff.isValid()) {
        QString eligible = QStringLiteral("deleted>=2 OR (deleted<2");
        if (neverUnreadCleanUp) eligible += QStringLiteral(" AND read!=0");
        if (neverStarCleanUp) eligible += QStringLiteral(" AND starred==0");
        if (neverLabelCleanUp)
          eligible += QStringLiteral(" AND (label=='' OR label==',' OR label IS NULL)");
        eligible += QLatin1Char(')');
        if (!q.prepare("SELECT id, published FROM news WHERE feedId=? AND (" + eligible + ")"))
          return queryError(q);
        q.addBindValue(feedId);
        QList<int> expiredIds;
        if (!q.exec()) return queryError(q);
        while (q.next()) {
          if (NewsRetention::expired(q.value(1).toString(), retentionCutoff))
            expiredIds.append(q.value(0).toInt());
        }
        if (q.lastError().isValid()) return queryError(q);
        q.finish();
        if (!removeArticles(expiredIds, feedId, true, false)) return false;
      }

      qStr = QString("SELECT count(*) FROM news WHERE feedId=='%1' AND deleted==0").arg(feedId);
      if (!execute(qStr)) return false;
      if (!q.next()) return queryError(q);
      countAllNews = q.value(0).toInt();

      if (fullCleanUp) {
        if (!execute(QString("DELETE FROM news WHERE feedId=='%1' AND deleted >= 2").arg(feedId)))
          return false;
      }

      qStr = QString("SELECT id, received, published FROM news WHERE feedId=='%1' AND deleted == 0").
          arg(feedId);
      if (neverUnreadCleanUp) qStr.append(" AND read!=0");
      if (neverStarCleanUp) qStr.append(" AND starred==0");
      if (neverLabelCleanUp) qStr.append(" AND (label=='' OR label==',' OR label IS NULL)");
      qStr.append(" ORDER BY published");
      if (!execute(qStr)) return false;
      QList<int> removeIds;
      QList<int> readRemoveIds;
      while (q.next()) {
        int newsId = q.value(0).toInt();

        if (newsCleanUpOn && (countDelNews < (countAllNews - maxNewsCleanUp))) {
          removeIds.append(newsId);
          countDelNews++;
          continue;
        }

        QDateTime dateTime = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        // Dated entries were handled above using publication age. Undated
        // entries retain receipt-age cleanup and their duplicate identities.
        const bool useReceiptAge = !isShutdown ||
            !NewsRetention::publicationDate(q.value(2).toString()).isValid();
        if (dayCleanUpOn && useReceiptAge &&
            (dateTime.daysTo(QDateTime::currentDateTime()) > maxDayCleanUp)) {
          removeIds.append(newsId);
          countDelNews++;
          continue;
        }

        if (readCleanUp) {
          readRemoveIds.append(newsId);
          countDelNews++;
        }
      }
      if (q.lastError().isValid()) return queryError(q);
      // Complete selection before any DELETE or UPDATE of the selected rows.
      q.finish();
      if (!removeArticles(removeIds, feedId, fullCleanUp, false)) return false;
      if (!removeArticles(readRemoveIds, feedId, fullCleanUp, !fullCleanUp)) return false;

      int undeleteCount = 0;
      qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
          arg(feedId);
      if (!execute(qStr)) return false;
      if (!q.next()) return queryError(q);
      undeleteCount = q.value(0).toInt();

      int unreadCount = 0;
      qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
          arg(feedId);
      if (!execute(qStr)) return false;
      if (!q.next()) return queryError(q);
      unreadCount = q.value(0).toInt();

      int newCount = 0;
      if (!isShutdown) {
        qStr = QString("SELECT count(new) FROM news WHERE feedId=='%1' AND new==1 AND deleted==0").
            arg(feedId);
        if (!execute(qStr)) return false;
        if (!q.next()) return queryError(q);
        newCount = q.value(0).toInt();
        qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3' WHERE id=='%4'").
            arg(unreadCount).arg(newCount).arg(undeleteCount).arg(feedId);
      } else {
        qStr = QString("UPDATE feeds SET unread='%1', undeleteCount='%2' WHERE id=='%3'").
            arg(unreadCount).arg(undeleteCount).arg(feedId);
      }
      if (!execute(qStr)) return false;
    }

    // Run categories recount, because cleanup may change counts
    foreach (int folderIdStart, foldersIdList) {
      if (folderIdStart < 1) continue;

      int folderId = folderIdStart;
      // Process all parents
      while (0 < folderId) {
        int unreadCount = -1;
        int undeleteCount = -1;
        int newCount = -1;

        // Calculate sum of all feeds with same parent
        qStr = QString("SELECT sum(unread), sum(undeleteCount), sum(newCount) "
                       "FROM feeds WHERE parentId=='%1'").arg(folderId);
        if (!execute(qStr)) return false;
        if (!q.next()) return queryError(q);
        unreadCount   = q.value(0).toInt();
        undeleteCount = q.value(1).toInt();
        newCount = q.value(2).toInt();

        if (unreadCount != -1) {
          qStr = QString("UPDATE feeds SET unread='%1', undeleteCount='%2', newCount='%3' WHERE id=='%4'").
              arg(unreadCount).arg(undeleteCount).arg(newCount).arg(folderId);
          if (!execute(qStr)) return false;
        }

        // go to next parent's parent
        qStr = QString("SELECT parentId FROM feeds WHERE id=='%1'").arg(folderId);
        folderId = 0;
        if (!execute(qStr)) return false;
        if (q.next()) folderId = q.value(0).toInt();
        else if (q.lastError().isValid()) return queryError(q);
      }
    }

    if (cleanUpDeleted) {
      if (!execute("UPDATE news SET description='', content='', received='', "
             "author_name='', author_uri='', author_email='', "
             "category='', new='', read='', starred='', label='', "
             "deleteDate='', feedParentId='', deleted=2 WHERE deleted==1")) return false;
    }
    if (!isShutdown) {
      if (!execute("SELECT count(id) FROM news WHERE deleted < 2")) return false;
      if (!q.next()) return queryError(q);
      countDeleted -= q.value(0).toInt();
    }
  }

  return true;
}

/** @brief Delete news from the feed by criteria
 *---------------------------------------------------------------------------*/
void UpdateObject::cleanUpShutdown()
{
  auto databaseAccess = Database::backgroundAccess();
  QSqlQuery q(db_);
  QStringList feedsIdList;
  QList<int> foldersIdList;
  if (!q.exec("SELECT id, xmlUrl FROM feeds")) {
    shutdownCleanupError_ = tr("Cannot select feeds for shutdown cleanup: %1").arg(q.lastError().text());
    return;
  }
  while (q.next()) {
    if (q.value(1).toString().isEmpty()) {
      foldersIdList << q.value(0).toInt();
    }
    else {
      feedsIdList << q.value(0).toString();
    }
  }
  if (q.lastError().isValid()) {
    shutdownCleanupError_ = tr("Cannot read feeds for shutdown cleanup: %1").arg(q.lastError().text());
    return;
  }
  q.finish();

  startCleanUp(true, feedsIdList, foldersIdList);
}

void UpdateObject::quitApp()
{
  auto databaseAccess = Database::backgroundAccess();
  if (!shutdownPrepared_) {
    shutdownPrepared_ = true;
  }
  retryQuitApp();
}

void UpdateObject::retryQuitApp(const QString &fileName)
{
  auto databaseAccess = Database::backgroundAccess();
  if (!shutdownPrepared_) return;
  auto reportFailure = [](const QString &error) {
    qCritical().noquote() << error;
    QMetaObject::invokeMethod(mainApp, [error] {
      mainApp->reportDatabaseSaveFailure(error);
    }, Qt::QueuedConnection);
  };
  if (shutdownCleanup_ == ShutdownCleanup::NotStarted) {
    cleanUpShutdown();
    if (shutdownCleanup_ == ShutdownCleanup::NotStarted) {
      reportFailure(shutdownCleanupError_);
      return;
    }
  }
  if (shutdownCleanup_ == ShutdownCleanup::TransactionOpen) {
    QString error;
    if (!rollbackCleanUp(error)) {
      reportFailure(error);
      return;
    }
    shutdownCleanup_ = ShutdownCleanup::Finished;
    qWarning().noquote() << shutdownCleanupWarning_;
  }
  if (mainApp->storeDBMemory()) {
    QString error;
    if (!Database::sqliteDBMemFile(db_, error, true, fileName)) {
      // Keep the SQL context and its live database alive for another attempt.
      reportFailure(error);
      return;
    }
    Settings settings("Settings");
    if (fileName.isEmpty() && settings.value("cleanupOnShutdown", true).toBool() &&
        settings.value("optimizeDB", false).toBool()) {
      QString optimizeError;
      if (!Database::setVacuum(optimizeError)) {
        const QString warning = tr("Database saved, but database optimization failed: %1")
                                .arg(optimizeError);
        qWarning().noquote() << warning;
        if (!shutdownCleanupWarning_.isEmpty()) shutdownCleanupWarning_ += '\n';
        shutdownCleanupWarning_ += warning;
      }
    }
  }
  const auto backup = DatabaseBackup::create(db_, DatabaseBackup::Trigger::Exit);
  const QString cleanupWarning = shutdownCleanupWarning_;
  QMetaObject::invokeMethod(mainApp, [backup, fileName, cleanupWarning] {
    if (!cleanupWarning.isEmpty())
      QMessageBox::warning(nullptr, MainApplication::tr("Shutdown cleanup"), cleanupWarning);
    DatabaseBackup::report(backup, false);
    if (!fileName.isEmpty()) {
      QMessageBox::information(nullptr, MainApplication::tr("Database recovery"),
          MainApplication::tr("The database was saved to:\n%1\n\n"
                              "If you saved it outside the normal database location, restore "
                              "this copy to %2 before starting the application again.")
                              .arg(fileName, mainApp->dbFileName()));
    }
    mainApp->quitApplication();
  }, Qt::QueuedConnection);
}

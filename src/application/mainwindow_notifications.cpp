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
#include "mainwindow.h"
#include "mainapplication.h"
#include "settings.h"
#include <utility>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

/** @brief Show notification on inbox news
 *---------------------------------------------------------------------------*/
void MainWindow::showNotification(bool bShowRecentNews/*=false*/)
{
  Settings settings;
  settings.setValue("Flags/updatingFeeds", false);

  bool showNotify = true;

  if (showNotifyInactiveApp_)
  {
    showNotify = !isActiveWindow();
  }

  NewNewsData& curNews = (bShowRecentNews ? recentNews : newNews);

  if (curNews.idFeedList_.isEmpty() || !showNotify || !showNotifyOn_)
  {
    clearNotification();
    return;
  }

  if (fullscreenModeNotify_) {
#if defined(Q_OS_WIN)
    HWND hWnd = GetForegroundWindow();
    RECT appBounds;
    RECT rc;
    GetWindowRect(GetDesktopWindow(), &rc);

    if ((hWnd != GetDesktopWindow())
       && (hWnd != GetShellWindow())
       ) {
      GetWindowRect(hWnd, &appBounds);
      if ((rc.top == appBounds.top) && (rc.bottom == appBounds.bottom) &&
          (rc.left == appBounds.left) && (rc.right == appBounds.right)) {
        return;
      }
    }
#endif
  }

  timerTrayOpenNotify.stop();

  if (notificationWidget != nullptr)
  {
    delete notificationWidget;
  }

  notificationWidget = new NotificationWidget(curNews.idFeedList_, curNews.cntNewsList_,
                                              idColorList_, colorList_, this);

  if (!bShowRecentNews)
  {
    clearNotification();
  }

  connect(notificationWidget, &NotificationWidget::signalShow, this, [this] { showWindows(); });
  connect(notificationWidget, &NotificationWidget::signalClose, this, &MainWindow::deleteNotification);
  connect(notificationWidget, &NotificationWidget::signalOpenNews, this, &MainWindow::slotOpenNew);
  connect(notificationWidget, &NotificationWidget::signalOpenExternalBrowser,
          this, &MainWindow::slotOpenNewBrowser);
  connect(notificationWidget, &NotificationWidget::signalMarkRead,
          this, &MainWindow::slotMarkReadNewsInNotification);
  connect(notificationWidget, &NotificationWidget::signalDeleteNews,
          this, &MainWindow::slotDeleteNewsInNotification);
  connect(notificationWidget, &NotificationWidget::signalMarkAllRead,
          this, &MainWindow::slotMarkAllReadNewsInNotification);

  notificationWidget->show();
}

/** @brief Delete notification widget
 *---------------------------------------------------------------------------*/
void MainWindow::deleteNotification()
{
  notificationWidget->deleteLater();
  notificationWidget = nullptr;
}

void MainWindow::clearNotification(bool bClearRecentNews/*=false*/)
{
  newNews.idFeedList_.clear();
  newNews.cntNewsList_.clear();

  if (bClearRecentNews)
  {
    recentNews.idFeedList_.clear();
    recentNews.cntNewsList_.clear();

    idColorList_.clear();
    colorList_.clear();
  }
}

void MainWindow::slotAddColorList(int id, const QString &color)
{
  if (idColorList_.indexOf(id) == -1)
  {
    idColorList_.append(id);
    colorList_.append(color);
  }
}

/** @brief Show news on click in notification window
 *---------------------------------------------------------------------------*/
void MainWindow::slotOpenNew(int feedId, int newsId)
{
  openingFeedAction_ = 0;
  openNewsWebViewOn_ = true;

  QSqlQuery q;
  q.exec(QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").arg(newsId).arg(feedId));

  QModelIndex feedIndex = feedsModel_->indexById(feedId);
  feedsView_->setCurrentIndex(feedsProxyModel_->mapFromSource(feedIndex));
  feedsModel_->setData(feedsModel_->indexSibling(feedIndex, "currentNews"),
                           newsId);

  if (stackedWidget_->count() && currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
  }

  // Search open tab containing this feed
  int indexTab = -1;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->feedId_ == feedId) {
      indexTab = i;
      break;
    }
  }

  if (indexTab == -1) {
    if (tabBar_->currentIndex() != TAB_WIDGET_PERMANENT) {
      setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadSwitchingTab, currentNewsTab);

      updateCurrentTab_ = false;
      tabBar_->setCurrentIndex(TAB_WIDGET_PERMANENT);
      updateCurrentTab_ = true;

      currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
      newsModel_ = currentNewsTab->newsModel_;
      newsView_ = currentNewsTab->newsView_;
    } else {
      if (stackedWidget_->count() && currentNewsTab->type_ != NewsTabWidget::TabTypeFeed) {
        setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadSwitchingFeed, currentNewsTab);
      } else {
        // Mark previous feed Read while switching to another feed
        setFeedRead(NewsTabWidget::TabTypeFeed, feedIdOld_, FeedReadSwitchingFeed, 0, feedId);
      }

      categoriesTree_->setCurrentIndex(QModelIndex());
    }
  } else {
    tabBar_->setCurrentIndex(indexTab);
  }
  slotFeedSelected(feedIndex);
  feedsView_->repaint();
  feedIdOld_ = feedId;

  openingFeedAction_ = AppSettings::openingFeedAction.get();
  openNewsWebViewOn_ = AppSettings::openNewsWebViewOn.get();
  showWindows();
  newsView_->setFocus();
}

/** @brief Open news in external browser on click in notification window
 *---------------------------------------------------------------------------*/
void MainWindow::slotOpenNewBrowser(const QUrl &url)
{
  currentNewsTab->openUrl(url);
}

void MainWindow::slotMarkReadNewsInNotification(int feedId, int newsId, int read)
{
  QSqlQuery q;
  bool showNews = false;
  if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    int cnt = newsModel_->rowCount();
    for (int i = 0; i < cnt; ++i) {
      if (newsId == newsModel_->index(i, newsModel_->fieldIndex("id")).data().toInt()) {
        if (read == 1) {
          if (newsModel_->index(i, newsModel_->fieldIndex("new")).data(Qt::EditRole).toInt() == 1) {
            newsModel_->setData(
                  newsModel_->index(i, newsModel_->fieldIndex("new")),
                  0);
            q.exec(QString("UPDATE news SET new=0 WHERE id=='%1'").arg(newsId));
          }
          if (newsModel_->index(i, newsModel_->fieldIndex("read")).data(Qt::EditRole).toInt() == 0) {
            newsModel_->setData(
                  newsModel_->index(i, newsModel_->fieldIndex("read")),
                  1);
            q.exec(QString("UPDATE news SET read=1 WHERE id=='%1'").arg(newsId));
          }
        } else {
          if (newsModel_->index(i, newsModel_->fieldIndex("read")).data(Qt::EditRole).toInt() != 0) {
            newsModel_->setData(
                  newsModel_->index(i, newsModel_->fieldIndex("read")),
                  0);
            q.exec(QString("UPDATE news SET read=0 WHERE id=='%1'").arg(newsId));
          }
        }

        newsView_->viewport()->update();
        showNews = true;
        break;
      }
    }
  }

  if (!showNews) {
    q.exec(QString("UPDATE news SET new=0, read='%1' WHERE id=='%2'").
           arg(read).arg(newsId));
  }

  slotUpdateStatus(feedId);
  recountCategoryCounts();
}

void MainWindow::slotDeleteNewsInNotification(int feedId, int newsId)
{
  mainApp->sqlQueryExec(QString("UPDATE news SET new=0, read=2, deleted=1, deleteDate='%1' WHERE id=='%2'").
               arg(QDateTime::currentDateTime().toString(Qt::ISODate)).
               arg(newsId));

  if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    for (int i = 0; i < newsModel_->rowCount(); ++i) {
      if (newsId == newsModel_->index(i, newsModel_->fieldIndex("id")).data().toInt()) {
        newsModel_->setData(newsModel_->index(i, newsModel_->fieldIndex("new")), 0);
        newsModel_->setData(newsModel_->index(i, newsModel_->fieldIndex("read")), 2);
        newsModel_->setData(newsModel_->index(i, newsModel_->fieldIndex("deleted")), 1);
        newsModel_->setData(newsModel_->index(i, newsModel_->fieldIndex("deleteDate")),
                            QDateTime::currentDateTime().toString(Qt::ISODate));

        newsModel_->submitAll();

        while (newsModel_->canFetchMore())
          newsModel_->fetchMore();

        currentNewsTab->loadNewspaper(NewsTabWidget::RefreshWithPos);

        QModelIndex curIndex;
        if (i == newsModel_->rowCount())
          curIndex = newsModel_->index(i-1, newsModel_->fieldIndex("title"));
        else if (i > newsModel_->rowCount())
          curIndex = newsModel_->index(i-1, newsModel_->fieldIndex("title"));
        else
          curIndex = newsModel_->index(i, newsModel_->fieldIndex("title"));
        newsView_->setCurrentIndex(curIndex);
        currentNewsTab->slotNewsViewSelected(curIndex);
        break;
      }
    }
  }

  slotUpdateStatus(feedId);
  recountCategoryCounts();
}

void MainWindow::slotMarkAllReadNewsInNotification()
{
  if (NotificationWidget *notificationWidget = qobject_cast<NotificationWidget*>(sender())) {
    QList<int> idFeedList = notificationWidget->idFeedList();
    QList<int> idNewsList = notificationWidget->idNewsList();
    const QSet<int> idNewsSet(idNewsList.begin(), idNewsList.end());

    if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads && !idNewsSet.isEmpty()) {
      const int idCol = newsModel_->fieldIndex(QStringLiteral("id"));
      const int newCol = newsModel_->fieldIndex(QStringLiteral("new"));
      const int readCol = newsModel_->fieldIndex(QStringLiteral("read"));
      for (int i = 0; i < newsModel_->rowCount(); ++i) {
        if (idNewsSet.contains(newsModel_->index(i, idCol).data().toInt())) {
          newsModel_->setData(newsModel_->index(i, newCol), 0);
          newsModel_->setData(newsModel_->index(i, readCol), 1);
        }
      }
      newsView_->viewport()->update();
    }

    if (!idNewsList.isEmpty()) {
      QStringList idStrs;
      idStrs.reserve(idNewsList.size());
      for (int newsId : std::as_const(idNewsList)) {
        idStrs.append(QString::number(newsId));
      }
      mainApp->sqlQueryExec(QStringLiteral("UPDATE news SET new=0, read=1 WHERE id IN (%1)")
                            .arg(idStrs.join(QLatin1Char(','))));
    }

    for (int feedId : std::as_const(idFeedList)) {
      slotUpdateStatus(feedId);
    }
    recountCategoryCounts();
  }
}

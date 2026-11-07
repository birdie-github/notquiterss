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

#include "database.h"
#include "downloadmanager.h"
#include "feedsproxymodel.h"
#include "findtext.h"
#include "mainapplication.h"
#include "settings.h"
#include "statusbarcontroller.h"

#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QTextCodec>
#include <utility>

// ---------------------------------------------------------------------------
void MainWindow::createNewsTab(int index)
{
  currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(index));
  currentNewsTab->setSettings();
  currentNewsTab->retranslateStrings();
  currentNewsTab->setBrowserPosition();

  newsModel_ = currentNewsTab->newsModel_;
  newsView_ = currentNewsTab->newsView_;
}

/** @brief Process updating news list
 *---------------------------------------------------------------------------*/
void MainWindow::slotUpdateNews(int refresh)
{
  if (mainApp->isClosing() || !currentNewsTab ||
      currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;
  auto databaseAccess = Database::tryAccess();
  if (!databaseAccess.owns_lock()) {
    const QPointer<NewsTabWidget> tab(currentNewsTab);
    const int feedId = tab->feedId_;
    const auto type = tab->type_;
    const int labelId = type == NewsTabWidget::TabTypeLabel ? tab->labelId_ : -1;
    deferDatabaseUi(DeferredDatabaseUi::NewsRefresh, [this, tab, feedId, type, labelId, refresh] {
      // A permanent tab can have been reused for a different feed/category.
      if (tab && tab == currentNewsTab && tab->feedId_ == feedId && tab->type_ == type &&
          (type != NewsTabWidget::TabTypeLabel || tab->labelId_ == labelId))
        slotUpdateNews(refresh);
    });
    return;
  }
  deferredDatabaseUi_.erase(DeferredDatabaseUi::NewsRefresh);

  int newsId = newsModel_->index(
        newsView_->currentIndex().row(), newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();

  newsModel_->select();

  if (newsModel_->rowCount() != 0) {
    while (newsModel_->canFetchMore())
      newsModel_->fetchMore();
  }

  currentNewsTab->loadNewspaper(refresh);

  QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
  QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
  if (indexList.count()) {
    int newsRow = indexList.first().row();
    newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
  } else {
    currentNewsTab->currentNewsIdOld = newsId;
    currentNewsTab->hideArticleContent();
  }
}

// ----------------------------------------------------------------------------
bool MainWindow::openArticleUrl(const QUrl &url)
{
  // Preserve eligibility across a batch even if the first link takes focus.
  const bool returnFocus = AppSettings::returnFocusAfterLink.get() &&
      (isActiveWindow() || returnFocusAfterLinkTimer_.isActive()) &&
      isVisible() && !isMinimized() && !mainApp->isClosing() &&
      !quitTimer_.isActive() && url.scheme() != QLatin1String("mailto");
  returnFocusAfterLinkTimer_.stop();
  const bool opened = mainApp->openExternalUrl(url);
  if (opened && returnFocus && isVisible() && !isMinimized() &&
      !mainApp->isClosing() && !quitTimer_.isActive())
    returnFocusAfterLinkTimer_.start();
  return opened;
}

/** @brief Set filter for news list
 *---------------------------------------------------------------------------*/
void MainWindow::setNewsFilter(QAction* pAct, bool clicked)
{
  if (currentNewsTab == nullptr) return;
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) {
    filterNewsAll_->setChecked(true);
    return;
  }

  QModelIndex index = newsView_->currentIndex();
  int feedId = currentNewsTab->feedId_;
  int newsId = newsModel_->index(
        index.row(), newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();

  // Hide news has marrked "Read"
  // read=1 - show regardless of filter
  // read=2 - don't display at all
  if (clicked) {
    QString qStr = QString("UPDATE news SET read=2 WHERE feedId='%1' AND read=1").
        arg(feedId);
    mainApp->sqlQueryExec(qStr);
  }

  // Create filter for category or for feed
  if (feedsModel_->isFolder(feedsModel_->indexById(feedId))) {
    newsFilterStr = QString("(%1) AND ").arg(getIdFeedsString(feedId));
  } else {
    newsFilterStr = QString("feedId=%1 AND ").arg(feedId);
  }

  // ... add filter from "filter"
  if (pAct->objectName() == "filterNewsAll_") {
    newsFilterStr.append("deleted = 0");
  } else if (pAct->objectName() == "filterNewsNew_") {
    newsFilterStr.append(QString("new = 1 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsUnread_") {
    newsFilterStr.append(QString("read < 2 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsStar_") {
    newsFilterStr.append(QString("starred = 1 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsNotStarred_") {
    newsFilterStr.append(QString("starred = 0 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsUnreadStar_") {
    newsFilterStr.append(QString("(read < 2 OR starred = 1) AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsLastDay_") {
    newsFilterStr.append(QString("(published >= datetime('now', '-1 day')) AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsLastWeek_") {
    newsFilterStr.append(QString("(published >= datetime('now', '-7 day')) AND deleted = 0"));
  }

  // ... add filter from "search"
  QString filterStr = newsFilterStr;
  currentNewsTab->appendSearchFilter(filterStr);

  newsModel_->setFilter(filterStr);
  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  currentNewsTab->loadNewspaper();

  // Set icon right before user click
  if (pAct->objectName() == "filterNewsAll_") newsFilter_->setIcon(QIcon(":/images/filterOff"));
  else newsFilter_->setIcon(QIcon(":/images/filterOn"));

  // Set focus on previous displayed feed, if user click has been
  if (clicked) {
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
    if (indexList.count()) {
      int newsRow = indexList.first().row();
      newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    } else {
      currentNewsTab->currentNewsIdOld = newsId;
      currentNewsTab->hideArticleContent();
    }
  }

  // Store filter to enable it as "last used filter"
  if (pAct->objectName() != mainNewsFilter_)
    newsFilterAction_ = pAct;
}

/** @brief Mark feed Read while clicking on unfocused one
 *---------------------------------------------------------------------------*/
void MainWindow::setFeedRead(int type, int feedId, FeedReedType feedReadType,
                             NewsTabWidget *widgetTab, int idException)
{
  if ((type >= NewsTabWidget::TabTypeDownloads) || (type == NewsTabWidget::TabTypeDel))
    return;

  if ((type == NewsTabWidget::TabTypeFeed) && (feedReadType != FeedReadSwitchingTab)) {
    if (feedId <= -1) return;

    QList<int> idNewsList;
    const bool markAllRead =
        ((feedReadType == FeedReadSwitchingFeed) && markReadSwitchingFeed_) ||
        ((feedReadType == FeedReadClosingTab) && markReadClosingTab_) ||
        ((feedReadType == FeedReadPlaceToTray) && markReadMinimize_);
    const bool markCurrentNewsRead = markNewsReadOn_ && markPrevNewsRead_;
    emit signalSetFeedRead(feedReadType, feedId, idException, idNewsList,
                           markAllRead, markCurrentNewsRead);
  } else if (widgetTab) {
    int cnt = widgetTab->newsModel_->rowCount();
    if (cnt == 0) return;

    QList<int> idNewsList;
    for (int i = cnt-1; i >= 0; --i) {
      int newsId = widgetTab->newsModel_->index(i, widgetTab->newsModel_->fieldIndex("id")).data().toInt();
      idNewsList.append(newsId);
    }
    for (int i = 0; i < stackedWidget_->count(); i++) {
      NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
      if ((widget->type_ < NewsTabWidget::TabTypeDownloads) &&
          !((feedReadType == FeedReadSwitchingFeed) && (i == TAB_WIDGET_PERMANENT))) {
        int row = widget->newsView_->currentIndex().row();
        int newsId = widget->newsModel_->index(row, widget->newsModel_->fieldIndex("id")).data().toInt();
        idNewsList.removeOne(newsId);
      }
    }
    emit signalSetFeedRead(FeedReadSwitchingTab, feedId, idException, idNewsList);
  }
}

/** @brief Refresh news view (After mark all feeds or one feed read)
 *---------------------------------------------------------------------------*/
void MainWindow::slotRefreshNewsView(int nextUnread)
{
  if (nextUnread == 1) {
    feedsView_->clearSelection();
    QModelIndex indexNextUnread =
        feedsView_->indexNextUnread(feedsView_->currentIndex());
    feedsView_->setCurrentIndex(indexNextUnread);
    slotFeedClicked(indexNextUnread);
  } else if ((tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) && (nextUnread == -1)) {
    QModelIndex index = feedsProxyModel_->index(-1, "text");
    feedsView_->setCurrentIndex(index);
    slotFeedClicked(index);
  } else {
    int currentRow = newsView_->currentIndex().row();

    newsModel_->select();

    while (newsModel_->canFetchMore())
      newsModel_->fetchMore();

    currentNewsTab->loadNewspaper(NewsTabWidget::RefreshWithPos);

    newsView_->setCurrentIndex(newsModel_->index(currentRow, newsModel_->fieldIndex("title")));
  }
}

// ----------------------------------------------------------------------------
void MainWindow::slotNewsFilter()
{
  if (newsFilterGroup_->checkedAction()->objectName() == mainNewsFilter_) {
    if (newsFilterAction_ != nullptr) {
      newsFilterAction_->setChecked(true);
      setNewsFilter(newsFilterAction_);
    } else {
      if (mainToolbar_->widgetForAction(newsFilter_)) {
        QWidget *widget = mainToolbar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
        }
      }
      if (feedsToolBar_->widgetForAction(newsFilter_)) {
        QWidget *widget = feedsToolBar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
        }
      }
      if (currentNewsTab->newsToolBar_->widgetForAction(newsFilter_)) {
        QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
        }
      }
    }
  } else {
    const auto filterActions = newsFilterGroup_->actions();
    for (QAction *action : filterActions) {
      if (action->objectName() == mainNewsFilter_) {
        action->setChecked(true);
        setNewsFilter(action);
        break;
      }
    }
  }
}

/** @brief Open feed in a new tab
 *---------------------------------------------------------------------------*/
void MainWindow::slotOpenFeedNewTab()
{
  if (stackedWidget_->count() && currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadSwitchingTab, currentNewsTab);
    currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
    Settings settings;
    settings.setValue("NewsTabSplitterState", currentNewsTab->newsTabWidgetSplitter_->saveState());
  }

  QModelIndexList indexList = feedsView_->selectionModel()->selectedRows(0);
  if (indexList.count() <= 1) {
    indexList.clear();
    indexList.append(feedsProxyModel_->mapFromSource(feedsView_->selectIndex()));
  }
  QModelIndex index = indexList.takeFirst();
  feedsView_->selectIdEn_ = false;
  feedsView_->setCurrentIndex(index);
  slotFeedSelected(feedsProxyModel_->mapToSource(index), true);

  for (const QModelIndex &indexProxy : std::as_const(indexList)) {
    QModelIndex index = feedsProxyModel_->mapToSource(indexProxy);
    creatFeedTab(feedsModel_->dataField(index, "id").toInt(),
                 feedsModel_->dataField(index, "parentId").toInt());
  }
}

void MainWindow::slotOpenCategoryNewTab()
{
  slotCategoriesClicked(categoriesTree_->currentItem(), 0, true);
}

/** @brief Close tab with \a index
 *---------------------------------------------------------------------------*/
void MainWindow::slotCloseTab(int index)
{
  if (index != 0) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(index));

    setFeedRead(widget->type_, widget->feedId_, FeedReadClosingTab, widget);

    stackedWidget_->removeWidget(widget);
    tabBar_->removeTab(index);
    widget->disconnectObjects();
    widget->newsTitleLabel_->deleteLater();
    widget->deleteLater();
  }
}

/** @brief Switch to tab with index \a index
 *---------------------------------------------------------------------------*/
void MainWindow::slotTabCurrentChanged(int index)
{
  if (mainApp->isClosing() || index < 0 || index >= stackedWidget_->count() ||
      index != tabBar_->currentIndex()) return;
  const bool currentTabAlive = stackedWidget_->indexOf(currentNewsTab) >= 0;
  // A newer selection supersedes an older queued tab change, except when
  // closing a tab requires replacing the pointer before it is destroyed.
  if (currentTabAlive && deferredDatabaseUi_.count(DeferredDatabaseUi::FeedSelection)) return;
  if (tabBar_->closingTabState_ == TabBar::CloseTabOtherIndex) return;
  auto databaseAccess = Database::tryAccess();
  // Closing a tab must replace currentNewsTab before deleteLater destroys it.
  // Only an ordinary switch between live tabs can safely be deferred.
  if (!databaseAccess.owns_lock() && currentTabAlive) {
    const QPointer<QWidget> tab(stackedWidget_->widget(index));
    deferDatabaseUi(DeferredDatabaseUi::TabSelection, [this, tab] {
      if (tab && stackedWidget_->widget(tabBar_->currentIndex()) == tab)
        slotTabCurrentChanged(stackedWidget_->indexOf(tab));
    });
    return;
  }
  deferredDatabaseUi_.erase(DeferredDatabaseUi::TabSelection);

  NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(index));

  if ((widget->type_ == NewsTabWidget::TabTypeFeed) || (widget->type_ >= NewsTabWidget::TabTypeDownloads))
    categoriesTree_->setCurrentIndex(QModelIndex());
  if (widget->type_ != NewsTabWidget::TabTypeFeed) {
    feedsView_->setCurrentIndex(QModelIndex());
    feedProperties_->setEnabled(false);
  }

  if (index == TAB_WIDGET_PERMANENT) {
    feedsWidgetVisibleAct_->setChecked(showFeedsTabPermanent_);
    slotVisibledFeedsWidget();
  } else {
    if (hideFeedsOpenTab_) {
      feedsWidgetVisibleAct_->setChecked(false);
      slotVisibledFeedsWidget();
    }
  }

  stackedWidget_->setCurrentIndex(index);

  if (!updateCurrentTab_) return;

  if ((tabBar_->closingTabState_ == TabBar::CloseTabIdle) && (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads)) {
    setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadSwitchingTab, currentNewsTab);

    currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
    Settings settings;
    settings.setValue("NewsTabSplitterState", currentNewsTab->newsTabWidgetSplitter_->saveState());
  }

  if (widget->type_ == NewsTabWidget::TabTypeFeed) {
    if (widget->feedId_ == 0)
      widget->hide();
    currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(index));
    currentNewsTab->setSettings(false);
    currentNewsTab->retranslateStrings();
    currentNewsTab->setBrowserPosition();

    newsModel_ = currentNewsTab->newsModel_;
    newsView_ = currentNewsTab->newsView_;

    QModelIndex feedIndex = feedsProxyModel_->mapFromSource(feedsModel_->indexById(widget->feedId_));
    feedsView_->setCurrentIndex(feedIndex);
    feedProperties_->setEnabled(feedIndex.isValid());

    setFeedsFilter(false);

    slotUpdateNews(NewsTabWidget::RefreshWithPos);
    if (widget->isVisible())
      newsView_->setFocus();
    else
      feedsView_->setFocus();

    statusBarController_->setCountsVisible(widget->feedId_, widget->feedId_);
  } else if (widget->type_ == NewsTabWidget::TabTypeDownloads) {
    statusBarController_->setCountsVisible(false, false);
    mainApp->downloadManager()->show();
    currentNewsTab = widget;
    currentNewsTab->retranslateStrings();
  } else {
    QList<QTreeWidgetItem *> treeItems;
    if (widget->type_ == NewsTabWidget::TabTypeLabel) {
      treeItems = categoriesTree_->findItems(QString::number(widget->labelId_),
                                             Qt::MatchFixedString|Qt::MatchRecursive,
                                             2);
    } else {
      treeItems = categoriesTree_->findItems(QString::number(widget->type_),
                                             Qt::MatchFixedString,
                                             1);
    }
    categoriesTree_->setCurrentItem(treeItems.at(0));

    currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(index));
    currentNewsTab->setSettings(false);
    currentNewsTab->retranslateStrings();
    currentNewsTab->setBrowserPosition();

    newsModel_ = currentNewsTab->newsModel_;
    newsView_ = currentNewsTab->newsView_;

    slotUpdateNews(NewsTabWidget::RefreshWithPos);
    newsView_->setFocus();

    int unreadCount = widget->getUnreadCount(categoriesTree_->currentItem()->text(4));
    int allCount = widget->newsModel_->rowCount();
    setStatusCounts(unreadCount, allCount);

    statusBarController_->setCountsVisible(widget->type_ != NewsTabWidget::TabTypeDel, true);
  }

  setTextTitle(widget->newsTitleLabel_->toolTip(), widget);
  currentNewsTab->updateActionStates();
}

/** @brief Process tab moving
 *----------------------------------------------------------------------------*/
void MainWindow::slotTabMoved(int fromIndex, int toIndex)
{
  stackedWidget_->insertWidget(toIndex, stackedWidget_->widget(fromIndex));
}

void MainWindow::setNewsLayout(QAction *action)
{
  newsLayout_ = action->data().toInt();
  switch (newsLayout_) {
  case 1:
    layoutToggle_->setIcon(QIcon(":/images/layout_newspaper"));
    break;
  default:
    layoutToggle_->setIcon(QIcon(":/images/layout_classic"));
  }
  currentNewsTab->setNewsLayout();
}

void MainWindow::setNewsLayout()
{
  if ((newsLayout_ + 1) >= layoutGroup_->actions().count())
    layoutGroup_->actions().at(0)->setChecked(true);
  else
    layoutGroup_->actions().at(newsLayout_ + 1)->setChecked(true);
  setNewsLayout(layoutGroup_->checkedAction());
}

/** @brief Set browser position
 *---------------------------------------------------------------------------*/
void MainWindow::setBrowserPosition(QAction *action)
{
  browserPosition_ = action->data().toInt();
  currentNewsTab->setBrowserPosition();
}

// ----------------------------------------------------------------------------
void MainWindow::creatFeedTab(int feedId, int feedParId)
{
  QSqlQuery q;
  q.exec(QString("SELECT text, image, currentNews, xmlUrl FROM feeds WHERE id=='%1'").
         arg(feedId));

  if (q.next()) {
    NewsTabWidget *widget = new NewsTabWidget(this, NewsTabWidget::TabTypeFeed, feedId, feedParId);
    addTab(widget);
    widget->setSettings();
    widget->retranslateStrings();
    widget->setBrowserPosition();

    bool isFeed = true;
    if (q.value(3).toString().isEmpty())
      isFeed = false;

    // Set icon and title for tab
    QPixmap iconTab;
    QByteArray byteArray = q.value(1).toByteArray();
    if (!isFeed) {
      iconTab.load(":/images/folder");
    } else {
      if (byteArray.isNull() || defaultIconFeeds_) {
        iconTab.load(":/images/feed");
      } else if (isFeed) {
        iconTab.loadFromData(QByteArray::fromBase64(byteArray));
      }
    }
    widget->newsIconTitle_->setPixmap(iconTab);
    widget->setTextTab(q.value(0).toString());

    QString feedIdFilter;
    if (feedsModel_->isFolder(feedsModel_->indexById(feedId))) {
      feedIdFilter = QString("(%1) AND ").arg(getIdFeedsString(feedId));
    } else {
      feedIdFilter = QString("feedId=%1 AND ").arg(feedId);
    }

    if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsAll_") {
      feedIdFilter.append("deleted = 0");
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsNew_") {
      feedIdFilter.append(QString("new = 1 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsUnread_") {
      feedIdFilter.append(QString("read < 2 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsStar_") {
      feedIdFilter.append(QString("starred = 1 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsNotStarred_") {
      feedIdFilter.append(QString("starred = 0 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsUnreadStar_") {
      feedIdFilter.append(QString("(read < 2 OR starred = 1) AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsLastDay_") {
      feedIdFilter.append(QString("(published >= datetime('now', '-1 day')) AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsLastWeek_") {
      feedIdFilter.append(QString("(published >= datetime('now', '-7 day')) AND deleted = 0"));
    }
    widget->newsModel_->setFilter(feedIdFilter);

    if (widget->newsModel_->rowCount() != 0) {
      while (widget->newsModel_->canFetchMore())
        widget->newsModel_->fetchMore();
    }

    currentNewsTab->loadNewspaper();

    // focus feed has displayed before
    int newsRow = -1;
    int newsId = widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();
    if (openingFeedAction_ == 0) {
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
      QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
      if (indexList.count()) newsRow = indexList.first().row();
    } else if (openingFeedAction_ == 1) newsRow = 0;

    widget->newsView_->setCurrentIndex(widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("title")));
    if (newsRow == -1) widget->newsView_->verticalScrollBar()->setValue(newsRow);

    if ((openingFeedAction_ < 2) && openNewsWebViewOn_) {
      widget->slotNewsViewSelected(widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("title")));
    } else {
      widget->slotNewsViewSelected(widget->newsModel_->index(-1, widget->newsModel_->fieldIndex("title")));
      QSqlQuery q;
      QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").
          arg(newsId).arg(feedId);
      q.exec(qStr);
    }
  }
}

/** @brief Open news using Enter key
 *---------------------------------------------------------------------------*/
void MainWindow::slotOpenNewsWebView()
{
  if (!newsView_->hasFocus()) return;
  currentNewsTab->slotNewsViewSelected(newsView_->currentIndex());
}
// ----------------------------------------------------------------------------
void MainWindow::slotNewsUpPressed()
{
  currentNewsTab->slotNewsUpPressed();
}
// ----------------------------------------------------------------------------
void MainWindow::slotNewsDownPressed()
{
  currentNewsTab->slotNewsDownPressed();
}
// ----------------------------------------------------------------------------
void MainWindow::slotNewsPageUpPressed()
{
  currentNewsTab->slotNewsPageUpPressed();
}
// ----------------------------------------------------------------------------
void MainWindow::slotNewsPageDownPressed()
{
  currentNewsTab->slotNewsPageDownPressed();
}
// ----------------------------------------------------------------------------
void MainWindow::markNewsRead()
{
  currentNewsTab->markNewsRead();
}
// ----------------------------------------------------------------------------
void MainWindow::markAllNewsRead()
{
  currentNewsTab->markAllNewsRead();
}
// ----------------------------------------------------------------------------
void MainWindow::markNewsStar()
{
  currentNewsTab->markNewsStar();
}
// ----------------------------------------------------------------------------
void MainWindow::deleteNews()
{
  currentNewsTab->deleteNews();
}
// ----------------------------------------------------------------------------
void MainWindow::deleteAllNewsList()
{
  currentNewsTab->deleteAllNewsList();
}
// ----------------------------------------------------------------------------
void MainWindow::restoreNews()
{
  currentNewsTab->restoreNews();
}
// ----------------------------------------------------------------------------
void MainWindow::openInExternalBrowserNews()
{
  currentNewsTab->openInExternalBrowserNews();
}

/** @brief Copy news URL-link
 *----------------------------------------------------------------------------*/
void MainWindow::slotCopyLinkNews()
{
  currentNewsTab->slotCopyLinkNews();
}

void MainWindow::updateNewsActionStates(bool hasSelection, bool hasNews,
                                        bool hasArticle)
{
  openDescriptionNewsAct_->setEnabled(hasSelection);
  openInExternalBrowserAct_->setEnabled(hasSelection);
  markNewsRead_->setEnabled(hasSelection);
  markStarAct_->setEnabled(hasSelection);
  newsLabelAction_->setEnabled(hasSelection);
  newsLabelMenuAction_->setEnabled(hasSelection);
  shareMenuAct_->setEnabled(hasSelection);
  deleteNewsAct_->setEnabled(hasSelection);
  restoreNewsAct_->setEnabled(hasSelection);
  copyLinkAct_->setEnabled(hasSelection);

  for (QAction *action : newsLabelGroup_->actions())
    action->setEnabled(hasSelection);
  for (QAction *action : shareGroup_->actions())
    action->setEnabled(hasSelection);

  markAllNewsRead_->setEnabled(hasNews);
  deleteAllNewsAct_->setEnabled(hasNews);

  autoLoadImagesToggle_->setEnabled(hasArticle);
  browserZoomMenu_->setEnabled(hasArticle);
  for (QAction *action : browserZoomGroup_->actions())
    action->setEnabled(hasArticle);
  printAct_->setEnabled(hasArticle);
  printPreviewAct_->setEnabled(hasArticle);
  savePageAsAct_->setEnabled(hasArticle);
  pageUpWebViewAct_->setEnabled(hasArticle);
  pageDownWebViewAct_->setEnabled(hasArticle);
}

void MainWindow::slotShowLabelsMenu()
{
  currentNewsTab->showLabelsMenu();
}
// ----------------------------------------------------------------------------
void MainWindow::slotPageUpWebView()
{
  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
  QApplication::sendEvent(currentNewsTab->articleView_, &keyEvent);
}

void MainWindow::slotPageDownWebView()
{
  QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
  QApplication::sendEvent(currentNewsTab->articleView_, &keyEvent);
}
// ----------------------------------------------------------------------------
void MainWindow::setCurrentTab(int index, bool updateCurrentTab)
{
  updateCurrentTab_ = updateCurrentTab;
  tabBar_->setCurrentIndex(index);
  updateCurrentTab_ = true;
}

/** @brief Set focus to search field (CTRL+F)
 *---------------------------------------------------------------------------*/
void MainWindow::findText()
{
  if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    if (!currentNewsTab->findText_->hasFocus())
      currentNewsTab->findText_->setFocus();
    else
      newsView_->setFocus();
  }
}

/** @brief Zooming in browser
 *---------------------------------------------------------------------------*/
void MainWindow::browserZoom(QAction *action)
{
  if (currentNewsTab->type_ == NewsTabWidget::TabTypeDownloads) return;

  if (action->objectName() == "zoomInAct") {
    if (currentNewsTab->articleView_->zoomFactor() < 5.0)
      currentNewsTab->articleView_->setZoomFactor(currentNewsTab->articleView_->zoomFactor()+0.1);
  } else if (action->objectName() == "zoomOutAct") {
    if (currentNewsTab->articleView_->zoomFactor() > 0.3)
      currentNewsTab->articleView_->setZoomFactor(currentNewsTab->articleView_->zoomFactor()-0.1);
  } else {
    currentNewsTab->articleView_->setZoomFactor(1);
  }
}

/** @brief Print browser page
 *---------------------------------------------------------------------------*/
void MainWindow::slotPrint()
{
  if (currentNewsTab->type_ == NewsTabWidget::TabTypeDownloads) return;
  QPrinter printer;
  printer.setDocName(tr("Article"));
  QPrintDialog dialog(&printer, this);
  if (dialog.exec() == QDialog::Accepted) currentNewsTab->articleView_->print(&printer);
}

/** @brief Call print preview dialog
 *---------------------------------------------------------------------------*/
void MainWindow::slotPrintPreview()
{
  if (currentNewsTab->type_ == NewsTabWidget::TabTypeDownloads) return;
  QPrinter printer;
  printer.setDocName(tr("Article"));
  QPrintPreviewDialog dialog(&printer, this);
  dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowMaximizeButtonHint);
  dialog.resize(650, 800);
  connect(&dialog, &QPrintPreviewDialog::paintRequested, currentNewsTab->articleView_, &ArticleView::print);
  dialog.exec();
}

/** @brief Set specified label for news
 *---------------------------------------------------------------------------*/
void MainWindow::setLabelNews(QAction *action)
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;

  newsLabelAction_->setIcon(action->icon());
  newsLabelAction_->setToolTip(action->text());
  newsLabelAction_->setData(action->data());

  currentNewsTab->setLabelNews(action->data().toInt());
}

/** @brief Set last chosen label for news
 *---------------------------------------------------------------------------*/
void MainWindow::setDefaultLabelNews()
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;

  currentNewsTab->setLabelNews(newsLabelAction_->data().toInt());
}

/** @brief Get Label that belong to current news
 *---------------------------------------------------------------------------*/
void MainWindow::getLabelNews()
{
  for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
    newsLabelGroup_->actions().at(i)->setChecked(false);
  }

  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;

  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(
        newsModel_->fieldIndex("label"));
  if (!indexes.count()) return;

  if (indexes.count() == 1) {
    QModelIndex index = indexes.at(0);
    QStringList strLabelIdList = index.data(Qt::EditRole).toString().split(",", Qt::SkipEmptyParts);
    for (const QString &strLabelId : std::as_const(strLabelIdList)) {
      for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
        if (newsLabelGroup_->actions().at(i)->data().toString() == strLabelId)
          newsLabelGroup_->actions().at(i)->setChecked(true);
      }
    }
  } else {
    for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
      bool check = false;
      QString strLabelId = newsLabelGroup_->actions().at(i)->data().toString();
      for (int y = indexes.count()-1; y >= 0; --y) {
        QModelIndex index = indexes.at(y);
        QString strIdLabels = index.data(Qt::EditRole).toString();
        if (!strIdLabels.contains(QString(",%1,").arg(strLabelId))) {
          check = false;
          break;
        }
        check = true;
      }
      newsLabelGroup_->actions().at(i)->setChecked(check);
    }
  }
}

/** @brief Add tab widget to tabs stack widget
 *---------------------------------------------------------------------------*/
int MainWindow::addTab(NewsTabWidget *widget)
{
  int indexTab;
  if (openNewTabNextToActive_) {
    if (stackedWidget_->count()) tabBar_->insertTab(tabBar_->currentIndex()+1, "");
    indexTab = stackedWidget_->insertWidget(stackedWidget_->currentIndex()+1, widget);
  }
  else {
    if (stackedWidget_->count()) tabBar_->addTab("");
    indexTab = stackedWidget_->addWidget(widget);
  }
  tabBar_->setTabButton(indexTab,
                        QTabBar::LeftSide,
                        widget->newsTitleLabel_);

  return indexTab;
}

// ----------------------------------------------------------------------------
void MainWindow::reduceNewsList()
{
  currentNewsTab->reduceNewsList();
}
// ----------------------------------------------------------------------------
void MainWindow::increaseNewsList()
{
  currentNewsTab->increaseNewsList();
}

/** @brief Save browser current page to file
 *---------------------------------------------------------------------------*/
void MainWindow::slotSavePageAs()
{
  if (currentNewsTab->type_ == NewsTabWidget::TabTypeDownloads) return;

  QString fileName = "news_descriptions";
  if (newsLayout_ == 0) {
    if (fileName == "news_descriptions") {
      int row = currentNewsTab->newsView_->currentIndex().row();
      fileName = currentNewsTab->newsModel_->dataField(row, "title").toString();
    }
  } else {
    if (currentNewsTab->type_ == NewsTabWidget::TabTypeFeed) {
      QModelIndex feedIndex = feedsView_->currentIndex();
      feedIndex = feedsProxyModel_->mapToSource(feedIndex);
      fileName = feedsModel_->dataField(feedIndex, "text").toString();
    } else {
      fileName = categoriesTree_->currentItem()->text(0);
    }
  }
  QString title = fileName.trimmed();

  fileName = fileName.trimmed();
  fileName = fileName.replace(QRegularExpression("[:\"]", QRegularExpression::DotMatchesEverythingOption), "_");
  fileName = QDir::toNativeSeparators(QDir::homePath() + "/" + fileName);
  fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                          fileName,
                                          QString(tr("HTML Files (*.%1)") + ";;" + tr("Text files (*.%2)"))
                                          .arg("html").arg("txt"));
  if (fileName.isNull()) return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly)) {
    statusBar()->showMessage(tr("Save As: could not open file"), 3000);
    return;
  }
  QFileInfo fileInfo(fileName);
  if (fileInfo.suffix() == "txt") {
    file.write(currentNewsTab->articleView_->toPlainText().toUtf8());
  } else {
    QString html = currentNewsTab->articleView_->exportHtml();
    QRegularExpression reg("news_descriptions",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    html = html.replace(reg, title.toHtmlEscaped());
    reg.setPattern("<img class=\"quiterss-img\"[^>]+\\>");
    html = html.remove(reg);
    QTextCodec *codec = QTextCodec::codecForHtml(html.toUtf8(),
                                                 QTextCodec::codecForName("UTF-8"));
    file.write(codec->fromUnicode(html));
  }
  file.close();
}

/** @brief Restore last deleted news
 *---------------------------------------------------------------------------*/
void MainWindow::restoreLastNews()
{
  QSqlQuery q;
  q.exec("SELECT id, feedId FROM news WHERE deleted=1 AND deleteDate!='' ORDER BY deleteDate DESC");
  if (q.next()) {
    QModelIndex curIndex = newsView_->currentIndex();
    int newsIdCur = newsModel_->index(curIndex.row(), newsModel_->fieldIndex("id")).data().toInt();

    int newsId = q.value(0).toInt();
    int feedId = q.value(1).toInt();
    q.exec(QString("UPDATE news SET deleted=0, deleteDate='' WHERE id=='%1'").
           arg(newsId));

    newsModel_->select();

    while (newsModel_->canFetchMore())
      newsModel_->fetchMore();

    currentNewsTab->loadNewspaper(NewsTabWidget::RefreshWithPos);

    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);
    if (indexList.count()) {
      int newsRow = indexList.first().row();
      newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    }
    slotUpdateStatus(feedId);
    recountCategoryCounts();
  }
}

/** @brief Switch to next unread news
 *---------------------------------------------------------------------------*/
void MainWindow::nextUnreadNews()
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;
  newsView_->clearSelection();

  int newsRow = currentNewsTab->findUnreadNews(true);

  if (newsRow == -1) {
    if (currentNewsTab->type_ != NewsTabWidget::TabTypeFeed) return;

    QModelIndex indexPrevUnread = QModelIndex();
    if (feedsView_->currentIndex().isValid())
      indexPrevUnread = feedsView_->indexNextUnread(feedsView_->currentIndex(), 1);
    if (!indexPrevUnread.isValid()) {
      indexPrevUnread = feedsView_->indexNextUnread(QModelIndex(), 1);
    }
    if (indexPrevUnread.isValid()) {
      if (changeBehaviorActionNUN_)
        openingFeedAction_ = 4;
      else
        openingFeedAction_ = 3;
      feedsView_->clearSelection();
      feedsView_->setCurrentIndex(indexPrevUnread);
      slotFeedClicked(indexPrevUnread);

      if (tabBar_->currentIndex() != TAB_WIDGET_PERMANENT) {
        QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("read"));
        QModelIndexList indexList;
        if ((newsView_->header()->sortIndicatorOrder() == Qt::DescendingOrder) &&
            (openingFeedAction_ != 4))
          indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
        else
          indexList = newsModel_->match(index, Qt::EditRole, 0);

        if (!indexList.isEmpty()) newsRow = indexList.last().row();

        // Focus feed news that displayed before
        newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
        if (newsRow == -1) newsView_->verticalScrollBar()->setValue(newsRow);

        if (openNewsWebViewOn_) {
          currentNewsTab->slotNewsViewSelected(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
        }
      }

      openingFeedAction_ = AppSettings::openingFeedAction.get();
    }
    return;
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (newsRow > (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(newsRow - pageStep/2);

  QModelIndex index = newsModel_->index(newsRow, newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(index);
  currentNewsTab->slotNewsViewSelected(index);
}

/** @brief Switch to previous unread news
 *---------------------------------------------------------------------------*/
void MainWindow::prevUnreadNews()
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;
  newsView_->clearSelection();

  int newsRow = currentNewsTab->findUnreadNews(false);

  int newsRowCur = newsView_->currentIndex().row();
  if ((newsRow >= newsRowCur) || (newsRow == -1)) {
    if (currentNewsTab->type_ != NewsTabWidget::TabTypeFeed) return;

    QModelIndex indexNextUnread =
        feedsView_->indexNextUnread(feedsView_->currentIndex(), 2);
    if (indexNextUnread.isValid()) {
      openingFeedAction_ = 3;
      feedsView_->clearSelection();
      feedsView_->setCurrentIndex(indexNextUnread);
      slotFeedClicked(indexNextUnread);
      openingFeedAction_ = AppSettings::openingFeedAction.get();
    }
    return;
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (newsRow < (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(newsRow - pageStep/2);

  QModelIndex index = newsModel_->index(newsRow, newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(index);
  currentNewsTab->slotNewsViewSelected(index);
}

/** @brief Process news sharing
 *---------------------------------------------------------------------------*/
void MainWindow::slotShareNews(QAction *action)
{
  if (action->property("missingShareConfiguration").toBool()) {
    QMessageBox::information(this, tr("Article sharing"), shareConfigurationMessage_);
    return;
  }
  currentNewsTab->slotShareNews(action);
}
// ----------------------------------------------------------------------------
void MainWindow::showMenuShareNews()
{
  if (mainToolbar_->widgetForAction(shareMenuAct_)) {
    QWidget *widget = mainToolbar_->widgetForAction(shareMenuAct_);
    if (widget->underMouse()) {
      shareMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
    }
  }
  if (feedsToolBar_->widgetForAction(shareMenuAct_)) {
    QWidget *widget = feedsToolBar_->widgetForAction(shareMenuAct_);
    if (widget->underMouse()) {
      shareMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
    }
  }
  if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    if (currentNewsTab->newsToolBar_->widgetForAction(shareMenuAct_)) {
      QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(shareMenuAct_);
      if (widget->underMouse()) {
        shareMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
      }
    }
  }
}

// ----------------------------------------------------------------------------
void MainWindow::showNewsMenu()
{
  if (currentNewsTab)
    newsSortByMenu_->setEnabled(currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads);
}
// ----------------------------------------------------------------------------
void MainWindow::showNewsSortByMenu()
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;

  QListIterator<QAction *> iter(newsSortByColumnGroup_->actions());
  while (iter.hasNext()) {
    QAction *nextAction = iter.next();
    delete nextAction;
  }

  int section = currentNewsTab->newsHeader_->sortIndicatorSection();
  iter = currentNewsTab->newsHeader_->viewMenu_->actions();
  while (iter.hasNext()) {
    QAction *nextAction = iter.next();
    QAction *newsSortByColumnAct = new QAction(this);
    newsSortByColumnAct->setCheckable(true);
    newsSortByColumnAct->setText(nextAction->text());
    newsSortByColumnAct->setData(nextAction->data());
    if (nextAction->data().toInt() == section) {
      newsSortByColumnAct->setChecked(true);
    }
    newsSortByColumnGroup_->addAction(newsSortByColumnAct);
  }
  newsSortByMenu_->insertActions(newsSortByMenu_->actions().at(0),
                                 newsSortByColumnGroup_->actions());

  if (currentNewsTab->newsHeader_->sortIndicatorOrder() == Qt::AscendingOrder) {
    newsSortOrderGroup_->actions().at(0)->setChecked(true);
  } else {
    newsSortOrderGroup_->actions().at(1)->setChecked(true);
  }
}
// ----------------------------------------------------------------------------
void MainWindow::setNewsSortByColumn()
{
  if (currentNewsTab->type_ >= NewsTabWidget::TabTypeDownloads) return;

  int lIdx = newsSortByColumnGroup_->checkedAction()->data().toInt();
  if (newsSortOrderGroup_->actions().at(0)->isChecked()) {
    currentNewsTab->newsHeader_->setSortIndicator(lIdx, Qt::AscendingOrder);
  } else {
    currentNewsTab->newsHeader_->setSortIndicator(lIdx, Qt::DescendingOrder);
  }
}

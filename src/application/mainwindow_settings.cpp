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
#include "applicationstyle.h"
#include "common.h"
#include "mainapplication.h"
#include "settings.h"
#include "trayiconcontroller.h"
#include "updatefeeds.h"
#include <QScreen>
#include <utility>

namespace {
QSize virtualDesktopSize()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  return screen ? screen->virtualGeometry().size() : QSize();
}

const char *colorSettingKey(int row)
{
  static const char *keys[] = {
    "feedsListTextColor", "feedsListBackgroundColor", "newsListTextColor",
    "newsListBackgroundColor", "focusedNewsTextColor", "focusedNewsBGColor",
    "linkColor", "titleColor", "dateColor", "authorColor", "newsTextColor",
    "newsTitleBackgroundColor", "newsBackgroundColor", "feedWithNewNewsColor",
    "countNewsUnreadColor", "newNewsTextColor", "unreadNewsTextColor",
    "focusedFeedTextColor", "focusedFeedBGColor", "feedDisabledUpdateColor",
    "alternatingRowColors", "notifierTextColor", "notifierBackgroundColor"
  };
  return row >= 0 && row < int(sizeof(keys) / sizeof(keys[0])) ? keys[row] : "";
}
}

/** @brief Load settings from ini-file
 *---------------------------------------------------------------------------*/
void MainWindow::loadSettings()
{
  Settings settings("Settings");

  showSplashScreen_ = AppSettings::showSplashScreen.get();
  reopenFeedStartup_ = settings.value("reopenFeedStartup", true).toBool();
  openNewTabNextToActive_ = settings.value("openNewTabNextToActive", false).toBool();

  showTrayIcon_ = settings.value("showTrayIcon", true).toBool();
#ifndef Q_OS_MAC
  startingTray_ = settings.value("startingTray", false).toBool();
  minimizingTray_ = settings.value("minimizingTray", true).toBool();
  closingTray_ = settings.value("closingTray", false).toBool();
#else
  startingTray_ = false;
  minimizingTray_ = false;
  closingTray_ = false;
#endif
  singleClickTray_ = settings.value("singleClickTray", false).toBool();
  clearStatusNew_ = settings.value("clearStatusNew", true).toBool();
  emptyWorking_ = settings.value("emptyWorking", true).toBool();

  QString fontFamily = settings.value("feedsFontFamily", qApp->font().family()).toString();
  int fontSize = settings.value("feedsFontSize", qApp->font().pointSize()).toInt();
  feedsView_->setFont(QFont(fontFamily, fontSize));
  feedsModel_->font_ = feedsView_->font();
  categoriesTree_->setFont(QFont(fontFamily, fontSize));

  newsListFontFamily_ = settings.value("newsFontFamily", qApp->font().family()).toString();
  newsListFontSize_ = settings.value("newsFontSize", qApp->font().pointSize()).toInt();
  newsTitleFontFamily_ = settings.value("newsTitleFontFamily", qApp->font().family()).toString();
  newsTitleFontSize_ = settings.value("newsTitleFontSize", qApp->font().pointSize()+2).toInt();
  newsTextFontFamily_ = settings.value("newsTextFontFamily", qApp->font().family()).toString();
  newsTextFontSize_ = settings.value("newsTextFontSize", qApp->font().pointSize()+2).toInt();
  notificationFontFamily_ = settings.value("notificationFontFamily", qApp->font().family()).toString();
  notificationFontSize_ = settings.value("notificationFontSize", qApp->font().pointSize()).toInt();

  updateFeedsEnable_ = settings.value("autoUpdatefeeds", false).toBool();
  updateFeedsInterval_ = settings.value("autoUpdatefeedsTime", 10).toInt();
  updateFeedsIntervalType_ = settings.value("autoUpdatefeedsInterval", 0).toInt();

  openingFeedAction_ = AppSettings::openingFeedAction.get();
  openNewsWebViewOn_ = AppSettings::openNewsWebViewOn.get();

  markNewsReadOn_ = settings.value("markNewsReadOn", true).toBool();
  markCurNewsRead_ = settings.value("markCurNewsRead", true).toBool();
  markNewsReadTime_ = settings.value("markNewsReadTime", 0).toInt();
  markPrevNewsRead_= settings.value("markPrevNewsRead", false).toBool();
  markReadSwitchingFeed_ = settings.value("markReadSwitchingFeed", false).toBool();
  markReadClosingTab_ = settings.value("markReadClosingTab", false).toBool();
  markReadMinimize_ = settings.value("markReadMinimize", false).toBool();

  formatDate_ = settings.value("formatData", "dd.MM.yy").toString();
  formatTime_ = settings.value("formatTime", "hh:mm").toString();
  feedsModel_->formatDate_ = formatDate_;
  feedsModel_->formatTime_ = formatTime_;

  alternatingRowColorsNews_ = settings.value("alternatingColorsNews", false).toBool();
  changeBehaviorActionNUN_ = settings.value("changeBehaviorActionNUN", false).toBool();
  simplifiedDateTime_ = settings.value("simplifiedDateTime", true).toBool();
  notDeleteStarred_ = settings.value("notDeleteStarred", false).toBool();
  notDeleteLabeled_ = settings.value("notDeleteLabeled", false).toBool();
  markIdenticalNewsRead_ = settings.value("markIdenticalNewsRead", true).toBool();
  avoidOldNews_ = settings.value("avoidOldNews", false).toBool();
  avoidedOldNewsDate_ = settings.value("avoidedOldNewsDate").toDate();

  mainNewsFilter_ = settings.value("mainNewsFilter", "filterNewsAll_").toString();

  cleanupOnShutdown_ = settings.value("cleanupOnShutdown", true).toBool();
  maxDayCleanUp_ = settings.value("maxDayClearUp", 30).toInt();
  maxNewsCleanUp_ = settings.value("maxNewsClearUp", 200).toInt();
  dayCleanUpOn_ = settings.value("dayClearUpOn", true).toBool();
  newsCleanUpOn_ = settings.value("newsClearUpOn", true).toBool();
  readCleanUp_ = settings.value("readClearUp", false).toBool();
  neverUnreadCleanUp_ = settings.value("neverUnreadClearUp", true).toBool();
  neverStarCleanUp_ = settings.value("neverStarClearUp", true).toBool();
  neverLabelCleanUp_ = settings.value("neverLabelClearUp", true).toBool();
  cleanUpDeleted_ = settings.value("cleanUpDeleted", false).toBool();
  optimizeDB_ = settings.value("optimizeDB", false).toBool();

  downloadLocation_ = settings.value("downloadLocation", "").toString();
  askDownloadLocation_ = settings.value("askDownloadLocation", true).toBool();
  defaultZoomPages_ = settings.value("defaultZoomPages", 100).toInt();
  autoLoadImages_ = settings.value("autoLoadImages", true).toBool();
  preloadNewspaperPages_ = qBound(0, settings.value("preloadNewspaperPages", 2).toInt(), 10);

  soundNewNews_ = settings.value("soundNewNews", true).toBool();
  soundNotifyPath_ = settings.value("soundNotifyPath", mainApp->soundNotifyDefaultFile()).toString();
  showNotifyOn_ = settings.value("showNotifyOn", true).toBool();
  screenNotify_ = settings.value("screenNotify", 0).toInt();
  positionNotify_ = settings.value("positionNotify", 3).toInt();
  transparencyNotify_ = settings.value("transparencyNotify", 60).toInt();
  countShowNewsNotify_ = settings.value("countShowNewsNotify", 10).toInt();
  widthTitleNewsNotify_ = settings.value("widthTitleNewsNotify", 300).toInt();
  timeShowNewsNotify_ = settings.value("timeShowNewsNotify", 10).toInt();
  fullscreenModeNotify_ = settings.value("fullscreenModeNotify", true).toBool();
  showNotifyInactiveApp_ = settings.value("showNotifyInactiveApp", true).toBool();
  onlySelectedFeeds_ = settings.value("onlySelectedFeeds", false).toBool();
  showTitlesFeedsNotify_ = settings.value("showTitlesFeedsNotify", true).toBool();
  showIconFeedNotify_ = settings.value("showIconFeedNotify", false).toBool();
  showButtonMarkAllNotify_ = settings.value("showButtonMarkAllNotify", true).toBool();
  showButtonMarkReadNotify_ = settings.value("showButtonMarkReadNotify", true).toBool();
  showButtonExBrowserNotify_ = settings.value("showButtonExBrowserNotify", true).toBool();
  showButtonDeleteNotify_ = settings.value("showButtonDeleteNotify", true).toBool();
  closeNotify_ = settings.value("closeNotify", true).toBool();

  toolBarLockAct_->setChecked(settings.value("mainToolbarLock", true).toBool());
  lockMainToolbar(toolBarLockAct_->isChecked());

  mainToolbarToggle_->setChecked(settings.value("mainToolbarShow2", false).toBool());
  feedsToolbarToggle_->setChecked(settings.value("feedsToolbarShow2", true).toBool());
  newsToolbarToggle_->setChecked(settings.value("newsToolbarShow", true).toBool());
  categoriesPanelToggle_->setChecked(settings.value("categoriesPanelShow", true).toBool());
  categoriesWidget_->setVisible(categoriesPanelToggle_->isChecked());
  statusBarToggle_->setChecked(settings.value("statusBarShow", true).toBool());

  QString str = settings.value("mainToolBar",
                               "newAct,Separator,updateFeedAct,updateAllFeedsAct,"
                               "Separator,markFeedRead,Separator,autoLoadImagesToggle").toString();

  for (const QString &actionStr : str.split(",", Qt::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      mainToolbar_->addSeparator();
    } else {
      QListIterator<QAction *> iter(actions());
      while (iter.hasNext()) {
        QAction *pAction = iter.next();
        if (!pAction->icon().isNull()) {
          if (pAction->objectName() == actionStr) {
            mainToolbar_->addAction(pAction);
            break;
          }
        }
      }
    }
  }

  str = settings.value("feedsToolBar2",
                               "newAct,Separator,updateAllFeedsAct,markFeedRead,"
                               "Separator,feedsFilter,findFeedAct").toString();

  for (const QString &actionStr : str.split(",", Qt::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      feedsToolBar_->addSeparator();
    } else {
      QListIterator<QAction *> iter(actions());
      while (iter.hasNext()) {
        QAction *pAction = iter.next();
        if (!pAction->icon().isNull()) {
          if (pAction->objectName() == actionStr) {
            feedsToolBar_->addAction(pAction);
            break;
          }
        }
      }
    }
  }

  setToolBarStyle(AppSettings::toolBarStyle.get());
  QString iconStr = AppSettings::toolBarIconSize.get();
  setToolBarIconSize(mainToolbar_, iconStr);
  iconStr = AppSettings::feedsToolBarIconSize.get();
  setToolBarIconSize(feedsToolBar_, iconStr);

  showUnreadCount_->setChecked(settings.value("showUnreadCount", true).toBool());
  showUndeleteCount_->setChecked(settings.value("showUndeleteCount", false).toBool());
  showLastUpdated_->setChecked(settings.value("showLastUpdated", false).toBool());
  feedsColumnVisible(showUnreadCount_);
  feedsColumnVisible(showUndeleteCount_);
  feedsColumnVisible(showLastUpdated_);

  indentationFeedsTreeAct_->setChecked(settings.value("indentationFeedsTree", true).toBool());
  slotIndentationFeedsTree();

  newsLayout_ = settings.value("newsLayout", 0).toInt();
  switch (newsLayout_) {
  case 1:
    newspaperLayoutAct_->setChecked(true);
    layoutToggle_->setIcon(QIcon(":/images/layout_newspaper"));
    break;
  default:
    classicLayoutAct_->setChecked(true);
    layoutToggle_->setIcon(QIcon(":/images/layout_classic"));
  }
  connect(layoutGroup_, &QActionGroup::triggered, this, QOverload<QAction*>::of(&MainWindow::setNewsLayout));

  browserPosition_ = settings.value("browserPosition", BOTTOM_POSITION).toInt();
  switch (browserPosition_) {
  case TOP_POSITION:   topBrowserPositionAct_->setChecked(true); break;
  case RIGHT_POSITION: rightBrowserPositionAct_->setChecked(true); break;
  case LEFT_POSITION:  leftBrowserPositionAct_->setChecked(true); break;
  default: bottomBrowserPositionAct_->setChecked(true);
  }

  stayOnTopAct_->setChecked(settings.value("stayOnTop", false).toBool());
  if (stayOnTopAct_->isChecked())
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  else
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);

#ifdef HAVE_X11
  if (allWorkspacesAct_) {
    allWorkspacesAct_->setChecked(settings.value("allWorkspaces", false).toBool());
    if (allWorkspacesAct_->isChecked()) {
      QTimer::singleShot(0, this, [this]() { setAllWorkspaces(); });
    }
  }
#endif

  hideFeedsOpenTab_ = settings.value("hideFeedsOpenTab", false).toBool();
  showToggleFeedsTree_ = settings.value("showToggleFeedsTree", true).toBool();
  pushButtonNull_->setVisible(showToggleFeedsTree_);

  defaultIconFeeds_ = settings.value("defaultIconFeeds", false).toBool();
  feedsModel_->defaultIconFeeds_ = defaultIconFeeds_;
  feedsView_->autocollapseFolder_ =
      settings.value("autocollapseFolder", false).toBool();

#ifndef Q_OS_MAC
  showMenuBarAct_->setChecked(settings.value("showMenuBar", false).toBool());
#else
  showMenuBarAct_->setChecked(true);
#endif

  // [Color] stores only values that differ from the active theme defaults.
  // Older versions wrote every color, so those values are preserved as
  // overrides until the user changes theme (which clears the group).
  setThemeColorDefaults();
  Settings colorSettings("Color");
  for (int row = 0; row < 23; ++row) {
    const QString key = QString::fromLatin1(colorSettingKey(row));
    if (!colorSettings.contains(key)) continue;
    customColorRows_.insert(row);
    setColorValueForOption(row, colorSettings.value(key, colorValueForOption(row)).toString());
  }
  feedsView_->setStyleSheet(feedsModel_->backgroundColor_.isEmpty()
      ? QString() : QString("#feedsView_ {background: %1;}").arg(feedsModel_->backgroundColor_));


  Settings stateSettings;
  resize(800, 600);
  restoreGeometry(stateSettings.value("GeometryState").toByteArray());
  restoreState(stateSettings.value("ToolBarsState").toByteArray());

  if (!mainToolbarToggle_->isChecked())
    mainToolbar_->hide();
  if (!feedsToolbarToggle_->isChecked())
    feedsPanel_->hide();
  if (!statusBarToggle_->isChecked())
    statusBar()->hide();

  mainSplitter_->restoreState(stateSettings.value("MainSplitterState").toByteArray());
  feedsWidgetVisibleAct_->setChecked(stateSettings.value("FeedsWidgetVisible", true).toBool());
  slotVisibledFeedsWidget();

  feedsWidgetSplitterState_ = stateSettings.value("FeedsWidgetSplitterState").toByteArray();
  bool showCategories = stateSettings.value("NewsCategoriesTreeVisible", true).toBool();
  categoriesTree_->setVisible(showCategories);
  if (showCategories) {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
    showCategoriesButton_->setToolTip(tr("Hide Categories"));
    feedsSplitter_->restoreState(feedsWidgetSplitterState_);
  } else {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_show.png"));
    showCategoriesButton_->setToolTip(tr("Show Categories"));
    QList <int> sizes;
    sizes << virtualDesktopSize().height() << 20;
    feedsSplitter_->setSizes(sizes);
  }
  bool expandCategories = stateSettings.value("categoriesTreeExpanded", true).toBool();
  if (expandCategories)
    categoriesTree_->expandAll();

  showMenuBar();

}

/** @brief Save settings in ini-file
 *---------------------------------------------------------------------------*/
void MainWindow::saveSettings()
{
  Settings settings("Settings");

  AppSettings::showSplashScreen.set(showSplashScreen_);
  settings.setValue("reopenFeedStartup", reopenFeedStartup_);
  settings.setValue("openNewTabNextToActive", openNewTabNextToActive_);

  settings.setValue("createLastFeed", mainApp->isSaveDataLastFeed());

  settings.setValue("showTrayIcon", showTrayIcon_);
  settings.setValue("startingTray", startingTray_);
  settings.setValue("minimizingTray", minimizingTray_);
  settings.setValue("closingTray", closingTray_);
  settings.setValue("behaviorIconTray", behaviorIconTray_);
  settings.setValue("singleClickTray", singleClickTray_);
  settings.setValue("clearStatusNew", clearStatusNew_);
  settings.setValue("emptyWorking", emptyWorking_);

  settings.setValue("langFileName", mainApp->language());

  QString fontFamily = feedsView_->font().family();
  settings.setValue("feedsFontFamily", fontFamily);
  int fontSize = feedsView_->font().pointSize();
  settings.setValue("feedsFontSize", fontSize);

  settings.setValue("newsFontFamily", newsListFontFamily_);
  settings.setValue("newsFontSize", newsListFontSize_);
  settings.setValue("newsTitleFontFamily", newsTitleFontFamily_);
  settings.setValue("newsTitleFontSize", newsTitleFontSize_);
  settings.setValue("newsTextFontFamily", newsTextFontFamily_);
  settings.setValue("newsTextFontSize", newsTextFontSize_);
  settings.setValue("notificationFontFamily", notificationFontFamily_);
  settings.setValue("notificationFontSize", notificationFontSize_);

  settings.setValue("autoUpdatefeeds", updateFeedsEnable_);
  settings.setValue("autoUpdatefeedsTime", updateFeedsInterval_);
  settings.setValue("autoUpdatefeedsInterval", updateFeedsIntervalType_);

  AppSettings::openingFeedAction.set(openingFeedAction_);
  AppSettings::openNewsWebViewOn.set(openNewsWebViewOn_);

  settings.setValue("markNewsReadOn", markNewsReadOn_);
  settings.setValue("markCurNewsRead", markCurNewsRead_);
  settings.setValue("markNewsReadTime", markNewsReadTime_);
  settings.setValue("markPrevNewsRead", markPrevNewsRead_);
  settings.setValue("markReadSwitchingFeed", markReadSwitchingFeed_);
  settings.setValue("markReadClosingTab", markReadClosingTab_);
  settings.setValue("markReadMinimize", markReadMinimize_);

  settings.setValue("formatData", formatDate_);
  settings.setValue("formatTime", formatTime_);

  settings.setValue("alternatingColorsNews", alternatingRowColorsNews_);
  settings.setValue("changeBehaviorActionNUN", changeBehaviorActionNUN_);
  settings.setValue("simplifiedDateTime", simplifiedDateTime_);
  settings.setValue("notDeleteStarred", notDeleteStarred_);
  settings.setValue("notDeleteLabeled", notDeleteLabeled_);
  settings.setValue("markIdenticalNewsRead", markIdenticalNewsRead_);
  settings.setValue("avoidOldNews", avoidOldNews_);
  settings.setValue("avoidedOldNewsDate", avoidedOldNewsDate_);

  settings.setValue("mainNewsFilter", mainNewsFilter_);

  settings.setValue("cleanupOnShutdown", cleanupOnShutdown_);
  settings.setValue("maxDayClearUp", maxDayCleanUp_);
  settings.setValue("maxNewsClearUp", maxNewsCleanUp_);
  settings.setValue("dayClearUpOn", dayCleanUpOn_);
  settings.setValue("newsClearUpOn", newsCleanUpOn_);
  settings.setValue("readClearUp", readCleanUp_);
  settings.setValue("neverUnreadClearUp", neverUnreadCleanUp_);
  settings.setValue("neverStarClearUp", neverStarCleanUp_);
  settings.setValue("neverLabelClearUp", neverLabelCleanUp_);
  settings.setValue("cleanUpDeleted", cleanUpDeleted_);
  settings.setValue("optimizeDB", optimizeDB_);

  settings.setValue("downloadLocation", downloadLocation_);
  settings.setValue("askDownloadLocation", askDownloadLocation_);
  settings.setValue("defaultZoomPages", defaultZoomPages_);
  settings.setValue("autoLoadImages", autoLoadImages_);
  settings.setValue("preloadNewspaperPages", preloadNewspaperPages_);

  settings.setValue("soundNewNews", soundNewNews_);
  settings.setValue("soundNotifyPath", soundNotifyPath_);
  settings.setValue("showNotifyOn", showNotifyOn_);
  settings.setValue("screenNotify", screenNotify_);
  settings.setValue("positionNotify", positionNotify_);
  settings.setValue("transparencyNotify", transparencyNotify_);
  settings.setValue("countShowNewsNotify", countShowNewsNotify_);
  settings.setValue("widthTitleNewsNotify", widthTitleNewsNotify_);
  settings.setValue("timeShowNewsNotify", timeShowNewsNotify_);
  settings.setValue("fullscreenModeNotify", fullscreenModeNotify_);
  settings.setValue("showNotifyInactiveApp", showNotifyInactiveApp_);
  settings.setValue("onlySelectedFeeds", onlySelectedFeeds_);
  settings.setValue("showTitlesFeedsNotify", showTitlesFeedsNotify_);
  settings.setValue("showIconFeedNotify", showIconFeedNotify_);
  settings.setValue("showButtonMarkAllNotify", showButtonMarkAllNotify_);
  settings.setValue("showButtonMarkReadNotify", showButtonMarkReadNotify_);
  settings.setValue("showButtonExBrowserNotify", showButtonExBrowserNotify_);
  settings.setValue("showButtonDeleteNotify", showButtonDeleteNotify_);
  settings.setValue("closeNotify", closeNotify_);

  settings.setValue("mainToolbarLock", toolBarLockAct_->isChecked());
#ifdef HAVE_X11
  if (allWorkspacesAct_)
    settings.setValue("allWorkspaces", allWorkspacesAct_->isChecked());
#endif

  settings.setValue("mainToolbarShow2", mainToolbarToggle_->isChecked());
  settings.setValue("feedsToolbarShow2", feedsToolbarToggle_->isChecked());
  settings.setValue("newsToolbarShow", newsToolbarToggle_->isChecked());
  settings.setValue("categoriesPanelShow", categoriesPanelToggle_->isChecked());
  settings.setValue("statusBarShow", statusBarToggle_->isChecked());

  settings.setValue("theme", mainApp->applicationStyle().id);

  settings.setValue("showUnreadCount", showUnreadCount_->isChecked());
  settings.setValue("showUndeleteCount", showUndeleteCount_->isChecked());
  settings.setValue("showLastUpdated", showLastUpdated_->isChecked());

  settings.setValue("indentationFeedsTree", indentationFeedsTreeAct_->isChecked());

  settings.setValue("newsLayout", newsLayout_);
  settings.setValue("browserPosition", browserPosition_);

  settings.setValue("stayOnTop", stayOnTopAct_->isChecked());

  settings.setValue("hideFeedsOpenTab", hideFeedsOpenTab_);
  settings.setValue("showToggleFeedsTree", showToggleFeedsTree_);

  settings.setValue("defaultIconFeeds", defaultIconFeeds_);
  settings.setValue("autocollapseFolder", feedsView_->autocollapseFolder_);

  settings.setValue("showMenuBar", showMenuBarAct_->isChecked());

  Settings().remove("Color");
  if (!customColorRows_.isEmpty()) {
    Settings colorSettings("Color");
    for (int row : customColorRows_)
      colorSettings.setValue(QString::fromLatin1(colorSettingKey(row)), colorValueForOption(row));
  }


  Settings stateSettings;
  stateSettings.setValue("GeometryState", saveGeometry());
  stateSettings.setValue("ToolBarsState", saveState());

  stateSettings.setValue("MainSplitterState", mainSplitter_->saveState());
  stateSettings.setValue("FeedsWidgetVisible", showFeedsTabPermanent_);

  bool newsCategoriesTreeVisible = true;
  if (categoriesWidget_->height() <= (categoriesPanel_->height()+2)) {
    newsCategoriesTreeVisible = false;
    stateSettings.setValue("FeedsWidgetSplitterState", feedsWidgetSplitterState_);
  } else {
    stateSettings.setValue("FeedsWidgetSplitterState", feedsSplitter_->saveState());
  }
  stateSettings.setValue("NewsCategoriesTreeVisible", newsCategoriesTreeVisible);
  stateSettings.setValue("categoriesTreeExpanded", categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->isExpanded());

  if (stackedWidget_->count()) {
    NewsTabWidget *widget;
    if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads)
      widget = currentNewsTab;
    else
      widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));

    widget->newsHeader_->saveStateColumns(widget);
    stateSettings.setValue("NewsTabSplitterState",
                      widget->newsTabWidgetSplitter_->saveState());
  }

  NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
  stateSettings.setValue("feedSettings/currentId", widget->feedId_);
  stateSettings.setValue("feedSettings/filterName",
                    feedsFilterGroup_->checkedAction()->objectName());
  stateSettings.setValue("newsSettings/filterName",
                    newsFilterGroup_->checkedAction()->objectName());
}

void MainWindow::loadSettingsFeeds()
{
  markCurNewsRead_ = false;
  Settings settings;
  behaviorIconTray_ = settings.value("Settings/behaviorIconTray", NEW_COUNT_ICON_TRAY).toInt();

  QString filterName = settings.value("feedSettings/filterName", "filterFeedsAll_").toString();
  QList<QAction*> listActions = feedsFilterGroup_->actions();
  for (QAction *action : std::as_const(listActions)) {
    if (action->objectName() == filterName) {
      action->setChecked(true);
      break;
    }
  }
  filterName = settings.value("newsSettings/filterName", "filterNewsAll_").toString();
  listActions = newsFilterGroup_->actions();
  for (QAction *action : std::as_const(listActions)) {
    if (action->objectName() == filterName) {
      action->setChecked(true);
      break;
    }
  }

  setFeedsFilter(false);
}

void MainWindow::showOptionDlg(int index)
{
  static int pageIndex = 0;
  Settings settings;

  if (index != -1) pageIndex = index;

  if (optionsDialog_) {
    optionsDialog_->activateWindow();
    return;
  }

  optionsDialog_ = new OptionsDialog(this);

  bool updateFeedsStartUp = AppSettings::autoUpdatefeedsStartUp.get();

  optionsDialog_->showSplashScreen_->setChecked(showSplashScreen_);
  optionsDialog_->reopenFeedStartup_->setChecked(reopenFeedStartup_);
  optionsDialog_->openNewTabNextToActive_->setChecked(openNewTabNextToActive_);
  optionsDialog_->hideFeedsOpenTab_->setChecked(hideFeedsOpenTab_);
  optionsDialog_->showToggleFeedsTree_->setChecked(showToggleFeedsTree_);
  optionsDialog_->defaultIconFeeds_->setChecked(defaultIconFeeds_);
  optionsDialog_->autocollapseFolder_->setChecked(feedsView_->autocollapseFolder_);
  bool showCloseButtonTab = AppSettings::showCloseButtonTab.get();
  optionsDialog_->showCloseButtonTab_->setChecked(showCloseButtonTab);

  bool updateCheckEnabled = AppSettings::updateCheckEnabled.get();
  optionsDialog_->updateCheckEnabled_->setChecked(updateCheckEnabled);

  bool storeDBMemory_ = AppSettings::storeDBMemory.get();
  optionsDialog_->storeDBMemory_->setChecked(storeDBMemory_);
  int saveDBMemFileInterval = AppSettings::saveDBMemFileInterval.get();
  optionsDialog_->saveDBMemFileInterval_->setValue(saveDBMemFileInterval);

  optionsDialog_->showTrayIconBox_->setChecked(showTrayIcon_);
  optionsDialog_->startingTray_->setChecked(startingTray_);
  optionsDialog_->minimizingTray_->setChecked(minimizingTray_);
  optionsDialog_->closingTray_->setChecked(closingTray_);
  optionsDialog_->setBehaviorIconTray(behaviorIconTray_);
  optionsDialog_->singleClickTray_->setChecked(singleClickTray_);
  optionsDialog_->clearStatusNew_->setChecked(clearStatusNew_);
  optionsDialog_->emptyWorking_->setChecked(emptyWorking_);

  optionsDialog_->setProxy(mainApp->networkProxy());

  int timeoutRequest = AppSettings::timeoutRequest.get();
  int numberRequests = AppSettings::numberRequest.get();
  int numberRepeats = AppSettings::numberRepeats.get();
  optionsDialog_->timeoutRequest_->setValue(timeoutRequest);
  optionsDialog_->numberRequests_->setValue(numberRequests);
  optionsDialog_->numberRepeats_->setValue(numberRepeats);

  optionsDialog_->autoLoadImages_->setChecked(autoLoadImages_);
  optionsDialog_->preloadNewspaperPages_->setValue(preloadNewspaperPages_);
  optionsDialog_->preloadNewspaperPages_->setEnabled(autoLoadImages_);
  optionsDialog_->defaultZoomPages_->setValue(defaultZoomPages_);
  const int browserMode = AppSettings::externalBrowserOn.get();
  optionsDialog_->customExternalBrowser_->setChecked(browserMode == 2 || browserMode == -1);
  optionsDialog_->externalBrowser_->setText(AppSettings::externalBrowser.get());
  optionsDialog_->returnFocusAfterLink_->setChecked(AppSettings::returnFocusAfterLink.get());

  optionsDialog_->downloadLocationEdit_->setText(downloadLocation_);
  optionsDialog_->askDownloadLocation_->setChecked(askDownloadLocation_);

  optionsDialog_->updateFeedsStartUp_->setChecked(updateFeedsStartUp);
  optionsDialog_->updateFeedsEnable_->setChecked(updateFeedsEnable_);
  optionsDialog_->updateIntervalType_->setCurrentIndex(updateFeedsIntervalType_+1);
  optionsDialog_->updateFeedsInterval_->setValue(updateFeedsInterval_);

  optionsDialog_->setOpeningFeed(openingFeedAction_);
  optionsDialog_->openNewsWebViewOn_->setChecked(openNewsWebViewOn_);

  optionsDialog_->markNewsReadOn_->setChecked(markNewsReadOn_);
  optionsDialog_->markCurNewsRead_->setChecked(markCurNewsRead_);
  optionsDialog_->markNewsReadTime_->setValue(markNewsReadTime_);
  optionsDialog_->markPrevNewsRead_->setChecked(markPrevNewsRead_);
  optionsDialog_->markReadSwitchingFeed_->setChecked(markReadSwitchingFeed_);
  optionsDialog_->markReadClosingTab_->setChecked(markReadClosingTab_);
  optionsDialog_->markReadMinimize_->setChecked(markReadMinimize_);

  for (int i = 0; i < optionsDialog_->formatDate_->count(); i++) {
    if (optionsDialog_->formatDate_->itemData(i).toString() == formatDate_) {
      optionsDialog_->formatDate_->setCurrentIndex(i);
      break;
    }
  }
  for (int i = 0; i < optionsDialog_->formatTime_->count(); i++) {
    if (optionsDialog_->formatTime_->itemData(i).toString() == formatTime_) {
      optionsDialog_->formatTime_->setCurrentIndex(i);
      break;
    }
  }

  optionsDialog_->alternatingRowColorsNews_->setChecked(alternatingRowColorsNews_);
  optionsDialog_->changeBehaviorActionNUN_->setChecked(changeBehaviorActionNUN_);
  optionsDialog_->simplifiedDateTime_->setChecked(simplifiedDateTime_);
  optionsDialog_->notDeleteStarred_->setChecked(notDeleteStarred_);
  optionsDialog_->notDeleteLabeled_->setChecked(notDeleteLabeled_);
  optionsDialog_->markIdenticalNewsRead_->setChecked(markIdenticalNewsRead_);
  optionsDialog_->avoidedOldNewsDateOn_->setChecked(avoidOldNews_);

  if (!avoidedOldNewsDate_.isNull() && avoidedOldNewsDate_.isValid()) {
    optionsDialog_->avoidedOldNewsDate_->setSelectedDate(avoidedOldNewsDate_);
  } else {
    optionsDialog_->avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
  }

  for (int i = 0; i < optionsDialog_->mainNewsFilter_->count(); i++) {
    if (optionsDialog_->mainNewsFilter_->itemData(i).toString() == mainNewsFilter_) {
      optionsDialog_->mainNewsFilter_->setCurrentIndex(i);
      break;
    }
  }

  QString styleSheetNews = settings.value("Settings/styleSheetNews",
                                          mainApp->styleSheetNewsDefaultFile()).toString();
  optionsDialog_->styleSheetNewsEdit_->setText(styleSheetNews);

  optionsDialog_->cleanupOnShutdownBox_->setChecked(cleanupOnShutdown_);
  optionsDialog_->dayCleanUpOn_->setChecked(dayCleanUpOn_);
  optionsDialog_->maxDayCleanUp_->setValue(maxDayCleanUp_);
  optionsDialog_->newsCleanUpOn_->setChecked(newsCleanUpOn_);
  optionsDialog_->maxNewsCleanUp_->setValue(maxNewsCleanUp_);
  optionsDialog_->readCleanUp_->setChecked(readCleanUp_);
  optionsDialog_->neverUnreadCleanUp_->setChecked(neverUnreadCleanUp_);
  optionsDialog_->neverStarCleanUp_->setChecked(neverStarCleanUp_);
  optionsDialog_->neverLabelCleanUp_->setChecked(neverLabelCleanUp_);
  optionsDialog_->cleanUpDeleted_->setChecked(cleanUpDeleted_);
  optionsDialog_->optimizeDB_->setChecked(optimizeDB_);

  optionsDialog_->soundNotifyBox_->setChecked(soundNewNews_);
  optionsDialog_->editSoundNotifer_->setText(soundNotifyPath_);
  optionsDialog_->showNotifyOn_->setChecked(showNotifyOn_);
  optionsDialog_->screenNotify_->setCurrentIndex(screenNotify_+1);
  optionsDialog_->positionNotify_->setCurrentIndex(positionNotify_);
  optionsDialog_->transparencyNotify_->setValue(transparencyNotify_);
  optionsDialog_->countShowNewsNotify_->setValue(countShowNewsNotify_);
  optionsDialog_->widthTitleNewsNotify_->setValue(widthTitleNewsNotify_);
  optionsDialog_->timeShowNewsNotify_->setValue(timeShowNewsNotify_);
  optionsDialog_->fullscreenModeNotify_->setChecked(fullscreenModeNotify_);
  optionsDialog_->showNotifyInactiveApp_->setChecked(showNotifyInactiveApp_);
  optionsDialog_->onlySelectedFeeds_->setChecked(onlySelectedFeeds_);
  optionsDialog_->showTitlesFeedsNotify_->setChecked(showTitlesFeedsNotify_);
  optionsDialog_->showIconFeedNotify_->setChecked(showIconFeedNotify_);
  optionsDialog_->showButtonMarkAllNotify_->setChecked(showButtonMarkAllNotify_);
  optionsDialog_->showButtonMarkReadNotify_->setChecked(showButtonMarkReadNotify_);
  optionsDialog_->showButtonExBrowserNotify_->setChecked(showButtonExBrowserNotify_);
  optionsDialog_->showButtonDeleteNotify_->setChecked(showButtonDeleteNotify_);
  optionsDialog_->closeNotify_->setChecked(closeNotify_);

  optionsDialog_->setLanguage(mainApp->language());

  QString strFont = QString("%1, %2").
      arg(feedsView_->font().family()).
      arg(feedsView_->font().pointSize());
  optionsDialog_->fontsTree_->topLevelItem(0)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsListFontFamily_).arg(newsListFontSize_);
  optionsDialog_->fontsTree_->topLevelItem(1)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsTitleFontFamily_).arg(newsTitleFontSize_);
  optionsDialog_->fontsTree_->topLevelItem(2)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsTextFontFamily_).arg(newsTextFontSize_);
  optionsDialog_->fontsTree_->topLevelItem(3)->setText(2, strFont);
  strFont = QString("%1, %2").arg(notificationFontFamily_).arg(notificationFontSize_);
  optionsDialog_->fontsTree_->topLevelItem(4)->setText(2, strFont);

  QPixmap pixmapColor(14, 14);
  pixmapColor.fill(feedsModel_->textColor_);
  optionsDialog_->colorsTree_->topLevelItem(0)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(0)->setText(1, feedsModel_->textColor_);
  if (feedsModel_->backgroundColor_.isEmpty())
    pixmapColor.fill(QColor(0, 0, 0, 0));
  else
    pixmapColor.fill(feedsModel_->backgroundColor_);
  optionsDialog_->colorsTree_->topLevelItem(1)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(1)->setText(1, feedsModel_->backgroundColor_);
  pixmapColor.fill(newsListTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(2)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(2)->setText(1, newsListTextColor_);

  if (newsListBackgroundColor_.isEmpty())
    pixmapColor.fill(QColor(0, 0, 0, 0));
  else
    pixmapColor.fill(newsListBackgroundColor_);
  optionsDialog_->colorsTree_->topLevelItem(3)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(3)->setText(1, newsListBackgroundColor_);
  pixmapColor.fill(focusedNewsTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(4)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(4)->setText(1, focusedNewsTextColor_);
  if (focusedNewsBGColor_.isEmpty())
    pixmapColor.fill(QColor(0, 0, 0, 0));
  else
    pixmapColor.fill(focusedNewsBGColor_);
  optionsDialog_->colorsTree_->topLevelItem(5)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(5)->setText(1, focusedNewsBGColor_);
  pixmapColor.fill(linkColor_);
  optionsDialog_->colorsTree_->topLevelItem(6)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(6)->setText(1, linkColor_);
  pixmapColor.fill(titleColor_);
  optionsDialog_->colorsTree_->topLevelItem(7)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(7)->setText(1, titleColor_);
  pixmapColor.fill(dateColor_);
  optionsDialog_->colorsTree_->topLevelItem(8)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(8)->setText(1, dateColor_);
  pixmapColor.fill(authorColor_);
  optionsDialog_->colorsTree_->topLevelItem(9)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(9)->setText(1, authorColor_);
  pixmapColor.fill(newsTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(10)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(10)->setText(1, newsTextColor_);
  pixmapColor.fill(newsTitleBackgroundColor_);
  optionsDialog_->colorsTree_->topLevelItem(11)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(11)->setText(1, newsTitleBackgroundColor_);
  pixmapColor.fill(newsBackgroundColor_);
  optionsDialog_->colorsTree_->topLevelItem(12)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(12)->setText(1, newsBackgroundColor_);
  pixmapColor.fill(feedsModel_->feedWithNewNewsColor_);
  optionsDialog_->colorsTree_->topLevelItem(13)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(13)->setText(1, feedsModel_->feedWithNewNewsColor_);
  pixmapColor.fill(feedsModel_->countNewsUnreadColor_);
  optionsDialog_->colorsTree_->topLevelItem(14)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(14)->setText(1, feedsModel_->countNewsUnreadColor_);

  pixmapColor.fill(newNewsTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(15)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(15)->setText(1, newNewsTextColor_);
  pixmapColor.fill(unreadNewsTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(16)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(16)->setText(1, unreadNewsTextColor_);

  pixmapColor.fill(feedsModel_->focusedFeedTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(17)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(17)->setText(1, feedsModel_->focusedFeedTextColor_);
  if (feedsModel_->focusedFeedBGColor_.isEmpty())
    pixmapColor.fill(QColor(0, 0, 0, 0));
  else
    pixmapColor.fill(feedsModel_->focusedFeedBGColor_);
  optionsDialog_->colorsTree_->topLevelItem(18)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(18)->setText(1, feedsModel_->focusedFeedBGColor_);
  pixmapColor.fill(feedsModel_->feedDisabledUpdateColor_);
  optionsDialog_->colorsTree_->topLevelItem(19)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(19)->setText(1, feedsModel_->feedDisabledUpdateColor_);

  pixmapColor.fill(alternatingRowColors_);
  optionsDialog_->colorsTree_->topLevelItem(20)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(20)->setText(1, alternatingRowColors_);

  pixmapColor.fill(notifierTextColor_);
  optionsDialog_->colorsTree_->topLevelItem(21)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(21)->setText(1, notifierTextColor_);
  pixmapColor.fill(notifierBackgroundColor_);
  optionsDialog_->colorsTree_->topLevelItem(22)->setIcon(0, pixmapColor);
  optionsDialog_->colorsTree_->topLevelItem(22)->setText(1, notifierBackgroundColor_);

  optionsDialog_->loadActionShortcut(shortcutRegistry_.actions(), &shortcutRegistry_.defaults());

  // Display setting dialog

  optionsDialog_->setCurrentItem(pageIndex);
  int result = optionsDialog_->exec();
  pageIndex = optionsDialog_->currentIndex();

  if (result == QDialog::Rejected) {
    delete optionsDialog_;
    optionsDialog_ = nullptr;
    return;
  }

  // Apply accepted settings

  // Removing actions changes the registry; iterate over its original contents.
  const auto shortcutActions = shortcutRegistry_.actions();
  for (QAction *action : shortcutActions) {
    QString objectName = action->objectName();
    if (objectName.contains("labelAction_")) {
      shortcutRegistry_.removeOne(action);
      delete action;
    }
  }
  optionsDialog_->saveActionShortcut(shortcutRegistry_.actions(), newsLabelGroup_);
  shortcutRegistry_.append(newsLabelGroup_->actions());
  newsLabelMenu_->addActions(newsLabelGroup_->actions());
  this->addActions(newsLabelGroup_->actions());
  if (newsLabelGroup_->actions().count()) {
    newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
    newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
    newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
  }

  if (optionsDialog_->idLabels_.count()) {
    QTreeWidgetItem *labelTreeItem = categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem);
    while (labelTreeItem->childCount()) {
      labelTreeItem->removeChild(labelTreeItem->child(0));
    }

    bool closeTab = true;
    int indexTab = -1;
    int tabLabelId = -1;
    for (int i = 0; i < stackedWidget_->count(); i++) {
      NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
      if (widget->type_ == NewsTabWidget::TabTypeLabel) {
        indexTab = i;
        tabLabelId = widget->labelId_;
        break;
      }
    }

    QSqlQuery q;
    q.exec("SELECT id, name, image, currentNews, num, color_bg, color_text FROM labels ORDER BY num");
    while (q.next()) {
      int idLabel = q.value(0).toInt();
      QString nameLabel = q.value(1).toString();
      QByteArray byteArray = q.value(2).toByteArray();
      QString currentNews = q.value(3).toString();
      QPixmap imageLabel;
      if (!byteArray.isNull())
        imageLabel.loadFromData(byteArray);
      QStringList dataItem;
      dataItem << nameLabel << QString::number(NewsTabWidget::TabTypeLabel)
               << QString::number(idLabel) << currentNews;
      QTreeWidgetItem *childItem = new QTreeWidgetItem(dataItem);
      childItem->setIcon(0, QIcon(imageLabel));
      childItem->setData(0, CategoriesTreeWidget::ImageRole, q.value(2));
      childItem->setData(0, CategoriesTreeWidget::NumRole, q.value(4));
      childItem->setData(0, CategoriesTreeWidget::colorBgRole, q.value(5));
      childItem->setData(0, CategoriesTreeWidget::colorTextRole, q.value(6));
      labelTreeItem->addChild(childItem);

      if (idLabel == tabLabelId) {
        closeTab = false;
        NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(indexTab));
        // Set icon and title for tab has opened
        widget->newsIconTitle_->setPixmap(imageLabel);
        widget->setTextTab(nameLabel);
      }
    }

    if (closeTab && (indexTab > 0) && (tabLabelId > 0)) {
      slotCloseTab(indexTab);
    }
    if ((tabBar_->currentIndex() == indexTab) && (indexTab > 0) && (tabLabelId == 0)) {
      slotUpdateNews(NewsTabWidget::RefreshWithPos);
    }
  }

  showSplashScreen_ = optionsDialog_->showSplashScreen_->isChecked();
  reopenFeedStartup_ = optionsDialog_->reopenFeedStartup_->isChecked();
  openNewTabNextToActive_ = optionsDialog_->openNewTabNextToActive_->isChecked();
  hideFeedsOpenTab_ = optionsDialog_->hideFeedsOpenTab_->isChecked();
  showToggleFeedsTree_ = optionsDialog_->showToggleFeedsTree_->isChecked();
  defaultIconFeeds_ = optionsDialog_->defaultIconFeeds_->isChecked();
  feedsModel_->defaultIconFeeds_ = defaultIconFeeds_;
  feedsView_->autocollapseFolder_ = optionsDialog_->autocollapseFolder_->isChecked();

  showCloseButtonTab = optionsDialog_->showCloseButtonTab_->isChecked();
  AppSettings::showCloseButtonTab.set(showCloseButtonTab);

  pushButtonNull_->setVisible(showToggleFeedsTree_);

  updateCheckEnabled = optionsDialog_->updateCheckEnabled_->isChecked();
  AppSettings::updateCheckEnabled.set(updateCheckEnabled);

  storeDBMemory_ = optionsDialog_->storeDBMemory_->isChecked();
  AppSettings::storeDBMemory.set(storeDBMemory_);
  if (saveDBMemFileInterval != optionsDialog_->saveDBMemFileInterval_->value()) {
    saveDBMemFileInterval = optionsDialog_->saveDBMemFileInterval_->value();
    AppSettings::saveDBMemFileInterval.set(saveDBMemFileInterval);
    mainApp->updateFeeds()->startSaveTimer();
  }

  const bool wasTrayIconEnabled = showTrayIcon_;
  showTrayIcon_ = optionsDialog_->showTrayIconBox_->isChecked();
  startingTray_ = optionsDialog_->startingTray_->isChecked();
  minimizingTray_ = optionsDialog_->minimizingTray_->isChecked();
  closingTray_ = optionsDialog_->closingTray_->isChecked();
  behaviorIconTray_ = optionsDialog_->behaviorIconTray();
  if (behaviorIconTray_ > CHANGE_ICON_TRAY) {
    emit signalRefreshInfoTray();
  } else {
    trayIconController_->showDefaultIcon();
  }
  singleClickTray_ = optionsDialog_->singleClickTray_->isChecked();
  clearStatusNew_ = optionsDialog_->clearStatusNew_->isChecked();
  emptyWorking_ = optionsDialog_->emptyWorking_->isChecked();
  if (showTrayIcon_) {
    trayIconController_->show();
  } else {
    trayIconController_->hide();
    if (wasTrayIconEnabled && isHidden())
      showWindows();
  }

  mainApp->proxySaveSettings(optionsDialog_->proxy());

  timeoutRequest = optionsDialog_->timeoutRequest_->value();
  numberRequests = optionsDialog_->numberRequests_->value();
  numberRepeats = optionsDialog_->numberRepeats_->value();
  AppSettings::timeoutRequest.set(timeoutRequest);
  AppSettings::numberRequest.set(numberRequests);
  AppSettings::numberRepeats.set(numberRepeats);

  autoLoadImages_ = optionsDialog_->autoLoadImages_->isChecked();
  preloadNewspaperPages_ = optionsDialog_->preloadNewspaperPages_->value();
  AppSettings::externalBrowserOn.set(optionsDialog_->customExternalBrowser_->isChecked() ? 2 : 1);
  AppSettings::externalBrowser.set(optionsDialog_->externalBrowser_->text());
  AppSettings::returnFocusAfterLink.set(optionsDialog_->returnFocusAfterLink_->isChecked());
  if (!optionsDialog_->returnFocusAfterLink_->isChecked()) returnFocusAfterLinkTimer_.stop();
  defaultZoomPages_ = optionsDialog_->defaultZoomPages_->value();

  downloadLocation_ = optionsDialog_->downloadLocationEdit_->text();
  askDownloadLocation_ = optionsDialog_->askDownloadLocation_->isChecked();

  updateFeedsStartUp = optionsDialog_->updateFeedsStartUp_->isChecked();
  updateFeedsEnable_ = optionsDialog_->updateFeedsEnable_->isChecked();
  updateFeedsInterval_ = optionsDialog_->updateFeedsInterval_->value();
  updateFeedsIntervalType_ = optionsDialog_->updateIntervalType_->currentIndex()-1;


  openingFeedAction_ = optionsDialog_->getOpeningFeed();
  openNewsWebViewOn_ = optionsDialog_->openNewsWebViewOn_->isChecked();

  markNewsReadOn_ = optionsDialog_->markNewsReadOn_->isChecked();
  markCurNewsRead_ = optionsDialog_->markCurNewsRead_->isChecked();
  markNewsReadTime_ = optionsDialog_->markNewsReadTime_->value();
  markPrevNewsRead_ = optionsDialog_->markPrevNewsRead_->isChecked();
  markReadSwitchingFeed_ = optionsDialog_->markReadSwitchingFeed_->isChecked();
  markReadClosingTab_ = optionsDialog_->markReadClosingTab_->isChecked();
  markReadMinimize_ = optionsDialog_->markReadMinimize_->isChecked();

  formatDate_ = optionsDialog_->formatDate_->itemData(
        optionsDialog_->formatDate_->currentIndex()).toString();
  feedsModel_->formatDate_ = formatDate_;
  formatTime_ = optionsDialog_->formatTime_->itemData(
        optionsDialog_->formatTime_->currentIndex()).toString();
  feedsModel_->formatTime_ = formatTime_;

  alternatingRowColorsNews_ = optionsDialog_->alternatingRowColorsNews_->isChecked();
  changeBehaviorActionNUN_ = optionsDialog_->changeBehaviorActionNUN_->isChecked();
  simplifiedDateTime_ = optionsDialog_->simplifiedDateTime_->isChecked();
  notDeleteStarred_ = optionsDialog_->notDeleteStarred_->isChecked();
  notDeleteLabeled_ = optionsDialog_->notDeleteLabeled_->isChecked();
  markIdenticalNewsRead_ = optionsDialog_->markIdenticalNewsRead_->isChecked();
  avoidOldNews_ = optionsDialog_->avoidedOldNewsDateOn_->isChecked();

  if (!optionsDialog_->avoidedOldNewsDate_->selectedDate().isNull() && optionsDialog_->avoidedOldNewsDate_->selectedDate().isValid()) {
    avoidedOldNewsDate_ = optionsDialog_->avoidedOldNewsDate_->selectedDate();
  } else {
    avoidedOldNewsDate_ = QDate::currentDate();
  }

  mainNewsFilter_ = optionsDialog_->mainNewsFilter_->itemData(
        optionsDialog_->mainNewsFilter_->currentIndex()).toString();

  styleSheetNews = optionsDialog_->styleSheetNewsEdit_->text();
  if (styleSheetNews.isEmpty()) styleSheetNews = mainApp->styleSheetNewsDefaultFile();
  settings.setValue("Settings/styleSheetNews", styleSheetNews);

  cleanupOnShutdown_ = optionsDialog_->cleanupOnShutdownBox_->isChecked();
  dayCleanUpOn_ = optionsDialog_->dayCleanUpOn_->isChecked();
  maxDayCleanUp_ = optionsDialog_->maxDayCleanUp_->value();
  newsCleanUpOn_ = optionsDialog_->newsCleanUpOn_->isChecked();
  maxNewsCleanUp_ = optionsDialog_->maxNewsCleanUp_->value();
  readCleanUp_ = optionsDialog_->readCleanUp_->isChecked();
  neverUnreadCleanUp_ = optionsDialog_->neverUnreadCleanUp_->isChecked();
  neverStarCleanUp_ = optionsDialog_->neverStarCleanUp_->isChecked();
  neverLabelCleanUp_ = optionsDialog_->neverLabelCleanUp_->isChecked();
  cleanUpDeleted_ = optionsDialog_->cleanUpDeleted_->isChecked();
  optimizeDB_ = optionsDialog_->optimizeDB_->isChecked();

  soundNewNews_ = optionsDialog_->soundNotifyBox_->isChecked();
  soundNotifyPath_ = optionsDialog_->editSoundNotifer_->text();
  showNotifyOn_ = optionsDialog_->showNotifyOn_->isChecked();
  screenNotify_ = optionsDialog_->screenNotify_->currentIndex()-1;
  positionNotify_ = optionsDialog_->positionNotify_->currentIndex();
  transparencyNotify_ = optionsDialog_->transparencyNotify_->value();
  countShowNewsNotify_ = optionsDialog_->countShowNewsNotify_->value();
  widthTitleNewsNotify_ = optionsDialog_->widthTitleNewsNotify_->value();
  timeShowNewsNotify_ = optionsDialog_->timeShowNewsNotify_->value();
  fullscreenModeNotify_ = optionsDialog_->fullscreenModeNotify_->isChecked();
  showNotifyInactiveApp_ = optionsDialog_->showNotifyInactiveApp_->isChecked();
  onlySelectedFeeds_ = optionsDialog_->onlySelectedFeeds_->isChecked();
  showTitlesFeedsNotify_ = optionsDialog_->showTitlesFeedsNotify_->isChecked();
  showIconFeedNotify_ = optionsDialog_->showIconFeedNotify_->isChecked();
  showButtonMarkAllNotify_ = optionsDialog_->showButtonMarkAllNotify_->isChecked();
  showButtonMarkReadNotify_ = optionsDialog_->showButtonMarkReadNotify_->isChecked();
  showButtonExBrowserNotify_ = optionsDialog_->showButtonExBrowserNotify_->isChecked();
  showButtonDeleteNotify_ = optionsDialog_->showButtonDeleteNotify_->isChecked();
  closeNotify_ = optionsDialog_->closeNotify_->isChecked();

  mainApp->setLanguage(optionsDialog_->language());
  mainApp->setTranslateApplication();

  QFont font = feedsView_->font();
  font.setFamily(
        optionsDialog_->fontsTree_->topLevelItem(0)->text(2).section(", ", 0, 0));
  font.setPointSize(
        optionsDialog_->fontsTree_->topLevelItem(0)->text(2).section(", ", 1).toInt());
  feedsView_->setFont(font);
  feedsModel_->font_ = font;
  categoriesTree_->setFont(font);

  newsListFontFamily_ = optionsDialog_->fontsTree_->topLevelItem(1)->text(2).section(", ", 0, 0);
  newsListFontSize_ = optionsDialog_->fontsTree_->topLevelItem(1)->text(2).section(", ", 1).toInt();
  newsTitleFontFamily_ = optionsDialog_->fontsTree_->topLevelItem(2)->text(2).section(", ", 0, 0);
  newsTitleFontSize_ = optionsDialog_->fontsTree_->topLevelItem(2)->text(2).section(", ", 1).toInt();
  newsTextFontFamily_ = optionsDialog_->fontsTree_->topLevelItem(3)->text(2).section(", ", 0, 0);
  newsTextFontSize_ = optionsDialog_->fontsTree_->topLevelItem(3)->text(2).section(", ", 1).toInt();
  notificationFontFamily_ = optionsDialog_->fontsTree_->topLevelItem(4)->text(2).section(", ", 0, 0);
  notificationFontSize_ = optionsDialog_->fontsTree_->topLevelItem(4)->text(2).section(", ", 1).toInt();

  feedsModel_->textColor_ = optionsDialog_->colorsTree_->topLevelItem(0)->text(1);
  feedsModel_->backgroundColor_ = optionsDialog_->colorsTree_->topLevelItem(1)->text(1);
  feedsView_->setStyleSheet(feedsModel_->backgroundColor_.isEmpty()
      ? QString() : QString("#feedsView_ {background: %1;}").arg(feedsModel_->backgroundColor_));
  newsListTextColor_ = optionsDialog_->colorsTree_->topLevelItem(2)->text(1);
  newsListBackgroundColor_ = optionsDialog_->colorsTree_->topLevelItem(3)->text(1);
  focusedNewsTextColor_ = optionsDialog_->colorsTree_->topLevelItem(4)->text(1);
  focusedNewsBGColor_ = optionsDialog_->colorsTree_->topLevelItem(5)->text(1);
  linkColor_ = optionsDialog_->colorsTree_->topLevelItem(6)->text(1);
  titleColor_ = optionsDialog_->colorsTree_->topLevelItem(7)->text(1);
  dateColor_ = optionsDialog_->colorsTree_->topLevelItem(8)->text(1);
  authorColor_ = optionsDialog_->colorsTree_->topLevelItem(9)->text(1);
  newsTextColor_ = optionsDialog_->colorsTree_->topLevelItem(10)->text(1);
  newsTitleBackgroundColor_ = optionsDialog_->colorsTree_->topLevelItem(11)->text(1);
  newsBackgroundColor_ = optionsDialog_->colorsTree_->topLevelItem(12)->text(1);
  feedsModel_->feedWithNewNewsColor_ = optionsDialog_->colorsTree_->topLevelItem(13)->text(1);
  feedsModel_->countNewsUnreadColor_ = optionsDialog_->colorsTree_->topLevelItem(14)->text(1);
  newNewsTextColor_ = optionsDialog_->colorsTree_->topLevelItem(15)->text(1);
  unreadNewsTextColor_ = optionsDialog_->colorsTree_->topLevelItem(16)->text(1);
  feedsModel_->focusedFeedTextColor_ = optionsDialog_->colorsTree_->topLevelItem(17)->text(1);
  feedsModel_->focusedFeedBGColor_ = optionsDialog_->colorsTree_->topLevelItem(18)->text(1);
  feedsModel_->feedDisabledUpdateColor_ = optionsDialog_->colorsTree_->topLevelItem(19)->text(1);
  alternatingRowColors_ = optionsDialog_->colorsTree_->topLevelItem(20)->text(1);
  notifierTextColor_ = optionsDialog_->colorsTree_->topLevelItem(21)->text(1);
  notifierBackgroundColor_ = optionsDialog_->colorsTree_->topLevelItem(22)->text(1);
  for (int row = 0; row < optionsDialog_->colorsTree_->topLevelItemCount(); ++row) {
    if (colorValueForOption(row) == defaultColorForOption(row))
      customColorRows_.remove(row);
    else
      customColorRows_.insert(row);
  }

  delete optionsDialog_;
  optionsDialog_ = nullptr;

  AppSettings::autoUpdatefeedsStartUp.set(updateFeedsStartUp);

  saveSettings();
  emit articleSettingsChanged(markIdenticalNewsRead_, avoidOldNews_, avoidedOldNewsDate_);
  shortcutRegistry_.save();

  if (currentNewsTab != nullptr) {
    if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads)
      currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
    currentNewsTab->setSettings(false);
  }
  refreshApplicationAppearance();
}

void MainWindow::showSettingPageLabels()
{
  showOptionDlg(5);
}

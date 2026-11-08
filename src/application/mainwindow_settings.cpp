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
#include <QElapsedTimer>
#include <QScreen>
#include <utility>

namespace {
QSize virtualDesktopSize()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  return screen ? screen->virtualGeometry().size() : QSize();
}

void selectComboData(QComboBox *combo, const QString &value)
{
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toString() == value) {
      combo->setCurrentIndex(i);
      break;
    }
  }
}

void populateToolbarActions(QToolBar *toolbar, const QList<QAction *> &actions, const QString &spec)
{
  for (const QString &actionStr : spec.split(",", Qt::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      toolbar->addSeparator();
    } else {
      for (QAction *pAction : actions) {
        if (!pAction->icon().isNull() && pAction->objectName() == actionStr) {
          toolbar->addAction(pAction);
          break;
        }
      }
    }
  }
}
}

struct MainWindow::SettingBindings {
  struct BoolCheckSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QCheckBox *OptionsDialog::*widget;
  };

  struct BoolRadioSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QRadioButton *OptionsDialog::*widget;
  };

  struct BoolGroupSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QGroupBox *OptionsDialog::*widget;
  };

  struct IntSpinSetting {
    const char *key;
    int MainWindow::*member;
    int defaultValue;
    QSpinBox *OptionsDialog::*widget;
  };

  struct IntComboSetting {
    const char *key;
    int MainWindow::*member;
    int defaultValue;
    QComboBox *OptionsDialog::*widget;
    int indexOffset;
  };

  struct ComboDataSetting {
    const char *key;
    QString MainWindow::*member;
    const char *defaultValue;
    QComboBox *OptionsDialog::*widget;
  };

  struct ActionBoolSetting {
    const char *key;
    QAction *MainWindow::*action;
    bool defaultValue;
  };

  struct AppBoolCheckSetting {
    const AppSettings::Setting<bool> *setting;
    QCheckBox *OptionsDialog::*widget;
  };

  struct AppIntSpinSetting {
    const AppSettings::Setting<int> *setting;
    QSpinBox *OptionsDialog::*widget;
  };

  static const BoolCheckSetting kBoolCheck[];
  static const BoolCheckSetting kTrayHold[];
  static const BoolRadioSetting kBoolRadio[];
  static const BoolGroupSetting kBoolGroup[];
  static const IntSpinSetting kIntSpin[];
  static const IntComboSetting kIntCombo[];
  static const ComboDataSetting kComboData[];
  static const ActionBoolSetting kActionBool[];
  static const AppBoolCheckSetting kAppBoolCheck[];
  static const AppIntSpinSetting kAppIntSpin[];
};

const MainWindow::SettingBindings::BoolCheckSetting MainWindow::SettingBindings::kBoolCheck[] = {
  { "reopenFeedStartup",        &MainWindow::reopenFeedStartup_,        true,  &OptionsDialog::reopenFeedStartup_ },
  { "openNewTabNextToActive",   &MainWindow::openNewTabNextToActive_,   false, &OptionsDialog::openNewTabNextToActive_ },
  { "hideFeedsOpenTab",         &MainWindow::hideFeedsOpenTab_,         false, &OptionsDialog::hideFeedsOpenTab_ },
  { "showToggleFeedsTree",      &MainWindow::showToggleFeedsTree_,      true,  &OptionsDialog::showToggleFeedsTree_ },
  { "defaultIconFeeds",         &MainWindow::defaultIconFeeds_,         false, &OptionsDialog::defaultIconFeeds_ },
  { "singleClickTray",          &MainWindow::singleClickTray_,          false, &OptionsDialog::singleClickTray_ },
  { "clearStatusNew",           &MainWindow::clearStatusNew_,           true,  &OptionsDialog::clearStatusNew_ },
  { "emptyWorking",             &MainWindow::emptyWorking_,             true,  &OptionsDialog::emptyWorking_ },
  { "autoLoadImages",           &MainWindow::autoLoadImages_,           true,  &OptionsDialog::autoLoadImages_ },
  { "askDownloadLocation",      &MainWindow::askDownloadLocation_,      true,  &OptionsDialog::askDownloadLocation_ },
  { "autoUpdatefeeds",          &MainWindow::updateFeedsEnable_,        false, &OptionsDialog::updateFeedsEnable_ },
  { "markReadSwitchingFeed",    &MainWindow::markReadSwitchingFeed_,    false, &OptionsDialog::markReadSwitchingFeed_ },
  { "markReadClosingTab",       &MainWindow::markReadClosingTab_,       false, &OptionsDialog::markReadClosingTab_ },
  { "markReadMinimize",         &MainWindow::markReadMinimize_,         false, &OptionsDialog::markReadMinimize_ },
  { "alternatingColorsNews",    &MainWindow::alternatingRowColorsNews_, false, &OptionsDialog::alternatingRowColorsNews_ },
  { "changeBehaviorActionNUN",  &MainWindow::changeBehaviorActionNUN_,  false, &OptionsDialog::changeBehaviorActionNUN_ },
  { "simplifiedDateTime",       &MainWindow::simplifiedDateTime_,       true,  &OptionsDialog::simplifiedDateTime_ },
  { "notDeleteStarred",         &MainWindow::notDeleteStarred_,         false, &OptionsDialog::notDeleteStarred_ },
  { "notDeleteLabeled",         &MainWindow::notDeleteLabeled_,         false, &OptionsDialog::notDeleteLabeled_ },
  { "markIdenticalNewsRead",    &MainWindow::markIdenticalNewsRead_,    true,  &OptionsDialog::markIdenticalNewsRead_ },
  { "dayClearUpOn",             &MainWindow::dayCleanUpOn_,             true,  &OptionsDialog::dayCleanUpOn_ },
  { "newsClearUpOn",            &MainWindow::newsCleanUpOn_,            true,  &OptionsDialog::newsCleanUpOn_ },
  { "readClearUp",              &MainWindow::readCleanUp_,              false, &OptionsDialog::readCleanUp_ },
  { "neverUnreadClearUp",       &MainWindow::neverUnreadCleanUp_,       true,  &OptionsDialog::neverUnreadCleanUp_ },
  { "neverStarClearUp",         &MainWindow::neverStarCleanUp_,         true,  &OptionsDialog::neverStarCleanUp_ },
  { "neverLabelClearUp",        &MainWindow::neverLabelCleanUp_,        true,  &OptionsDialog::neverLabelCleanUp_ },
  { "cleanUpDeleted",           &MainWindow::cleanUpDeleted_,           false, &OptionsDialog::cleanUpDeleted_ },
  { "optimizeDB",               &MainWindow::optimizeDB_,               false, &OptionsDialog::optimizeDB_ },
  { "fullscreenModeNotify",     &MainWindow::fullscreenModeNotify_,     true,  &OptionsDialog::fullscreenModeNotify_ },
  { "showNotifyInactiveApp",    &MainWindow::showNotifyInactiveApp_,    true,  &OptionsDialog::showNotifyInactiveApp_ },
  { "onlySelectedFeeds",        &MainWindow::onlySelectedFeeds_,        false, &OptionsDialog::onlySelectedFeeds_ },
  { "showTitlesFeedsNotify",    &MainWindow::showTitlesFeedsNotify_,    true,  &OptionsDialog::showTitlesFeedsNotify_ },
  { "showIconFeedNotify",       &MainWindow::showIconFeedNotify_,       false, &OptionsDialog::showIconFeedNotify_ },
  { "showButtonMarkAllNotify",  &MainWindow::showButtonMarkAllNotify_,  true,  &OptionsDialog::showButtonMarkAllNotify_ },
  { "showButtonMarkReadNotify", &MainWindow::showButtonMarkReadNotify_, true,  &OptionsDialog::showButtonMarkReadNotify_ },
  { "showButtonExBrowserNotify",&MainWindow::showButtonExBrowserNotify_,true,  &OptionsDialog::showButtonExBrowserNotify_ },
  { "showButtonDeleteNotify",   &MainWindow::showButtonDeleteNotify_,   true,  &OptionsDialog::showButtonDeleteNotify_ },
  { "closeNotify",              &MainWindow::closeNotify_,              true,  &OptionsDialog::closeNotify_ },
};

const MainWindow::SettingBindings::BoolCheckSetting MainWindow::SettingBindings::kTrayHold[] = {
  { "startingTray",   &MainWindow::startingTray_,   false, &OptionsDialog::startingTray_ },
  { "minimizingTray", &MainWindow::minimizingTray_, true,  &OptionsDialog::minimizingTray_ },
  { "closingTray",    &MainWindow::closingTray_,    false, &OptionsDialog::closingTray_ },
};

const MainWindow::SettingBindings::BoolRadioSetting MainWindow::SettingBindings::kBoolRadio[] = {
  { "markCurNewsRead",  &MainWindow::markCurNewsRead_,  true,  &OptionsDialog::markCurNewsRead_ },
  { "markPrevNewsRead", &MainWindow::markPrevNewsRead_, false, &OptionsDialog::markPrevNewsRead_ },
};

const MainWindow::SettingBindings::BoolGroupSetting MainWindow::SettingBindings::kBoolGroup[] = {
  { "showTrayIcon",      &MainWindow::showTrayIcon_,      true,  &OptionsDialog::showTrayIconBox_ },
  { "markNewsReadOn",    &MainWindow::markNewsReadOn_,    true,  &OptionsDialog::markNewsReadOn_ },
  { "avoidOldNews",      &MainWindow::avoidOldNews_,      false, &OptionsDialog::avoidedOldNewsDateOn_ },
  { "cleanupOnShutdown", &MainWindow::cleanupOnShutdown_, true,  &OptionsDialog::cleanupOnShutdownBox_ },
  { "soundNewNews",      &MainWindow::soundNewNews_,      true,  &OptionsDialog::soundNotifyBox_ },
  { "showNotifyOn",      &MainWindow::showNotifyOn_,      true,  &OptionsDialog::showNotifyOn_ },
};

const MainWindow::SettingBindings::IntSpinSetting MainWindow::SettingBindings::kIntSpin[] = {
  { "defaultZoomPages",     &MainWindow::defaultZoomPages_,     100, &OptionsDialog::defaultZoomPages_ },
  { "autoUpdatefeedsTime",  &MainWindow::updateFeedsInterval_,  10,  &OptionsDialog::updateFeedsInterval_ },
  { "markNewsReadTime",     &MainWindow::markNewsReadTime_,     0,   &OptionsDialog::markNewsReadTime_ },
  { "maxDayClearUp",        &MainWindow::maxDayCleanUp_,        30,  &OptionsDialog::maxDayCleanUp_ },
  { "maxNewsClearUp",       &MainWindow::maxNewsCleanUp_,       200, &OptionsDialog::maxNewsCleanUp_ },
  { "transparencyNotify",   &MainWindow::transparencyNotify_,   60,  &OptionsDialog::transparencyNotify_ },
  { "countShowNewsNotify",  &MainWindow::countShowNewsNotify_,  10,  &OptionsDialog::countShowNewsNotify_ },
  { "widthTitleNewsNotify", &MainWindow::widthTitleNewsNotify_, 300, &OptionsDialog::widthTitleNewsNotify_ },
  { "timeShowNewsNotify",   &MainWindow::timeShowNewsNotify_,   10,  &OptionsDialog::timeShowNewsNotify_ },
};

const MainWindow::SettingBindings::IntComboSetting MainWindow::SettingBindings::kIntCombo[] = {
  { "autoUpdatefeedsInterval", &MainWindow::updateFeedsIntervalType_, 0, &OptionsDialog::updateIntervalType_, 1 },
  { "screenNotify",            &MainWindow::screenNotify_,            0, &OptionsDialog::screenNotify_,       1 },
  { "positionNotify",          &MainWindow::positionNotify_,          3, &OptionsDialog::positionNotify_,     0 },
};

const MainWindow::SettingBindings::ComboDataSetting MainWindow::SettingBindings::kComboData[] = {
  { "formatData",     &MainWindow::formatDate_,     "dd.MM.yy",       &OptionsDialog::formatDate_ },
  { "formatTime",     &MainWindow::formatTime_,     "hh:mm",          &OptionsDialog::formatTime_ },
  { "mainNewsFilter", &MainWindow::mainNewsFilter_, "filterNewsAll_", &OptionsDialog::mainNewsFilter_ },
};

const MainWindow::SettingBindings::ActionBoolSetting MainWindow::SettingBindings::kActionBool[] = {
  { "mainToolbarLock",      &MainWindow::toolBarLockAct_,          true },
  { "mainToolbarShow2",     &MainWindow::mainToolbarToggle_,       false },
  { "feedsToolbarShow2",    &MainWindow::feedsToolbarToggle_,      true },
  { "newsToolbarShow",      &MainWindow::newsToolbarToggle_,       true },
  { "categoriesPanelShow",  &MainWindow::categoriesPanelToggle_,   true },
  { "statusBarShow",        &MainWindow::statusBarToggle_,         true },
  { "showUnreadCount",      &MainWindow::showUnreadCount_,         true },
  { "showUndeleteCount",    &MainWindow::showUndeleteCount_,       false },
  { "showLastUpdated",      &MainWindow::showLastUpdated_,         false },
  { "indentationFeedsTree", &MainWindow::indentationFeedsTreeAct_, true },
  { "stayOnTop",            &MainWindow::stayOnTopAct_,            false },
};

const MainWindow::SettingBindings::AppBoolCheckSetting MainWindow::SettingBindings::kAppBoolCheck[] = {
  { &AppSettings::showCloseButtonTab, &OptionsDialog::showCloseButtonTab_ },
  { &AppSettings::updateCheckEnabled, &OptionsDialog::updateCheckEnabled_ },
  { &AppSettings::storeDBMemory,      &OptionsDialog::storeDBMemory_ },
};

const MainWindow::SettingBindings::AppIntSpinSetting MainWindow::SettingBindings::kAppIntSpin[] = {
  { &AppSettings::timeoutRequest, &OptionsDialog::timeoutRequest_ },
  { &AppSettings::numberRequest,  &OptionsDialog::numberRequests_ },
  { &AppSettings::numberRepeats,  &OptionsDialog::numberRepeats_ },
};

/** @brief Load settings from ini-file
 *---------------------------------------------------------------------------*/
void MainWindow::loadSettings()
{
  Settings settings("Settings");

  showSplashScreen_ = AppSettings::showSplashScreen.get();
  openingFeedAction_ = AppSettings::openingFeedAction.get();
  openNewsWebViewOn_ = AppSettings::openNewsWebViewOn.get();

  for (const auto &s : SettingBindings::kBoolCheck)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toBool();
  for (const auto &s : SettingBindings::kBoolRadio)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toBool();
  for (const auto &s : SettingBindings::kBoolGroup)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toBool();
  for (const auto &s : SettingBindings::kIntSpin)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toInt();
  for (const auto &s : SettingBindings::kIntCombo)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toInt();
  for (const auto &s : SettingBindings::kComboData)
    this->*(s.member) = settings.value(s.key, QString::fromLatin1(s.defaultValue)).toString();

#ifndef Q_OS_MAC
  for (const auto &s : SettingBindings::kTrayHold)
    this->*(s.member) = settings.value(s.key, s.defaultValue).toBool();
#else
  for (const auto &s : SettingBindings::kTrayHold)
    this->*(s.member) = false;
#endif

  for (int row = 0; row < kFontOptionCount; ++row) {
    const QFont def = defaultFontForOption(row);
    const QString family = settings.value(fontFamilySettingKey(row), def.family()).toString();
    const int size = settings.value(fontSizeSettingKey(row), def.pointSize()).toInt();
    setFontForOption(row, QFont(family, size));
  }

  feedsModel_->formatDate_ = formatDate_;
  feedsModel_->formatTime_ = formatTime_;
  avoidedOldNewsDate_ = settings.value("avoidedOldNewsDate").toDate();

  downloadLocation_ = settings.value("downloadLocation", "").toString();
  preloadNewspaperPages_ = qBound(0, settings.value("preloadNewspaperPages", 2).toInt(), 10);
  soundNotifyPath_ = settings.value("soundNotifyPath", mainApp->soundNotifyDefaultFile()).toString();

  for (const auto &s : SettingBindings::kActionBool)
    (this->*(s.action))->setChecked(settings.value(s.key, s.defaultValue).toBool());

  lockMainToolbar(toolBarLockAct_->isChecked());
  categoriesWidget_->setVisible(categoriesPanelToggle_->isChecked());

  populateToolbarActions(mainToolbar_, actions(),
                         settings.value("mainToolBar",
                                        "newAct,Separator,updateFeedAct,updateAllFeedsAct,"
                                        "Separator,markFeedRead,Separator,autoLoadImagesToggle").toString());
  populateToolbarActions(feedsToolBar_, actions(),
                         settings.value("feedsToolBar2",
                                        "newAct,Separator,updateAllFeedsAct,markFeedRead,"
                                        "Separator,feedsFilter,findFeedAct").toString());

  setToolBarStyle(AppSettings::toolBarStyle.get());
  setToolBarIconSize(mainToolbar_, AppSettings::toolBarIconSize.get());
  setToolBarIconSize(feedsToolBar_, AppSettings::feedsToolBarIconSize.get());

  feedsColumnVisible(showUnreadCount_);
  feedsColumnVisible(showUndeleteCount_);
  feedsColumnVisible(showLastUpdated_);

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

  pushButtonNull_->setVisible(showToggleFeedsTree_);
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
  for (int row = 0; row < kColorOptionCount; ++row) {
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
  AppSettings::openingFeedAction.set(openingFeedAction_);
  AppSettings::openNewsWebViewOn.set(openNewsWebViewOn_);

  for (const auto &s : SettingBindings::kBoolCheck)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kTrayHold)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kBoolRadio)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kBoolGroup)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kIntSpin)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kIntCombo)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kComboData)
    settings.setValue(s.key, this->*(s.member));
  for (const auto &s : SettingBindings::kActionBool)
    settings.setValue(s.key, (this->*(s.action))->isChecked());

  settings.setValue("createLastFeed", mainApp->isSaveDataLastFeed());
  settings.setValue("behaviorIconTray", behaviorIconTray_);
  settings.setValue("langFileName", mainApp->language());

  for (int row = 0; row < kFontOptionCount; ++row) {
    const QFont f = fontForOption(row);
    settings.setValue(fontFamilySettingKey(row), f.family());
    settings.setValue(fontSizeSettingKey(row), f.pointSize());
  }

  settings.setValue("avoidedOldNewsDate", avoidedOldNewsDate_);
  settings.setValue("downloadLocation", downloadLocation_);
  settings.setValue("preloadNewspaperPages", preloadNewspaperPages_);
  settings.setValue("soundNotifyPath", soundNotifyPath_);

#ifdef HAVE_X11
  if (allWorkspacesAct_)
    settings.setValue("allWorkspaces", allWorkspacesAct_->isChecked());
#endif

  settings.setValue("theme", mainApp->applicationStyle().id);
  settings.setValue("newsLayout", newsLayout_);
  settings.setValue("browserPosition", browserPosition_);
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

  QElapsedTimer totalTimer;
  totalTimer.start();
  QElapsedTimer stepTimer;
  stepTimer.start();

  optionsDialog_ = new OptionsDialog(this);
  const qint64 ctorMs = stepTimer.restart();

  bool updateFeedsStartUp = AppSettings::autoUpdatefeedsStartUp.get();
  int saveDBMemFileInterval = AppSettings::saveDBMemFileInterval.get();

  for (const auto &s : SettingBindings::kBoolCheck)
    (optionsDialog_->*(s.widget))->setChecked(this->*(s.member));
  for (const auto &s : SettingBindings::kTrayHold)
    (optionsDialog_->*(s.widget))->setChecked(this->*(s.member));
  for (const auto &s : SettingBindings::kBoolRadio)
    (optionsDialog_->*(s.widget))->setChecked(this->*(s.member));
  for (const auto &s : SettingBindings::kBoolGroup)
    (optionsDialog_->*(s.widget))->setChecked(this->*(s.member));
  for (const auto &s : SettingBindings::kIntSpin)
    (optionsDialog_->*(s.widget))->setValue(this->*(s.member));
  for (const auto &s : SettingBindings::kIntCombo)
    (optionsDialog_->*(s.widget))->setCurrentIndex(this->*(s.member) + s.indexOffset);
  for (const auto &s : SettingBindings::kComboData)
    selectComboData(optionsDialog_->*(s.widget), this->*(s.member));
  for (const auto &s : SettingBindings::kAppBoolCheck)
    (optionsDialog_->*(s.widget))->setChecked(s.setting->get());
  for (const auto &s : SettingBindings::kAppIntSpin)
    (optionsDialog_->*(s.widget))->setValue(s.setting->get());

  optionsDialog_->showSplashScreen_->setChecked(showSplashScreen_);
  optionsDialog_->autocollapseFolder_->setChecked(feedsView_->autocollapseFolder_);
  optionsDialog_->saveDBMemFileInterval_->setValue(saveDBMemFileInterval);
  optionsDialog_->setBehaviorIconTray(behaviorIconTray_);
  optionsDialog_->setProxy(mainApp->networkProxy());

  optionsDialog_->preloadNewspaperPages_->setValue(preloadNewspaperPages_);
  optionsDialog_->preloadNewspaperPages_->setEnabled(autoLoadImages_);
  const int browserMode = AppSettings::externalBrowserOn.get();
  optionsDialog_->customExternalBrowser_->setChecked(browserMode == 2 || browserMode == -1);
  optionsDialog_->externalBrowser_->setText(AppSettings::externalBrowser.get());
  optionsDialog_->returnFocusAfterLink_->setChecked(AppSettings::returnFocusAfterLink.get());

  optionsDialog_->downloadLocationEdit_->setText(downloadLocation_);
  optionsDialog_->updateFeedsStartUp_->setChecked(updateFeedsStartUp);
  optionsDialog_->setOpeningFeed(openingFeedAction_);
  optionsDialog_->openNewsWebViewOn_->setChecked(openNewsWebViewOn_);

  if (!avoidedOldNewsDate_.isNull() && avoidedOldNewsDate_.isValid()) {
    optionsDialog_->avoidedOldNewsDate_->setSelectedDate(avoidedOldNewsDate_);
  } else {
    optionsDialog_->avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
  }

  QString styleSheetNews = settings.value("Settings/styleSheetNews",
                                          mainApp->styleSheetNewsDefaultFile()).toString();
  optionsDialog_->styleSheetNewsEdit_->setText(styleSheetNews);
  optionsDialog_->editSoundNotifer_->setText(soundNotifyPath_);
  optionsDialog_->setLanguage(mainApp->language());
  const qint64 bindMs = stepTimer.restart();

  for (int row = 0; row < kFontOptionCount; ++row) {
    const QFont f = fontForOption(row);
    optionsDialog_->fontsTree_->topLevelItem(row)->setText(
          2, QString("%1, %2").arg(f.family()).arg(f.pointSize()));
  }

  for (int row = 0; row < kColorOptionCount; ++row) {
    const QString colorName = colorValueForOption(row);
    QPixmap pixmapColor(14, 14);
    if (colorName.isEmpty())
      pixmapColor.fill(QColor(0, 0, 0, 0));
    else
      pixmapColor.fill(colorName);
    optionsDialog_->colorsTree_->topLevelItem(row)->setIcon(0, pixmapColor);
    optionsDialog_->colorsTree_->topLevelItem(row)->setText(1, colorName);
  }
  const qint64 themeMs = stepTimer.restart();

  optionsDialog_->loadActionShortcut(shortcutRegistry_.actions(), &shortcutRegistry_.defaults());
  const qint64 shortcutsMs = stepTimer.restart();

  // Display setting dialog

  optionsDialog_->setCurrentItem(pageIndex);
  const qint64 currentItemMs = stepTimer.restart();
  qInfo().noquote() << QStringLiteral(
      "[perf] MainWindow::showOptionDlg pre-exec: total=%1 ms (ctor=%2, bind=%3, theme=%4, shortcuts=%5, setCurrentItem=%6, page=%7)")
      .arg(totalTimer.elapsed())
      .arg(ctorMs)
      .arg(bindMs)
      .arg(themeMs)
      .arg(shortcutsMs)
      .arg(currentItemMs)
      .arg(pageIndex);
  int result = optionsDialog_->exec();
  pageIndex = optionsDialog_->currentIndex();

  if (result == QDialog::Rejected) {
    delete optionsDialog_;
    optionsDialog_ = nullptr;
    return;
  }

  // Apply accepted settings
  QElapsedTimer applyTimer;
  applyTimer.start();

  if (optionsDialog_->shortcutsLoaded()) {
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

  const bool wasTrayIconEnabled = showTrayIcon_;

  for (const auto &s : SettingBindings::kBoolCheck)
    this->*(s.member) = (optionsDialog_->*(s.widget))->isChecked();
  for (const auto &s : SettingBindings::kTrayHold)
    this->*(s.member) = (optionsDialog_->*(s.widget))->isChecked();
  for (const auto &s : SettingBindings::kBoolRadio)
    this->*(s.member) = (optionsDialog_->*(s.widget))->isChecked();
  for (const auto &s : SettingBindings::kBoolGroup)
    this->*(s.member) = (optionsDialog_->*(s.widget))->isChecked();
  for (const auto &s : SettingBindings::kIntSpin)
    this->*(s.member) = (optionsDialog_->*(s.widget))->value();
  for (const auto &s : SettingBindings::kIntCombo)
    this->*(s.member) = (optionsDialog_->*(s.widget))->currentIndex() - s.indexOffset;
  for (const auto &s : SettingBindings::kComboData) {
    QComboBox *combo = optionsDialog_->*(s.widget);
    this->*(s.member) = combo->itemData(combo->currentIndex()).toString();
  }
  for (const auto &s : SettingBindings::kAppBoolCheck)
    s.setting->set((optionsDialog_->*(s.widget))->isChecked());
  for (const auto &s : SettingBindings::kAppIntSpin)
    s.setting->set((optionsDialog_->*(s.widget))->value());

  showSplashScreen_ = optionsDialog_->showSplashScreen_->isChecked();
  feedsModel_->defaultIconFeeds_ = defaultIconFeeds_;
  feedsView_->autocollapseFolder_ = optionsDialog_->autocollapseFolder_->isChecked();
  pushButtonNull_->setVisible(showToggleFeedsTree_);

  if (saveDBMemFileInterval != optionsDialog_->saveDBMemFileInterval_->value()) {
    saveDBMemFileInterval = optionsDialog_->saveDBMemFileInterval_->value();
    AppSettings::saveDBMemFileInterval.set(saveDBMemFileInterval);
    mainApp->updateFeeds()->startSaveTimer();
  }

  behaviorIconTray_ = optionsDialog_->behaviorIconTray();
  if (behaviorIconTray_ > CHANGE_ICON_TRAY) {
    emit signalRefreshInfoTray();
  } else {
    trayIconController_->showDefaultIcon();
  }
  if (showTrayIcon_) {
    trayIconController_->show();
  } else {
    trayIconController_->hide();
    if (wasTrayIconEnabled && isHidden())
      showWindows();
  }

  mainApp->proxySaveSettings(optionsDialog_->proxy());

  preloadNewspaperPages_ = optionsDialog_->preloadNewspaperPages_->value();
  AppSettings::externalBrowserOn.set(optionsDialog_->customExternalBrowser_->isChecked() ? 2 : 1);
  AppSettings::externalBrowser.set(optionsDialog_->externalBrowser_->text());
  AppSettings::returnFocusAfterLink.set(optionsDialog_->returnFocusAfterLink_->isChecked());
  if (!optionsDialog_->returnFocusAfterLink_->isChecked()) returnFocusAfterLinkTimer_.stop();

  downloadLocation_ = optionsDialog_->downloadLocationEdit_->text();
  updateFeedsStartUp = optionsDialog_->updateFeedsStartUp_->isChecked();
  openingFeedAction_ = optionsDialog_->getOpeningFeed();
  openNewsWebViewOn_ = optionsDialog_->openNewsWebViewOn_->isChecked();

  feedsModel_->formatDate_ = formatDate_;
  feedsModel_->formatTime_ = formatTime_;

  if (!optionsDialog_->avoidedOldNewsDate_->selectedDate().isNull() && optionsDialog_->avoidedOldNewsDate_->selectedDate().isValid()) {
    avoidedOldNewsDate_ = optionsDialog_->avoidedOldNewsDate_->selectedDate();
  } else {
    avoidedOldNewsDate_ = QDate::currentDate();
  }

  styleSheetNews = optionsDialog_->styleSheetNewsEdit_->text();
  if (styleSheetNews.isEmpty()) styleSheetNews = mainApp->styleSheetNewsDefaultFile();
  settings.setValue("Settings/styleSheetNews", styleSheetNews);

  soundNotifyPath_ = optionsDialog_->editSoundNotifer_->text();

  if (mainApp->language() != optionsDialog_->language()) {
    mainApp->setLanguage(optionsDialog_->language());
    mainApp->setTranslateApplication();
  }

  for (int row = 0; row < kFontOptionCount; ++row) {
    const QString text = optionsDialog_->fontsTree_->topLevelItem(row)->text(2);
    QFont font = fontForOption(row);
    font.setFamily(text.section(", ", 0, 0));
    font.setPointSize(text.section(", ", 1).toInt());
    setFontForOption(row, font);
  }

  for (int row = 0; row < optionsDialog_->colorsTree_->topLevelItemCount(); ++row) {
    setColorValueForOption(row, optionsDialog_->colorsTree_->topLevelItem(row)->text(1));
    if (colorValueForOption(row) == defaultColorForOption(row))
      customColorRows_.remove(row);
    else
      customColorRows_.insert(row);
  }
  feedsView_->setStyleSheet(feedsModel_->backgroundColor_.isEmpty()
      ? QString() : QString("#feedsView_ {background: %1;}").arg(feedsModel_->backgroundColor_));

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
  qInfo().noquote() << QStringLiteral("[perf] MainWindow::showOptionDlg apply: %1 ms")
      .arg(applyTimer.elapsed());
}

void MainWindow::showSettingPageLabels()
{
  showOptionDlg(5);
}

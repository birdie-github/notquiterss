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
#include "mainwindow.h"
#include "preferences/applicationstyle.h"
#include "preferences/pages/browsersettingspage.h"
#include "preferences/pages/feedssettingspage.h"
#include "preferences/pages/fontscolorssettingspage.h"
#include "preferences/pages/generalsettingspage.h"
#include "preferences/pages/labelssettingspage.h"
#include "preferences/pages/languagesettingspage.h"
#include "preferences/pages/networksettingspage.h"
#include "preferences/pages/notificationssettingspage.h"
#include "preferences/pages/shortcutssettingspage.h"
#include "preferences/pages/systemtraysettingspage.h"
#include "common/common.h"
#include "app/mainapplication.h"
#include "preferences/settings.h"
#include "trayiconcontroller.h"
#include "feeds/update/updatefeeds.h"
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
}

void MainWindow::populateToolbarActions(QToolBar *toolbar, const QList<QAction *> &actions, const QString &spec)
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

struct MainWindow::SettingBindings {
  struct BoolCheckSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QCheckBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct BoolRadioSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QRadioButton *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct BoolGroupSetting {
    const char *key;
    bool MainWindow::*member;
    bool defaultValue;
    QGroupBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct IntSpinSetting {
    const char *key;
    int MainWindow::*member;
    int defaultValue;
    QSpinBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct IntComboSetting {
    const char *key;
    int MainWindow::*member;
    int defaultValue;
    QComboBox *(*widget)(OptionsDialog *);
    int indexOffset;
    OptionsDialog::Page page;
  };

  struct ComboDataSetting {
    const char *key;
    QString MainWindow::*member;
    const char *defaultValue;
    QComboBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct ActionBoolSetting {
    const char *key;
    QAction *MainWindow::*action;
    bool defaultValue;
  };

  struct AppBoolCheckSetting {
    const AppSettings::Setting<bool> *setting;
    QCheckBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
  };

  struct AppIntSpinSetting {
    const AppSettings::Setting<int> *setting;
    QSpinBox *(*widget)(OptionsDialog *);
    OptionsDialog::Page page;
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

#define OPT_PAGE_WIDGET(pageGetter, field) \
  [](OptionsDialog *d) { return d->pageGetter() ? d->pageGetter()->field : nullptr; }

const MainWindow::SettingBindings::BoolCheckSetting MainWindow::SettingBindings::kBoolCheck[] = {
  { "reopenFeedStartup",        &MainWindow::reopenFeedStartup_,        true,  OPT_PAGE_WIDGET(generalPage, reopenFeedStartup_),        OptionsDialog::PageGeneral },
  { "openNewTabNextToActive",   &MainWindow::openNewTabNextToActive_,   false, OPT_PAGE_WIDGET(generalPage, openNewTabNextToActive_),   OptionsDialog::PageGeneral },
  { "hideFeedsOpenTab",         &MainWindow::hideFeedsOpenTab_,         false, OPT_PAGE_WIDGET(generalPage, hideFeedsOpenTab_),         OptionsDialog::PageGeneral },
  { "showToggleFeedsTree",      &MainWindow::showToggleFeedsTree_,      true,  OPT_PAGE_WIDGET(generalPage, showToggleFeedsTree_),      OptionsDialog::PageGeneral },
  { "defaultIconFeeds",         &MainWindow::defaultIconFeeds_,         false, OPT_PAGE_WIDGET(generalPage, defaultIconFeeds_),         OptionsDialog::PageGeneral },
  { "singleClickTray",          &MainWindow::singleClickTray_,          false, OPT_PAGE_WIDGET(traySystemPage, singleClickTray_),       OptionsDialog::PageSystemTray },
  { "clearStatusNew",           &MainWindow::clearStatusNew_,           true,  OPT_PAGE_WIDGET(traySystemPage, clearStatusNew_),        OptionsDialog::PageSystemTray },
  { "emptyWorking",             &MainWindow::emptyWorking_,             true,  OPT_PAGE_WIDGET(traySystemPage, emptyWorking_),          OptionsDialog::PageSystemTray },
  { "autoLoadImages",           &MainWindow::autoLoadImages_,           true,  OPT_PAGE_WIDGET(browserPage, autoLoadImages_),           OptionsDialog::PageBrowser },
  { "askDownloadLocation",      &MainWindow::askDownloadLocation_,      true,  OPT_PAGE_WIDGET(browserPage, askDownloadLocation_),      OptionsDialog::PageBrowser },
  { "autoUpdatefeeds",          &MainWindow::updateFeedsEnable_,        false, OPT_PAGE_WIDGET(feedsPage, updateFeedsEnable_),          OptionsDialog::PageFeeds },
  { "markReadSwitchingFeed",    &MainWindow::markReadSwitchingFeed_,    false, OPT_PAGE_WIDGET(feedsPage, markReadSwitchingFeed_),      OptionsDialog::PageFeeds },
  { "markReadClosingTab",       &MainWindow::markReadClosingTab_,       false, OPT_PAGE_WIDGET(feedsPage, markReadClosingTab_),         OptionsDialog::PageFeeds },
  { "markReadMinimize",         &MainWindow::markReadMinimize_,         false, OPT_PAGE_WIDGET(feedsPage, markReadMinimize_),           OptionsDialog::PageFeeds },
  { "alternatingColorsNews",    &MainWindow::alternatingRowColorsNews_, false, OPT_PAGE_WIDGET(feedsPage, alternatingRowColorsNews_),   OptionsDialog::PageFeeds },
  { "changeBehaviorActionNUN",  &MainWindow::changeBehaviorActionNUN_,  false, OPT_PAGE_WIDGET(feedsPage, changeBehaviorActionNUN_),    OptionsDialog::PageFeeds },
  { "simplifiedDateTime",       &MainWindow::simplifiedDateTime_,       true,  OPT_PAGE_WIDGET(feedsPage, simplifiedDateTime_),         OptionsDialog::PageFeeds },
  { "notDeleteStarred",         &MainWindow::notDeleteStarred_,         false, OPT_PAGE_WIDGET(feedsPage, notDeleteStarred_),           OptionsDialog::PageFeeds },
  { "notDeleteLabeled",         &MainWindow::notDeleteLabeled_,         false, OPT_PAGE_WIDGET(feedsPage, notDeleteLabeled_),           OptionsDialog::PageFeeds },
  { "markIdenticalNewsRead",    &MainWindow::markIdenticalNewsRead_,    true,  OPT_PAGE_WIDGET(feedsPage, markIdenticalNewsRead_),      OptionsDialog::PageFeeds },
  { "dayClearUpOn",             &MainWindow::dayCleanUpOn_,             true,  OPT_PAGE_WIDGET(feedsPage, dayCleanUpOn_),               OptionsDialog::PageFeeds },
  { "newsClearUpOn",            &MainWindow::newsCleanUpOn_,            true,  OPT_PAGE_WIDGET(feedsPage, newsCleanUpOn_),              OptionsDialog::PageFeeds },
  { "readClearUp",              &MainWindow::readCleanUp_,              false, OPT_PAGE_WIDGET(feedsPage, readCleanUp_),                OptionsDialog::PageFeeds },
  { "neverUnreadClearUp",       &MainWindow::neverUnreadCleanUp_,       true,  OPT_PAGE_WIDGET(feedsPage, neverUnreadCleanUp_),         OptionsDialog::PageFeeds },
  { "neverStarClearUp",         &MainWindow::neverStarCleanUp_,         true,  OPT_PAGE_WIDGET(feedsPage, neverStarCleanUp_),           OptionsDialog::PageFeeds },
  { "neverLabelClearUp",        &MainWindow::neverLabelCleanUp_,        true,  OPT_PAGE_WIDGET(feedsPage, neverLabelCleanUp_),          OptionsDialog::PageFeeds },
  { "cleanUpDeleted",           &MainWindow::cleanUpDeleted_,           false, OPT_PAGE_WIDGET(feedsPage, cleanUpDeleted_),             OptionsDialog::PageFeeds },
  { "optimizeDB",               &MainWindow::optimizeDB_,               false, OPT_PAGE_WIDGET(feedsPage, optimizeDB_),                 OptionsDialog::PageFeeds },
  { "fullscreenModeNotify",     &MainWindow::fullscreenModeNotify_,     true,  OPT_PAGE_WIDGET(notifierPage, fullscreenModeNotify_),    OptionsDialog::PageNotifications },
  { "showNotifyInactiveApp",    &MainWindow::showNotifyInactiveApp_,    true,  OPT_PAGE_WIDGET(notifierPage, showNotifyInactiveApp_),   OptionsDialog::PageNotifications },
  { "onlySelectedFeeds",        &MainWindow::onlySelectedFeeds_,        false, OPT_PAGE_WIDGET(notifierPage, onlySelectedFeeds_),       OptionsDialog::PageNotifications },
  { "showTitlesFeedsNotify",    &MainWindow::showTitlesFeedsNotify_,    true,  OPT_PAGE_WIDGET(notifierPage, showTitlesFeedsNotify_),   OptionsDialog::PageNotifications },
  { "showIconFeedNotify",       &MainWindow::showIconFeedNotify_,       false, OPT_PAGE_WIDGET(notifierPage, showIconFeedNotify_),      OptionsDialog::PageNotifications },
  { "showButtonMarkAllNotify",  &MainWindow::showButtonMarkAllNotify_,  true,  OPT_PAGE_WIDGET(notifierPage, showButtonMarkAllNotify_), OptionsDialog::PageNotifications },
  { "showButtonMarkReadNotify", &MainWindow::showButtonMarkReadNotify_, true,  OPT_PAGE_WIDGET(notifierPage, showButtonMarkReadNotify_),OptionsDialog::PageNotifications },
  { "showButtonExBrowserNotify",&MainWindow::showButtonExBrowserNotify_,true,  OPT_PAGE_WIDGET(notifierPage, showButtonExBrowserNotify_),OptionsDialog::PageNotifications },
  { "showButtonDeleteNotify",   &MainWindow::showButtonDeleteNotify_,   true,  OPT_PAGE_WIDGET(notifierPage, showButtonDeleteNotify_),  OptionsDialog::PageNotifications },
  { "closeNotify",              &MainWindow::closeNotify_,              true,  OPT_PAGE_WIDGET(notifierPage, closeNotify_),             OptionsDialog::PageNotifications },
};

const MainWindow::SettingBindings::BoolCheckSetting MainWindow::SettingBindings::kTrayHold[] = {
  { "startingTray",   &MainWindow::startingTray_,   false, OPT_PAGE_WIDGET(traySystemPage, startingTray_),   OptionsDialog::PageSystemTray },
  { "minimizingTray", &MainWindow::minimizingTray_, true,  OPT_PAGE_WIDGET(traySystemPage, minimizingTray_), OptionsDialog::PageSystemTray },
  { "closingTray",    &MainWindow::closingTray_,    false, OPT_PAGE_WIDGET(traySystemPage, closingTray_),    OptionsDialog::PageSystemTray },
};

const MainWindow::SettingBindings::BoolRadioSetting MainWindow::SettingBindings::kBoolRadio[] = {
  { "markCurNewsRead",  &MainWindow::markCurNewsRead_,  true,  OPT_PAGE_WIDGET(feedsPage, markCurNewsRead_),  OptionsDialog::PageFeeds },
  { "markPrevNewsRead", &MainWindow::markPrevNewsRead_, false, OPT_PAGE_WIDGET(feedsPage, markPrevNewsRead_), OptionsDialog::PageFeeds },
};

const MainWindow::SettingBindings::BoolGroupSetting MainWindow::SettingBindings::kBoolGroup[] = {
  { "showTrayIcon",      &MainWindow::showTrayIcon_,      true,  OPT_PAGE_WIDGET(traySystemPage, showTrayIconBox_), OptionsDialog::PageSystemTray },
  { "markNewsReadOn",    &MainWindow::markNewsReadOn_,    true,  OPT_PAGE_WIDGET(feedsPage, markNewsReadOn_),         OptionsDialog::PageFeeds },
  { "avoidOldNews",      &MainWindow::avoidOldNews_,      false, OPT_PAGE_WIDGET(feedsPage, avoidedOldNewsDateOn_),   OptionsDialog::PageFeeds },
  { "cleanupOnShutdown", &MainWindow::cleanupOnShutdown_, true,  OPT_PAGE_WIDGET(feedsPage, cleanupOnShutdownBox_),   OptionsDialog::PageFeeds },
  { "soundNewNews",      &MainWindow::soundNewNews_,      true,  OPT_PAGE_WIDGET(notifierPage, soundNotifyBox_),      OptionsDialog::PageNotifications },
  { "showNotifyOn",      &MainWindow::showNotifyOn_,      true,  OPT_PAGE_WIDGET(notifierPage, showNotifyOn_),        OptionsDialog::PageNotifications },
};

const MainWindow::SettingBindings::IntSpinSetting MainWindow::SettingBindings::kIntSpin[] = {
  { "defaultZoomPages",     &MainWindow::defaultZoomPages_,     100, OPT_PAGE_WIDGET(browserPage, defaultZoomPages_),     OptionsDialog::PageBrowser },
  { "autoUpdatefeedsTime",  &MainWindow::updateFeedsInterval_,  10,  OPT_PAGE_WIDGET(feedsPage, updateFeedsInterval_),    OptionsDialog::PageFeeds },
  { "markNewsReadTime",     &MainWindow::markNewsReadTime_,     0,   OPT_PAGE_WIDGET(feedsPage, markNewsReadTime_),       OptionsDialog::PageFeeds },
  { "maxDayClearUp",        &MainWindow::maxDayCleanUp_,        30,  OPT_PAGE_WIDGET(feedsPage, maxDayCleanUp_),          OptionsDialog::PageFeeds },
  { "maxNewsClearUp",       &MainWindow::maxNewsCleanUp_,       200, OPT_PAGE_WIDGET(feedsPage, maxNewsCleanUp_),         OptionsDialog::PageFeeds },
  { "transparencyNotify",   &MainWindow::transparencyNotify_,   60,  OPT_PAGE_WIDGET(notifierPage, transparencyNotify_),  OptionsDialog::PageNotifications },
  { "countShowNewsNotify",  &MainWindow::countShowNewsNotify_,  10,  OPT_PAGE_WIDGET(notifierPage, countShowNewsNotify_), OptionsDialog::PageNotifications },
  { "widthTitleNewsNotify", &MainWindow::widthTitleNewsNotify_, 300, OPT_PAGE_WIDGET(notifierPage, widthTitleNewsNotify_),OptionsDialog::PageNotifications },
  { "timeShowNewsNotify",   &MainWindow::timeShowNewsNotify_,   10,  OPT_PAGE_WIDGET(notifierPage, timeShowNewsNotify_),  OptionsDialog::PageNotifications },
};

const MainWindow::SettingBindings::IntComboSetting MainWindow::SettingBindings::kIntCombo[] = {
  { "autoUpdatefeedsInterval", &MainWindow::updateFeedsIntervalType_, 0, OPT_PAGE_WIDGET(feedsPage, updateIntervalType_), 1, OptionsDialog::PageFeeds },
  { "screenNotify",            &MainWindow::screenNotify_,            0, OPT_PAGE_WIDGET(notifierPage, screenNotify_),    1, OptionsDialog::PageNotifications },
  { "positionNotify",          &MainWindow::positionNotify_,          3, OPT_PAGE_WIDGET(notifierPage, positionNotify_),  0, OptionsDialog::PageNotifications },
};

const MainWindow::SettingBindings::ComboDataSetting MainWindow::SettingBindings::kComboData[] = {
  { "formatData",     &MainWindow::formatDate_,     "dd.MM.yy",       OPT_PAGE_WIDGET(feedsPage, formatDate_),     OptionsDialog::PageFeeds },
  { "formatTime",     &MainWindow::formatTime_,     "hh:mm",          OPT_PAGE_WIDGET(feedsPage, formatTime_),     OptionsDialog::PageFeeds },
  { "mainNewsFilter", &MainWindow::mainNewsFilter_, "filterNewsAll_", OPT_PAGE_WIDGET(feedsPage, mainNewsFilter_), OptionsDialog::PageFeeds },
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
#ifdef NQRSS_ENABLE_DONATE_BUTTON
  { &AppSettings::hideDonateButton, OPT_PAGE_WIDGET(generalPage, hideDonateButton_), OptionsDialog::PageGeneral },
#endif
  { &AppSettings::showCloseButtonTab,      OPT_PAGE_WIDGET(generalPage, showCloseButtonTab_),      OptionsDialog::PageGeneral },
  { &AppSettings::updateCheckEnabled,      OPT_PAGE_WIDGET(generalPage, updateCheckEnabled_),      OptionsDialog::PageGeneral },
  { &AppSettings::storeDBMemory,           OPT_PAGE_WIDGET(generalPage, storeDBMemory_),           OptionsDialog::PageGeneral },
  { &AppSettings::highQualityImageScaling, OPT_PAGE_WIDGET(browserPage, highQualityImageScaling_), OptionsDialog::PageBrowser },
};

const MainWindow::SettingBindings::AppIntSpinSetting MainWindow::SettingBindings::kAppIntSpin[] = {
  { &AppSettings::timeoutRequest, OPT_PAGE_WIDGET(networkPage, timeoutRequest_), OptionsDialog::PageNetwork },
  { &AppSettings::numberRequest,  OPT_PAGE_WIDGET(networkPage, numberRequests_), OptionsDialog::PageNetwork },
  { &AppSettings::numberRepeats,  OPT_PAGE_WIDGET(networkPage, numberRepeats_),  OptionsDialog::PageNetwork },
};

#undef OPT_PAGE_WIDGET

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

void MainWindow::initOptionPage(int page)
{
  if (!optionsDialog_) return;

  for (const auto &s : SettingBindings::kBoolCheck) {
    if (s.page == page) s.widget(optionsDialog_)->setChecked(this->*(s.member));
  }
  for (const auto &s : SettingBindings::kTrayHold) {
    if (s.page == page) s.widget(optionsDialog_)->setChecked(this->*(s.member));
  }
  for (const auto &s : SettingBindings::kBoolRadio) {
    if (s.page == page) s.widget(optionsDialog_)->setChecked(this->*(s.member));
  }
  for (const auto &s : SettingBindings::kBoolGroup) {
    if (s.page == page) s.widget(optionsDialog_)->setChecked(this->*(s.member));
  }
  for (const auto &s : SettingBindings::kIntSpin) {
    if (s.page == page) s.widget(optionsDialog_)->setValue(this->*(s.member));
  }
  for (const auto &s : SettingBindings::kIntCombo) {
    if (s.page == page) s.widget(optionsDialog_)->setCurrentIndex(this->*(s.member) + s.indexOffset);
  }
  for (const auto &s : SettingBindings::kComboData) {
    if (s.page == page) selectComboData(s.widget(optionsDialog_), this->*(s.member));
  }
  for (const auto &s : SettingBindings::kAppBoolCheck) {
    if (s.page == page) s.widget(optionsDialog_)->setChecked(s.setting->get());
  }
  for (const auto &s : SettingBindings::kAppIntSpin) {
    if (s.page == page) s.widget(optionsDialog_)->setValue(s.setting->get());
  }

  switch (page) {
  case OptionsDialog::PageGeneral: {
    auto *p = optionsDialog_->generalPage();
    p->showSplashScreen_->setChecked(showSplashScreen_);
    p->autocollapseFolder_->setChecked(feedsView_->autocollapseFolder_);
    p->saveDBMemFileInterval_->setValue(AppSettings::saveDBMemFileInterval.get());
    break;
  }
  case OptionsDialog::PageSystemTray:
    optionsDialog_->traySystemPage()->setBehaviorIconTray(behaviorIconTray_);
    break;
  case OptionsDialog::PageNetwork:
    optionsDialog_->networkPage()->setProxy(mainApp->networkProxy());
    break;
  case OptionsDialog::PageBrowser: {
    auto *p = optionsDialog_->browserPage();
    p->highQualityImageScaling_->setEnabled(autoLoadImages_);
    p->preloadNewspaperPages_->setValue(preloadNewspaperPages_);
    p->preloadNewspaperPages_->setEnabled(autoLoadImages_);
    const int browserMode = AppSettings::externalBrowserOn.get();
    p->customExternalBrowser_->setChecked(browserMode == 2 || browserMode == -1);
    p->externalBrowser_->setText(AppSettings::externalBrowser.get());
    p->returnFocusAfterLink_->setChecked(AppSettings::returnFocusAfterLink.get());
    p->downloadLocationEdit_->setText(downloadLocation_);
    break;
  }
  case OptionsDialog::PageFeeds: {
    Settings settings;
    auto *p = optionsDialog_->feedsPage();
    p->updateFeedsStartUp_->setChecked(AppSettings::autoUpdatefeedsStartUp.get());
    p->setOpeningFeed(openingFeedAction_);
    p->openNewsWebViewOn_->setChecked(openNewsWebViewOn_);
    if (!avoidedOldNewsDate_.isNull() && avoidedOldNewsDate_.isValid()) {
      p->avoidedOldNewsDate_->setSelectedDate(avoidedOldNewsDate_);
    } else {
      p->avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
    }
    const QString styleSheetNews = settings.value("Settings/styleSheetNews",
                                                  mainApp->styleSheetNewsDefaultFile()).toString();
    p->styleSheetNewsEdit_->setText(styleSheetNews);
    break;
  }
  case OptionsDialog::PageNotifications:
    optionsDialog_->notifierPage()->editSoundNotifier_->setText(soundNotifyPath_);
    break;
  case OptionsDialog::PageLanguage:
    optionsDialog_->languagePage()->setLanguage(mainApp->language());
    break;
  case OptionsDialog::PageFontsColors: {
    auto *p = optionsDialog_->fontsColorsPage();
    for (int row = 0; row < kFontOptionCount; ++row)
      p->setFontOptionValue(row, fontForOption(row));
    for (int row = 0; row < kColorOptionCount; ++row)
      p->setColorOptionValue(row, colorValueForOption(row));
    break;
  }
  default:
    break;
  }
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

  optionsDialog_->loadActionShortcut(shortcutRegistry_.actions(), &shortcutRegistry_.defaults());
  const qint64 shortcutsMs = stepTimer.restart();

  // Display setting dialog

  optionsDialog_->setCurrentItem(pageIndex);
  const qint64 currentItemMs = stepTimer.restart();
  qInfo().noquote() << QStringLiteral(
      "[perf] MainWindow::showOptionDlg pre-exec: total=%1 ms (ctor=%2, shortcuts=%3, setCurrentItem=%4, page=%5)")
      .arg(totalTimer.elapsed())
      .arg(ctorMs)
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

  if (optionsDialog_->shortcutsPage() && optionsDialog_->shortcutsPage()->isLoaded()) {
    // Removing actions changes the registry; iterate over its original contents.
    const auto shortcutActions = shortcutRegistry_.actions();
    for (QAction *action : shortcutActions) {
      QString objectName = action->objectName();
      if (objectName.contains("labelAction_")) {
        shortcutRegistry_.removeOne(action);
        delete action;
      }
    }
    optionsDialog_->shortcutsPage()->save(shortcutRegistry_.actions(), newsLabelGroup_, this);
    shortcutRegistry_.append(newsLabelGroup_->actions());
    newsLabelMenu_->addActions(newsLabelGroup_->actions());
    this->addActions(newsLabelGroup_->actions());
    if (newsLabelGroup_->actions().count()) {
      newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
      newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
      newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
    }
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageLabels) &&
      optionsDialog_->labelsPage() && !optionsDialog_->labelsPage()->modifiedIds().isEmpty()) {
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

  for (const auto &s : SettingBindings::kBoolCheck) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      this->*(s.member) = s.widget(optionsDialog_)->isChecked();
  }
  for (const auto &s : SettingBindings::kTrayHold) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      this->*(s.member) = s.widget(optionsDialog_)->isChecked();
  }
  for (const auto &s : SettingBindings::kBoolRadio) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      this->*(s.member) = s.widget(optionsDialog_)->isChecked();
  }
  for (const auto &s : SettingBindings::kBoolGroup) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      this->*(s.member) = s.widget(optionsDialog_)->isChecked();
  }
  for (const auto &s : SettingBindings::kIntSpin) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      this->*(s.member) = s.widget(optionsDialog_)->value();
  }
  for (const auto &s : SettingBindings::kIntCombo) {
    if (optionsDialog_->isPageSaveEligible(s.page)) {
      const int idx = s.widget(optionsDialog_)->currentIndex();
      // Preserve stored values (such as a disconnected notification monitor) when no entry is selected.
      if (idx >= 0)
        this->*(s.member) = idx - s.indexOffset;
    }
  }
  for (const auto &s : SettingBindings::kComboData) {
    if (optionsDialog_->isPageSaveEligible(s.page)) {
      QComboBox *combo = s.widget(optionsDialog_);
      if (combo->currentIndex() >= 0)
        this->*(s.member) = combo->itemData(combo->currentIndex()).toString();
    }
  }
  for (const auto &s : SettingBindings::kAppBoolCheck) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      s.setting->set(s.widget(optionsDialog_)->isChecked());
  }
  for (const auto &s : SettingBindings::kAppIntSpin) {
    if (optionsDialog_->isPageSaveEligible(s.page))
      s.setting->set(s.widget(optionsDialog_)->value());
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageGeneral)) {
    auto *p = optionsDialog_->generalPage();
    showSplashScreen_ = p->showSplashScreen_->isChecked();
    feedsModel_->defaultIconFeeds_ = defaultIconFeeds_;
    feedsView_->autocollapseFolder_ = p->autocollapseFolder_->isChecked();
    pushButtonNull_->setVisible(showToggleFeedsTree_);
#ifdef NQRSS_ENABLE_DONATE_BUTTON
    setDonateButtonHidden(AppSettings::hideDonateButton.get());
#endif

    if (saveDBMemFileInterval != p->saveDBMemFileInterval_->value()) {
      saveDBMemFileInterval = p->saveDBMemFileInterval_->value();
      AppSettings::saveDBMemFileInterval.set(saveDBMemFileInterval);
      mainApp->updateFeeds()->startSaveTimer();
    }
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageSystemTray)) {
    behaviorIconTray_ = optionsDialog_->traySystemPage()->behaviorIconTray();
  }
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

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageNetwork)) {
    mainApp->proxySaveSettings(optionsDialog_->networkPage()->proxy());
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageBrowser)) {
    auto *p = optionsDialog_->browserPage();
    preloadNewspaperPages_ = p->preloadNewspaperPages_->value();
    AppSettings::externalBrowserOn.set(p->customExternalBrowser_->isChecked() ? 2 : 1);
    AppSettings::externalBrowser.set(p->externalBrowser_->text());
    AppSettings::returnFocusAfterLink.set(p->returnFocusAfterLink_->isChecked());
    if (!p->returnFocusAfterLink_->isChecked()) returnFocusAfterLinkTimer_.stop();

    downloadLocation_ = p->downloadLocationEdit_->text();
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageFeeds)) {
    auto *p = optionsDialog_->feedsPage();
    updateFeedsStartUp = p->updateFeedsStartUp_->isChecked();
    openingFeedAction_ = p->getOpeningFeed();
    openNewsWebViewOn_ = p->openNewsWebViewOn_->isChecked();

    feedsModel_->formatDate_ = formatDate_;
    feedsModel_->formatTime_ = formatTime_;

    if (!p->avoidedOldNewsDate_->selectedDate().isNull() && p->avoidedOldNewsDate_->selectedDate().isValid()) {
      avoidedOldNewsDate_ = p->avoidedOldNewsDate_->selectedDate();
    } else {
      avoidedOldNewsDate_ = QDate::currentDate();
    }

    QString styleSheetNews = p->styleSheetNewsEdit_->text();
    if (styleSheetNews.isEmpty()) styleSheetNews = mainApp->styleSheetNewsDefaultFile();
    settings.setValue("Settings/styleSheetNews", styleSheetNews);
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageNotifications)) {
    soundNotifyPath_ = optionsDialog_->notifierPage()->editSoundNotifier_->text();
    reloadNotificationFeeds();
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageLanguage) &&
      mainApp->language() != optionsDialog_->languagePage()->language()) {
    mainApp->setLanguage(optionsDialog_->languagePage()->language());
    mainApp->setTranslateApplication();
  }

  if (optionsDialog_->isPageSaveEligible(OptionsDialog::PageFontsColors)) {
    auto *p = optionsDialog_->fontsColorsPage();
    for (int row = 0; row < kFontOptionCount; ++row) {
      const QFont selected = p->fontOptionValue(row);
      QFont font = fontForOption(row);
      font.setFamily(selected.family());
      font.setPointSize(selected.pointSize());
      setFontForOption(row, font);
    }

    for (int row = 0; row < kColorOptionCount; ++row) {
      setColorValueForOption(row, p->colorOptionValue(row));
      if (colorValueForOption(row) == defaultColorForOption(row))
        customColorRows_.remove(row);
      else
        customColorRows_.insert(row);
    }
    feedsView_->setStyleSheet(feedsModel_->backgroundColor_.isEmpty()
        ? QString() : QString("#feedsView_ {background: %1;}").arg(feedsModel_->backgroundColor_));
  }

  delete optionsDialog_;
  optionsDialog_ = nullptr;

  AppSettings::autoUpdatefeedsStartUp.set(updateFeedsStartUp);

  saveSettings();
  updateGlobalStatusBar();
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
  showOptionDlg(OptionsDialog::PageLabels);
}

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
#include "common.h"
#include "cookiesdialog.h"
#include "customizetoolbardialog.h"
#include "downloadmanager.h"
#include "mainapplication.h"
#include "projectmetadata.h"
#include "settings.h"
#include "shareservice.h"
#include "statusbarcontroller.h"
#include "trayiconcontroller.h"
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QStatusBar>
#include <QStyle>
#include <utility>

namespace {
QSize virtualDesktopSize()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  return screen ? screen->virtualGeometry().size() : QSize();
}
}

// ---------------------------------------------------------------------------
void MainWindow::createFeedsWidget()
{
  feedsModel_ = new FeedsModel(this);

  feedsProxyModel_ = new FeedsProxyModel(feedsModel_);
  feedsProxyModel_->setSourceModel(feedsModel_);

  feedsView_ = new FeedsView(this);
  feedsView_->setModel(feedsProxyModel_);
  feedsView_->setSourceModel(feedsModel_);
  connect(feedsView_, &FeedsView::navigationActivated,
          this, &MainWindow::slotFeedClicked);
  feedsModel_->setView(feedsView_);

  for (int i = 0; i < feedsView_->model()->columnCount(); ++i)
    feedsView_->hideColumn(i);
  feedsView_->showColumn(feedsView_->columnIndex("text"));
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("text"), QHeaderView::Stretch);
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("unread"), QHeaderView::ResizeToContents);
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("undeleteCount"), QHeaderView::ResizeToContents);
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("updated"), QHeaderView::ResizeToContents);

  feedsToolBar_ = new QToolBar(this);
  feedsToolBar_->setObjectName("feedsToolBar");
  feedsToolBar_->setStyleSheet("QToolBar { border: none; padding: 0px; }");

  QHBoxLayout *feedsPanelLayout = new QHBoxLayout();
  feedsPanelLayout->setContentsMargins(2, 2, 2, 2);
  feedsPanelLayout->addWidget(feedsToolBar_, 1);

  feedsPanel_ = new QWidget(this);
  feedsPanel_->setObjectName("feedsPanel_");
  feedsPanel_->setStyleSheet(
        QString("#feedsPanel_ {border-bottom: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  feedsPanel_->setLayout(feedsPanelLayout);

  findFeeds_ = new FindFeed(this);
  QVBoxLayout *findFeedsLayout = new QVBoxLayout();
  findFeedsLayout->setContentsMargins(2, 2, 2, 2);
  findFeedsLayout->addWidget(findFeeds_);
  findFeedsWidget_ = new QWidget(this);
  findFeedsWidget_->hide();
  findFeedsWidget_->setLayout(findFeedsLayout);

  categoriesTree_ = new CategoriesTreeWidget(this);

  categoriesLabel_ = new QLabel(this);
  categoriesLabel_->setObjectName("categoriesLabel_");

  showCategoriesButton_ = new ToolButton(this);
  showCategoriesButton_->setFocusPolicy(Qt::NoFocus);
  showCategoriesButton_->setMaximumSize(16, 16);
  showCategoriesButton_->setAutoRaise(true);

  QHBoxLayout *categoriesPanelLayout = new QHBoxLayout();
  categoriesPanelLayout->setContentsMargins(2, 2, 2, 2);
  categoriesPanelLayout->addSpacing(2);
  categoriesPanelLayout->addWidget(categoriesLabel_, 1);
  categoriesPanelLayout->addWidget(showCategoriesButton_);

  categoriesPanel_ = new QWidget(this);
  categoriesPanel_->setObjectName("categoriesPanel_");
  categoriesPanel_->setLayout(categoriesPanelLayout);

  QVBoxLayout *categoriesLayout = new QVBoxLayout();
  categoriesLayout->setContentsMargins(0, 0, 0, 0);
  categoriesLayout->setSpacing(0);
  categoriesLayout->addWidget(categoriesPanel_);
  categoriesLayout->addWidget(categoriesTree_, 1);

  categoriesWidget_ = new QWidget(this);
  categoriesWidget_->setLayout(categoriesLayout);

  feedsSplitter_ = new QSplitter(Qt::Vertical);
  feedsSplitter_->setChildrenCollapsible(false);
  feedsSplitter_->setHandleWidth(1);
  feedsSplitter_->setStyleSheet(
        QString("QSplitter::handle {background: %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  feedsSplitter_->addWidget(feedsView_);
  feedsSplitter_->addWidget(categoriesWidget_);
  feedsSplitter_->setStretchFactor(0, 1);

#define CATEGORIES_HEIGHT 210
  QList <int> sizes;
  sizes << virtualDesktopSize().height() << CATEGORIES_HEIGHT;
  feedsSplitter_->setSizes(sizes);

  QVBoxLayout *feedsLayout = new QVBoxLayout();
  feedsLayout->setContentsMargins(0, 0, 0, 0);
  feedsLayout->setSpacing(0);
  feedsLayout->addWidget(feedsPanel_);
  feedsLayout->addWidget(findFeedsWidget_);
  feedsLayout->addWidget(feedsSplitter_, 1);

  feedsWidget_ = new QFrame(this);
  feedsWidget_->setFrameStyle(QFrame::NoFrame);
  feedsWidget_->setLayout(feedsLayout);

  connect(feedsView_, &QAbstractItemView::pressed, this, &MainWindow::slotFeedClicked);
  connect(feedsView_, &FeedsView::signalMiddleClicked, this, &MainWindow::slotOpenFeedNewTab);
  connect(feedsView_, &FeedsView::signalDoubleClicked, this, &MainWindow::slotGetFeed);
  connect(feedsView_, &FeedsView::pressKeyUp, this, &MainWindow::slotFeedUpPressed);
  connect(feedsView_, &FeedsView::pressKeyDown, this, &MainWindow::slotFeedDownPressed);
  connect(feedsView_, &FeedsView::pressKeyHome, this, &MainWindow::slotFeedHomePressed);
  connect(feedsView_, &FeedsView::pressKeyEnd, this, &MainWindow::slotFeedEndPressed);
  connect(feedsView_, &FeedsView::pressKeyPageUp, this, &MainWindow::slotFeedPageUpPressed);
  connect(feedsView_, &FeedsView::pressKeyPageDown, this, &MainWindow::slotFeedPageDownPressed);
  connect(feedsView_, &FeedsView::signalDropped, this, &MainWindow::slotMoveIndex);
  connect(feedsView_, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenuFeed);

  connect(findFeeds_, &QLineEdit::textChanged, this, &MainWindow::slotFindFeeds);
  connect(findFeeds_, &FindFeed::signalSelectFind, this, &MainWindow::slotSelectFind);
  connect(findFeeds_, &QLineEdit::returnPressed, this, &MainWindow::slotSelectFind);
  connect(findFeeds_, &FindFeed::signalVisible, this, &MainWindow::findFeedVisible);

  connect(categoriesTree_, &QTreeWidget::itemPressed,
          this, [this](QTreeWidgetItem *item, int column) {
    slotCategoriesClicked(item, column);
  });
  connect(categoriesTree_, &CategoriesTreeWidget::signalMiddleClicked,
          this, &MainWindow::slotOpenCategoryNewTab);
  connect(categoriesTree_, &CategoriesTreeWidget::signalClearDeleted, this, &MainWindow::clearDeleted);
  connect(categoriesTree_, &CategoriesTreeWidget::signalMarkRead, this, &MainWindow::slotMarkReadCategory);
  connect(showCategoriesButton_, &QAbstractButton::clicked, this, &MainWindow::showNewsCategoriesTree);
  connect(feedsSplitter_, &QSplitter::splitterMoved, this, &MainWindow::feedsSplitterMoved);

  categoriesLabel_->installEventFilter(this);
  categoriesTree_->installEventFilter(this);
}
// ---------------------------------------------------------------------------
void MainWindow::createToolBarNull()
{
  pushButtonNull_ = new QPushButton(this);
  pushButtonNull_->setObjectName("pushButtonNull");
  pushButtonNull_->setIcon(QIcon(":/images/images/triangleR.png"));
  pushButtonNull_->setFixedWidth(6);
  pushButtonNull_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  pushButtonNull_->setFocusPolicy(Qt::NoFocus);
}
// ---------------------------------------------------------------------------
void MainWindow::createStatusBar()
{
  statusBarController_ = new StatusBarController(statusBar(), stopUpdateAct_,
                                                 autoLoadImagesToggle_, fullScreenAct_, this);
  statusBar()->installEventFilter(this);
}
// ---------------------------------------------------------------------------
void MainWindow::createTray()
{
  showWindowAct_ = new QAction(this);
  connect(showWindowAct_, &QAction::triggered, this, [this] { showWindows(); });
  QFont font = showWindowAct_->font();
  font.setBold(true);
  showWindowAct_->setFont(font);

  trayIconController_ = new TrayIconController(
        this, showWindowAct_, addFeedTrayAct_, updateAllFeedsAct_, markAllFeedsRead_,
        optionsAct_, exitAct_, this);

#ifndef Q_OS_MAC
  connect(trayIcon(), &QSystemTrayIcon::activated, this, &MainWindow::slotActivationTray);
  connect(this, &MainWindow::signalPlaceToTray, this, &MainWindow::slotPlaceToTray, Qt::QueuedConnection);
#endif
}

QSystemTrayIcon *MainWindow::trayIcon() const
{
  return trayIconController_->systemTrayIcon();
}

/** @brief Create tabbar widget
 *---------------------------------------------------------------------------*/
void MainWindow::createTabBarWidget()
{
  mainMenuButton_ = new ToolButton(this);
  mainMenuButton_->setObjectName("mainMenuButton");
  mainMenuButton_->setIcon(QIcon(":/images/menu"));

  tabBar_ = new TabBar();

  QHBoxLayout *tabBarLayout = new QHBoxLayout();
  tabBarLayout->setContentsMargins(0, 0, 0, 0);
  tabBarLayout->setSpacing(0);
  tabBarLayout->addWidget(mainMenuButton_);
  tabBarLayout->addSpacing(5);
  tabBarLayout->addWidget(tabBar_, 1);

  tabBarWidget_ = new QWidget(this);
  tabBarWidget_->setObjectName("tabBarWidget");
  tabBarWidget_->setMinimumHeight(1);
  tabBarWidget_->setLayout(tabBarLayout);

  connect(mainMenuButton_, &QAbstractButton::clicked, this, &MainWindow::showMainMenu);
  connect(tabBar_, &TabBar::closeTab, this, &MainWindow::slotCloseTab);
  connect(tabBar_, &QTabBar::currentChanged, this, [this] {
    deferredDatabaseUi_.erase(DeferredDatabaseUi::FeedSelection);
    deferredDatabaseUi_.erase(DeferredDatabaseUi::TabSelection);
  });
  connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::slotTabCurrentChanged, Qt::QueuedConnection);
  connect(tabBar_, &QTabBar::tabMoved, this, &MainWindow::slotTabMoved);
  connect(this, &MainWindow::signalSetCurrentTab, this, &MainWindow::setCurrentTab, Qt::QueuedConnection);

  connect(closeTabAct_, &QAction::triggered, tabBar_, &TabBar::slotCloseTab);
  connect(closeOtherTabsAct_, &QAction::triggered, tabBar_, &TabBar::slotCloseOtherTabs);
  connect(closeAllTabsAct_, &QAction::triggered, tabBar_, &TabBar::slotCloseAllTab);
  connect(nextTabAct_, &QAction::triggered, tabBar_, &TabBar::slotNextTab);
  connect(prevTabAct_, &QAction::triggered, tabBar_, &TabBar::slotPrevTab);
}

void MainWindow::createCentralWidget()
{
  stackedWidget_ = new QStackedWidget(this);
  stackedWidget_->setObjectName("stackedWidget_");
  stackedWidget_->setFrameStyle(QFrame::NoFrame);

  mainSplitter_ = new QSplitter(this);
  mainSplitter_ ->setFrameStyle(QFrame::NoFrame);
  mainSplitter_->setHandleWidth(1);
  mainSplitter_->setStyleSheet(
        QString("QSplitter::handle {background: qlineargradient("
                "x1: 0, y1: 0, x2: 0, y2: 1,"
                "stop: 0 %1, stop: 0.07 %2);}").
        arg(feedsPanel_->palette().window().color().name()).
        arg(qApp->palette().color(QPalette::Dark).name()));
  mainSplitter_->setChildrenCollapsible(false);
  mainSplitter_->addWidget(feedsWidget_);
  mainSplitter_->addWidget(stackedWidget_);
  mainSplitter_->setStretchFactor(1, 1);

#define FEEDS_WIDTH 180
  QList <int> sizes;
  sizes << FEEDS_WIDTH << virtualDesktopSize().width();
  mainSplitter_->setSizes(sizes);

  QHBoxLayout *mainLayout1 = new QHBoxLayout();
  mainLayout1->addWidget(pushButtonNull_);
  mainLayout1->addWidget(mainSplitter_, 1);

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  mainLayout->addWidget(tabBarWidget_);
  mainLayout->addLayout(mainLayout1, 1);

  centralWidget_ = new QWidget(this);
  centralWidget_->setLayout(mainLayout);

  setCentralWidget(centralWidget_);
}

struct MainWindow::UiBindings {
  enum ActionFlags {
    NoFlags     = 0,
    AddToWindow = 1 << 0,
    Checkable   = 1 << 1,
    Checked     = 1 << 2,
  };

  struct ActionSpec {
    QAction *MainWindow::*member;
    const char *objectName;
    const char *iconPath;
    int flags;
    const char *text;
    const char *toolTip;
  };

  struct ShortcutSpec {
    QAction *MainWindow::*member;
    int defaultKey;
  };

  struct MenuTitleSpec {
    QMenu *MainWindow::*menu;
    const char *title;
  };

  static const ActionSpec kActions[];
  static const ShortcutSpec kShortcuts[];
  static const MenuTitleSpec kMenuTitles[];
};

const MainWindow::UiBindings::ActionSpec MainWindow::UiBindings::kActions[] = {
  { &MainWindow::addAct_,                     "newAct",                   ":/images/add",                       AddToWindow,                       QT_TR_NOOP("&Add"),                                                QT_TR_NOOP("Add New Feed") },
  { &MainWindow::addFeedAct_,                 "addFeedAct",               ":/images/feed",                      AddToWindow,                       QT_TR_NOOP("&Feed..."),                                            QT_TR_NOOP("Add New Feed") },
  { &MainWindow::addFeedTrayAct_,             nullptr,                    ":/images/feed",                      NoFlags,                           QT_TR_NOOP("Add Feed..."),                                         nullptr },
  { &MainWindow::addFolderAct_,               "addFolderAct",             ":/images/folder",                    AddToWindow,                       QT_TR_NOOP("F&older..."),                                          QT_TR_NOOP("Add New Folder") },
  { &MainWindow::openFeedNewTabAct_,          "openNewTabAct",            nullptr,                              AddToWindow,                       QT_TR_NOOP("Open in New Tab"),                                     nullptr },
  { &MainWindow::deleteFeedAct_,              "deleteFeedAct",            ":/images/delete",                    AddToWindow,                       QT_TR_NOOP("&Delete..."),                                          QT_TR_NOOP("Delete Selected Feed") },
  { &MainWindow::importFeedsAct_,             "importFeedsAct",           ":/images/importFeeds",               AddToWindow,                       QT_TR_NOOP("&Import Feeds..."),                                    QT_TR_NOOP("Import Feeds from OPML File") },
  { &MainWindow::exportFeedsAct_,             "exportFeedsAct",           ":/images/exportFeeds",               AddToWindow,                       QT_TR_NOOP("&Export Feeds..."),                                    QT_TR_NOOP("Export Feeds to OPML File") },
  { &MainWindow::createBackupAct_,            "createBackupAct",          ":/images/backup",                    AddToWindow,                       QT_TR_NOOP("&Create Backup..."),                                   nullptr },
  { &MainWindow::showMenuBarAct_,             nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("S&how Menu Bar"),                                      nullptr },
  { &MainWindow::exitAct_,                    "exitAct",                  nullptr,                              AddToWindow,                       QT_TR_NOOP("E&xit"),                                               nullptr },
  { &MainWindow::mainToolbarToggle_,          nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Main Toolbar"),                                        nullptr },
  { &MainWindow::feedsToolbarToggle_,         nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Feeds Toolbar"),                                       nullptr },
  { &MainWindow::newsToolbarToggle_,          nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("News Toolbar"),                                        nullptr },
  { &MainWindow::categoriesPanelToggle_,      nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Panel Categories"),                                    nullptr },
  { &MainWindow::statusBarToggle_,            nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Status Bar"),                                          nullptr },
  { &MainWindow::customizeMainToolbarAct_,    "customizeMainToolbarAct",  nullptr,                              NoFlags,                           QT_TR_NOOP("Main Toolbar..."),                                     nullptr },
  { &MainWindow::customizeMainToolbarAct2_,   nullptr,                    nullptr,                              NoFlags,                           QT_TR_NOOP("Customize Toolbar..."),                                nullptr },
  { &MainWindow::toolBarLockAct_,             nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Lock Toolbar"),                                        nullptr },
  { &MainWindow::toolBarHideAct_,             nullptr,                    nullptr,                              NoFlags,                           QT_TR_NOOP("Hide Toolbar"),                                        nullptr },
  { &MainWindow::customizeFeedsToolbarAct_,   "customizeFeedsToolbarAct", nullptr,                              NoFlags,                           QT_TR_NOOP("Feeds Toolbar..."),                                    nullptr },
  { &MainWindow::customizeNewsToolbarAct_,    "customizeNewsToolbarAct",  nullptr,                              NoFlags,                           QT_TR_NOOP("News Toolbar..."),                                     nullptr },
  { &MainWindow::classicLayoutAct_,           "classicLayoutAct_",        ":/images/layout_classic",            Checkable,                         QT_TR_NOOP("Classic"),                                             nullptr },
  { &MainWindow::newspaperLayoutAct_,         "newspaperLayoutAct_",      ":/images/layout_newspaper",          Checkable,                         QT_TR_NOOP("Newspaper"),                                           nullptr },
  { &MainWindow::layoutToggle_,               "layoutToggle",             ":/images/layout_classic",            AddToWindow,                       QT_TR_NOOP("Layout"),                                              nullptr },
  { &MainWindow::topBrowserPositionAct_,      nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Top"),                                                 nullptr },
  { &MainWindow::bottomBrowserPositionAct_,   nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Bottom"),                                              nullptr },
  { &MainWindow::rightBrowserPositionAct_,    nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Right"),                                               nullptr },
  { &MainWindow::leftBrowserPositionAct_,     nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Left"),                                                nullptr },
  { &MainWindow::autoLoadImagesToggle_,       "autoLoadImagesToggle",     ":/images/imagesOn",                  AddToWindow,                       nullptr,                                                           nullptr },
  { &MainWindow::printAct_,                   "printAct",                 ":/images/printer",                   AddToWindow,                       QT_TR_NOOP("Print..."),                                            QT_TR_NOOP("Print Article") },
  { &MainWindow::printPreviewAct_,            "printPreviewAct",          ":/images/printer",                   AddToWindow,                       QT_TR_NOOP("Print Preview..."),                                    QT_TR_NOOP("Preview Article") },
  { &MainWindow::savePageAsAct_,              "savePageAsAct",            ":/images/save_as",                   AddToWindow,                       QT_TR_NOOP("Save As..."),                                          QT_TR_NOOP("Save Article As...") },
  { &MainWindow::zoomInAct_,                  "zoomInAct",                ":/images/zoomIn",                    AddToWindow,                       QT_TR_NOOP("Zoom In"),                                             QT_TR_NOOP("Zoom in") },
  { &MainWindow::zoomOutAct_,                 "zoomOutAct",               ":/images/zoomOut",                   AddToWindow,                       QT_TR_NOOP("Zoom Out"),                                            QT_TR_NOOP("Zoom out") },
  { &MainWindow::zoomTo100Act_,               "zoomTo100Act",             nullptr,                              AddToWindow,                       QT_TR_NOOP("100%"),                                                QT_TR_NOOP("Reset zoom") },
  { &MainWindow::updateFeedAct_,              "updateFeedAct",            ":/images/updateFeed",                AddToWindow,                       nullptr,                                                           nullptr },
  { &MainWindow::updateAllFeedsAct_,          "updateAllFeedsAct",        ":/images/updateAllFeeds",            AddToWindow,                       QT_TR_NOOP("Update All"),                                          QT_TR_NOOP("Update All Feeds") },
  { &MainWindow::stopUpdateAct_,              "stopUpdateAct",            ":/images/close",                     AddToWindow,                       QT_TR_NOOP("Stop Updating Feeds"),                                 QT_TR_NOOP("Stop Updating Feeds") },
  { &MainWindow::markAllFeedsRead_,           "markAllFeedRead",          ":/images/markReadAll",               AddToWindow,                       QT_TR_NOOP("Mark All Feeds Read"),                                 nullptr },
  { &MainWindow::indentationFeedsTreeAct_,    nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Enable Indentation"),                                  nullptr },
  { &MainWindow::sortedByTitleFeedsTreeAct_,  nullptr,                    nullptr,                              NoFlags,                           QT_TR_NOOP("Sort by Name"),                                        nullptr },
  { &MainWindow::collapseAllFoldersAct_,      "collapseAllFolderAct",     ":/images/bulletMinus",               AddToWindow,                       QT_TR_NOOP("Collapse All Folders"),                                nullptr },
  { &MainWindow::expandAllFoldersAct_,        "expandAllFolderAct",       ":/images/bulletPlus",                AddToWindow,                       QT_TR_NOOP("Expand All Folders"),                                  nullptr },
  { &MainWindow::markNewsRead_,               "markNewsRead",             ":/images/markRead",                  AddToWindow,                       QT_TR_NOOP("Mark Article Read/Unread"),                            QT_TR_NOOP("Mark Current Article Read/Unread") },
  { &MainWindow::markAllNewsRead_,            "markAllNewsRead",          ":/images/markReadAll",               AddToWindow,                       QT_TR_NOOP("Mark All Articles Read"),                              QT_TR_NOOP("Mark All Articles Read") },
  { &MainWindow::showDownloadManagerAct_,     "showDownloadManagerAct",   ":/images/download",                  AddToWindow,                       QT_TR_NOOP("Downloads"),                                           nullptr },
  { &MainWindow::showCleanUpWizardAct_,       "showCleanUpWizardAct",     ":/images/cleanup",                   AddToWindow,                       QT_TR_NOOP("Clean Up..."),                                         nullptr },
  { &MainWindow::setNewsFiltersAct_,          "setNewsFiltersAct",        ":/images/filterOff",                 AddToWindow,                       QT_TR_NOOP("Article Filters..."),                                  nullptr },
  { &MainWindow::bulkFeedSettingsAct_,        "bulkFeedSettingsAct",      nullptr,                              AddToWindow,                       QT_TR_NOOP("Bulk configure feeds..."),                             nullptr },
  { &MainWindow::websiteCookiesAct_,          "websiteCookiesAct",        nullptr,                              AddToWindow,                       QT_TR_NOOP("Website cookies…"),                                    nullptr },
  { &MainWindow::setFilterNewsAct_,           "setFilterNewsAct",         ":/images/filterOff",                 AddToWindow,                       QT_TR_NOOP("Filter Articles..."),                                  nullptr },
  { &MainWindow::optionsAct_,                 "optionsAct",               ":/images/options",                   AddToWindow,                       QT_TR_NOOP("Options..."),                                          QT_TR_NOOP("Open Options Dialog") },
  { &MainWindow::feedsFilter_,                "feedsFilter",              ":/images/filterOff",                 AddToWindow,                       QT_TR_NOOP("Filter Feeds"),                                        nullptr },
  { &MainWindow::filterFeedsAll_,             "filterFeedsAll_",          nullptr,                              Checkable | Checked,               QT_TR_NOOP("Show All"),                                            nullptr },
  { &MainWindow::filterFeedsNew_,             "filterFeedsNew_",          nullptr,                              Checkable,                         QT_TR_NOOP("Show New"),                                            nullptr },
  { &MainWindow::filterFeedsUnread_,          "filterFeedsUnread_",       nullptr,                              Checkable,                         QT_TR_NOOP("Show Unread"),                                         nullptr },
  { &MainWindow::filterFeedsStarred_,         "filterFeedsStarred_",      nullptr,                              Checkable,                         QT_TR_NOOP("Show Starred Feeds"),                                  nullptr },
  { &MainWindow::filterFeedsError_,           "filterFeedsError_",        nullptr,                              Checkable,                         QT_TR_NOOP("Show Not Working Feeds"),                              nullptr },
  { &MainWindow::newsFilter_,                 "newsFilter",               ":/images/filterOff",                 AddToWindow,                       QT_TR_NOOP("Filter News"),                                         nullptr },
  { &MainWindow::filterNewsAll_,              "filterNewsAll_",           nullptr,                              Checkable | Checked,               QT_TR_NOOP("Show All"),                                            nullptr },
  { &MainWindow::filterNewsNew_,              "filterNewsNew_",           nullptr,                              Checkable,                         QT_TR_NOOP("Show New"),                                            nullptr },
  { &MainWindow::filterNewsUnread_,           "filterNewsUnread_",        nullptr,                              Checkable,                         QT_TR_NOOP("Show Unread"),                                         nullptr },
  { &MainWindow::filterNewsStar_,             "filterNewsStar_",          nullptr,                              Checkable,                         QT_TR_NOOP("Show Starred"),                                        nullptr },
  { &MainWindow::filterNewsNotStarred_,       "filterNewsNotStarred_",    nullptr,                              Checkable,                         QT_TR_NOOP("Show Not Starred"),                                    nullptr },
  { &MainWindow::filterNewsUnreadStar_,       "filterNewsUnreadStar_",    nullptr,                              Checkable,                         QT_TR_NOOP("Show Unread or Starred"),                              nullptr },
  { &MainWindow::filterNewsLastDay_,          "filterNewsLastDay_",       nullptr,                              Checkable,                         QT_TR_NOOP("Show Last Day"),                                       nullptr },
  { &MainWindow::filterNewsLastWeek_,         "filterNewsLastWeek_",      nullptr,                              Checkable,                         QT_TR_NOOP("Show Last 7 Days"),                                    nullptr },
  { &MainWindow::aboutAct_,                   "AboutAct_",                nullptr,                              NoFlags,                           QT_TR_NOOP("About..."),                                            QT_TR_NOOP("Show 'About' Dialog") },
  { &MainWindow::updateAppAct_,               "UpdateApp_",               nullptr,                              NoFlags,                           QT_TR_NOOP("Check for Updates..."),                                nullptr },
  { &MainWindow::reportProblemAct_,           "reportProblemAct_",        nullptr,                              NoFlags,                           QT_TR_NOOP("Report a Problem..."),                                 nullptr },
  { &MainWindow::openInExternalBrowserAct_,   "openInExternalBrowserAct", ":/images/openBrowser",               AddToWindow,                       QT_TR_NOOP("Open"),                                                QT_TR_NOOP("Open") },
  { &MainWindow::markStarAct_,                "markStarAct",              ":/images/starOn",                    AddToWindow,                       QT_TR_NOOP("Star"),                                                QT_TR_NOOP("Star Article") },
  { &MainWindow::deleteNewsAct_,              "deleteNewsAct",            ":/images/delete",                    AddToWindow,                       QT_TR_NOOP("Delete"),                                              QT_TR_NOOP("Delete Selected Articles") },
  { &MainWindow::deleteAllNewsAct_,           "deleteAllNewsAct",         nullptr,                              AddToWindow,                       QT_TR_NOOP("Delete All Articles"),                                 QT_TR_NOOP("Delete All Articles from List") },
  { &MainWindow::restoreNewsAct_,             "restoreNewsAct",           ":/images/images/arrow_turn_left.png",NoFlags,                           QT_TR_NOOP("Restore"),                                             QT_TR_NOOP("Restore Article") },
  { &MainWindow::restoreLastNewsAct_,         "restoreLastNewsAct",       ":/images/images/arrow_turn_left.png",AddToWindow,                       QT_TR_NOOP("Restore Last Deleted Article"),                        nullptr },
  { &MainWindow::markFeedRead_,               "markFeedRead",             ":/images/markRead",                  AddToWindow,                       QT_TR_NOOP("Mark Read"),                                           QT_TR_NOOP("Mark Feed Read") },
  { &MainWindow::feedProperties_,             "feedProperties",           ":/images/preferencesFeed",           AddToWindow,                       QT_TR_NOOP("Properties"),                                          QT_TR_NOOP("Properties") },
  { &MainWindow::feedKeyUpAct_,               "feedKeyUp",                nullptr,                              AddToWindow,                       QT_TR_NOOP("Previous Feed"),                                       nullptr },
  { &MainWindow::feedKeyDownAct_,             "feedKeyDownAct",           nullptr,                              AddToWindow,                       QT_TR_NOOP("Next Feed"),                                           nullptr },
  { &MainWindow::newsKeyUpAct_,               "newsKeyUpAct",             nullptr,                              AddToWindow,                       QT_TR_NOOP("Previous Article"),                                    nullptr },
  { &MainWindow::newsKeyDownAct_,             "newsKeyDownAct",           nullptr,                              AddToWindow,                       QT_TR_NOOP("Next Article"),                                        nullptr },
  { &MainWindow::newsKeyPageUpAct_,           "newsKeyPageUpAct",         nullptr,                              AddToWindow,                       QT_TR_NOOP("Article Page Up"),                                     nullptr },
  { &MainWindow::newsKeyPageDownAct_,         "newsKeyPageDownAct",       nullptr,                              AddToWindow,                       QT_TR_NOOP("Article Page Down"),                                   nullptr },
  { &MainWindow::switchFocusAct_,             "switchFocusAct",           nullptr,                              AddToWindow,                       QT_TR_NOOP("Switch Focus to Next Panel"),                          QT_TR_NOOP("Switch Focus to Next Panel (Feed Tree, Article List, Article Pane)") },
  { &MainWindow::switchFocusPrevAct_,         "switchFocusPrevAct",       nullptr,                              AddToWindow,                       QT_TR_NOOP("Switch Focus to Previous Panel"),                      QT_TR_NOOP("Switch Focus to Previous Panel (Feed Tree, Article Pane, Article List)") },
  { &MainWindow::feedsWidgetVisibleAct_,      "visibleFeedsWidgetAct",    nullptr,                              AddToWindow | Checkable,           QT_TR_NOOP("Feeds Pane"),                                          nullptr },
  { &MainWindow::showUnreadCount_,            nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Number of unread articles"),                           nullptr },
  { &MainWindow::showUndeleteCount_,          nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Total number of articles"),                            nullptr },
  { &MainWindow::showLastUpdated_,            nullptr,                    nullptr,                              Checkable,                         QT_TR_NOOP("Last update"),                                         nullptr },
  { &MainWindow::openDescriptionNewsAct_,     "openDescriptionNewsAct",   nullptr,                              AddToWindow,                       QT_TR_NOOP("Open Article"),                                        QT_TR_NOOP("Open Article Description") },
  { &MainWindow::findTextAct_,                "findTextAct",              nullptr,                              AddToWindow,                       QT_TR_NOOP("Find"),                                                nullptr },
  { &MainWindow::placeToTrayAct_,             "placeToTrayAct",           nullptr,                              AddToWindow,                       QT_TR_NOOP("Minimize to Tray"),                                    QT_TR_NOOP("Minimize Application to Tray") },
  { &MainWindow::findFeedAct_,                "findFeedAct",              ":/images/images/findFeed.png",       AddToWindow | Checkable,           QT_TR_NOOP("Search Feed"),                                         QT_TR_NOOP("Search Feed") },
  { &MainWindow::fullScreenAct_,              "fullScreenAct",            ":/images/images/fullScreen.png",     AddToWindow,                       QT_TR_NOOP("Full Screen"),                                         QT_TR_NOOP("Full Screen") },
  { &MainWindow::stayOnTopAct_,               "stayOnTopAct",             nullptr,                              AddToWindow | Checkable,           QT_TR_NOOP("Stay On Top"),                                         QT_TR_NOOP("Stay On Top") },
  { &MainWindow::newsLabelAction_,            "newsLabelAction",          nullptr,                              AddToWindow,                       QT_TR_NOOP("Label"),                                               nullptr },
  { &MainWindow::showLabelsMenuAct_,          "showLabelsMenuAct",        nullptr,                              AddToWindow,                       QT_TR_NOOP("Show labels menu"),                                    nullptr },
  { &MainWindow::closeTabAct_,                "closeTabAct",              nullptr,                              AddToWindow,                       QT_TR_NOOP("Close Tab"),                                           nullptr },
  { &MainWindow::closeOtherTabsAct_,          "closeOtherTabsAct",        nullptr,                              AddToWindow,                       QT_TR_NOOP("Close Other Tabs"),                                    nullptr },
  { &MainWindow::closeAllTabsAct_,            "closeAllTabsAct",          nullptr,                              AddToWindow,                       QT_TR_NOOP("Close All Tabs"),                                      nullptr },
  { &MainWindow::nextTabAct_,                 "nextTabAct",               nullptr,                              AddToWindow,                       QT_TR_NOOP("Switch to next tab"),                                  nullptr },
  { &MainWindow::prevTabAct_,                 "prevTabAct",               nullptr,                              AddToWindow,                       QT_TR_NOOP("Switch to previous tab"),                              nullptr },
  { &MainWindow::reduceNewsListAct_,          "reduceNewsListAct",        nullptr,                              AddToWindow,                       QT_TR_NOOP("Decrease article list / increase article pane"),       nullptr },
  { &MainWindow::increaseNewsListAct_,        "increaseNewsListAct",      nullptr,                              AddToWindow,                       QT_TR_NOOP("Increase article list / decrease article pane"),       nullptr },
  { &MainWindow::nextUnreadNewsAct_,          "nextUnreadNewsAct",        ":/images/moveDown",                  AddToWindow,                       QT_TR_NOOP("Next Unread Article"),                                 nullptr },
  { &MainWindow::prevUnreadNewsAct_,          "prevUnreadNewsAct",        ":/images/moveUp",                    AddToWindow,                       QT_TR_NOOP("Previous Unread Article"),                             nullptr },
  { &MainWindow::openHomeFeedAct_,            "openHomeFeedsAct",         ":/images/homePage",                  AddToWindow,                       QT_TR_NOOP("Open Feed Homepage"),                                  nullptr },
  { &MainWindow::copyLinkAct_,                "copyLinkAct",              ":/images/copy",                      AddToWindow,                       QT_TR_NOOP("Copy Link"),                                           QT_TR_NOOP("Copy Article Link") },
  { &MainWindow::pageUpWebViewAct_,           "pageUpWebViewAct",         nullptr,                              AddToWindow,                       QT_TR_NOOP("Page up (Article)"),                                   nullptr },
  { &MainWindow::pageDownWebViewAct_,         "pageDownWebViewAct",       nullptr,                              AddToWindow,                       QT_TR_NOOP("Page down (Article)"),                                 nullptr },
  { &MainWindow::nextFolderAct_,              "nextFolderAct",            nullptr,                              AddToWindow,                       QT_TR_NOOP("Next Folder"),                                         nullptr },
  { &MainWindow::prevFolderAct_,              "prevFolderAct",            nullptr,                              AddToWindow,                       QT_TR_NOOP("Previous Folder"),                                     nullptr },
  { &MainWindow::expandFolderAct_,            "expandFolderAct",          nullptr,                              AddToWindow,                       QT_TR_NOOP("Expand Folder"),                                       nullptr },
  { &MainWindow::settingPageLabelsAct_,       "settingPageLabelsAct",     nullptr,                              AddToWindow,                       QT_TR_NOOP("Settings Page: Labels"),                               nullptr },
};

const MainWindow::UiBindings::ShortcutSpec MainWindow::UiBindings::kShortcuts[] = {
  { &MainWindow::addFeedAct_,               -1 }, // QKeySequence::New
  { &MainWindow::addFolderAct_,             int(Qt::CTRL | Qt::SHIFT | Qt::Key_N) },
  { &MainWindow::deleteFeedAct_,            0 },
  { &MainWindow::createBackupAct_,          0 },
  { &MainWindow::exitAct_,                  int(Qt::CTRL | Qt::Key_Q) }, // standart on other OS
  { &MainWindow::updateFeedAct_,            int(Qt::Key_F5) },
  { &MainWindow::updateAllFeedsAct_,        int(Qt::CTRL | Qt::Key_F5) },
  { &MainWindow::stopUpdateAct_,            0 },
  { &MainWindow::openHomeFeedAct_,          0 },
  { &MainWindow::showDownloadManagerAct_,   0 },
  { &MainWindow::showCleanUpWizardAct_,     0 },
  { &MainWindow::setNewsFiltersAct_,        0 },
  { &MainWindow::bulkFeedSettingsAct_,      0 },
  { &MainWindow::websiteCookiesAct_,        0 },
  { &MainWindow::setFilterNewsAct_,         0 },
  { &MainWindow::optionsAct_,               int(Qt::Key_F8) },
  { &MainWindow::deleteNewsAct_,            int(Qt::Key_Delete) },
  { &MainWindow::deleteAllNewsAct_,         int(Qt::CTRL | Qt::Key_Delete) },
  { &MainWindow::feedProperties_,           int(Qt::CTRL | Qt::Key_E) },
  { &MainWindow::feedKeyUpAct_,             int(Qt::CTRL | Qt::Key_Up) },
  { &MainWindow::feedKeyDownAct_,           int(Qt::CTRL | Qt::Key_Down) },
  { &MainWindow::newsKeyUpAct_,             int(Qt::Key_Left) },
  { &MainWindow::newsKeyDownAct_,           int(Qt::Key_Right) },
  { &MainWindow::newsKeyPageUpAct_,         0 },
  { &MainWindow::newsKeyPageDownAct_,       0 },
  { &MainWindow::nextUnreadNewsAct_,        0 },
  { &MainWindow::prevUnreadNewsAct_,        0 },
  { &MainWindow::nextFolderAct_,            0 },
  { &MainWindow::prevFolderAct_,            0 },
  { &MainWindow::importFeedsAct_,           0 },
  { &MainWindow::exportFeedsAct_,           0 },
  { &MainWindow::createBackupAct_,          0 },
  { &MainWindow::autoLoadImagesToggle_,     0 },
  { &MainWindow::markAllFeedsRead_,         0 },
  { &MainWindow::markFeedRead_,             0 },
  { &MainWindow::markNewsRead_,             0 },
  { &MainWindow::markAllNewsRead_,          0 },
  { &MainWindow::markStarAct_,              0 },
  { &MainWindow::collapseAllFoldersAct_,    0 },
  { &MainWindow::expandAllFoldersAct_,      0 },
  { &MainWindow::expandFolderAct_,          0 },
  { &MainWindow::openDescriptionNewsAct_,   int(Qt::Key_Return) },
  { &MainWindow::openInExternalBrowserAct_, int(Qt::CTRL | Qt::Key_O) },
  { &MainWindow::switchFocusAct_,           int(Qt::CTRL | Qt::Key_Tab) },
  { &MainWindow::switchFocusPrevAct_,       int(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab) },
  { &MainWindow::feedsWidgetVisibleAct_,    int(Qt::CTRL | Qt::Key_D) },
  { &MainWindow::placeToTrayAct_,           0 },
  { &MainWindow::zoomInAct_,                int(Qt::CTRL | Qt::Key_Plus) },
  { &MainWindow::zoomOutAct_,               int(Qt::CTRL | Qt::Key_Minus) },
  { &MainWindow::zoomTo100Act_,             int(Qt::CTRL | Qt::Key_0) },
  { &MainWindow::printAct_,                 int(Qt::CTRL | Qt::Key_P) },
  { &MainWindow::printPreviewAct_,          int(Qt::CTRL | Qt::SHIFT | Qt::Key_P) },
  { &MainWindow::savePageAsAct_,            int(Qt::CTRL | Qt::Key_S) },
  { &MainWindow::fullScreenAct_,            int(Qt::Key_F11) },
  { &MainWindow::stayOnTopAct_,             int(Qt::Key_F10) },
  { &MainWindow::layoutToggle_,             0 },
  { &MainWindow::closeTabAct_,              int(Qt::CTRL | Qt::Key_W) },
  { &MainWindow::closeOtherTabsAct_,        0 },
  { &MainWindow::closeAllTabsAct_,          0 },
  { &MainWindow::nextTabAct_,               0 },
  { &MainWindow::prevTabAct_,               0 },
  { &MainWindow::reduceNewsListAct_,        int(Qt::ALT | Qt::Key_Up) },
  { &MainWindow::increaseNewsListAct_,      int(Qt::ALT | Qt::Key_Down) },
  { &MainWindow::restoreLastNewsAct_,       int(Qt::CTRL | Qt::Key_Z) },
  { &MainWindow::findTextAct_,              int(Qt::CTRL | Qt::Key_F) },
  { &MainWindow::findFeedAct_,              0 },
  { &MainWindow::copyLinkAct_,              0 },
  { &MainWindow::pageUpWebViewAct_,         0 },
  { &MainWindow::pageDownWebViewAct_,       0 },
};

const MainWindow::UiBindings::MenuTitleSpec MainWindow::UiBindings::kMenuTitles[] = {
  { &MainWindow::fileMenu_,             QT_TR_NOOP("&File") },
  { &MainWindow::viewMenu_,             QT_TR_NOOP("&View") },
  { &MainWindow::feedMenu_,             QT_TR_NOOP("Fee&ds") },
  { &MainWindow::newsMenu_,             QT_TR_NOOP("&News") },
  { &MainWindow::browserMenu_,          QT_TR_NOOP("&Article") },
  { &MainWindow::toolsMenu_,            QT_TR_NOOP("&Tools") },
  { &MainWindow::helpMenu_,             QT_TR_NOOP("&Help") },
  { &MainWindow::customizeToolbarMenu_, QT_TR_NOOP("Customize Toolbar") },
  { &MainWindow::layoutMenu_,           QT_TR_NOOP("Layout") },
  { &MainWindow::styleMenu_,            QT_TR_NOOP("Application Theme") },
  { &MainWindow::browserPositionMenu_,  QT_TR_NOOP("Article Pane Position") },
  { &MainWindow::feedsColumnsMenu_,     QT_TR_NOOP("Columns") },
  { &MainWindow::browserZoomMenu_,      QT_TR_NOOP("Zoom") },
  { &MainWindow::toolbarsMenu_,         QT_TR_NOOP("Show/Hide") },
  { &MainWindow::newsSortByMenu_,       QT_TR_NOOP("Sort By") },
};

/** @brief Create actions for main menu and toolbar
 *---------------------------------------------------------------------------*/
void MainWindow::createActions()
{
  for (const auto &spec : UiBindings::kActions) {
    QAction *action = new QAction(this);
    if (spec.objectName)
      action->setObjectName(QString::fromLatin1(spec.objectName));
    if (spec.iconPath)
      action->setIcon(QIcon(QString::fromLatin1(spec.iconPath)));
    if (spec.flags & UiBindings::Checkable)
      action->setCheckable(true);
    if (spec.flags & UiBindings::Checked)
      action->setChecked(true);
    if (spec.flags & UiBindings::AddToWindow)
      this->addAction(action);
    this->*(spec.member) = action;
  }

  classicLayoutAct_->setData(0);
  newspaperLayoutAct_->setData(1);

  topBrowserPositionAct_->setData(TOP_POSITION);
  bottomBrowserPositionAct_->setData(BOTTOM_POSITION);
  rightBrowserPositionAct_->setData(RIGHT_POSITION);
  leftBrowserPositionAct_->setData(LEFT_POSITION);

  showUnreadCount_->setData(feedsView_->columnIndex("unread"));
  showUndeleteCount_->setData(feedsView_->columnIndex("undeleteCount"));
  showLastUpdated_->setData(feedsView_->columnIndex("updated"));

  QIcon cookieIcon;
  // Supply multiple resolutions for menus and high-DPI screens.
  for (int size : {16, 24, 32, 48, 64}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QFont font = this->font();
    font.setPixelSize(size - 2);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("🍪"));
    painter.end();
    cookieIcon.addPixmap(pixmap);
  }
  websiteCookiesAct_->setIcon(cookieIcon);

#ifdef HAVE_X11
  allWorkspacesAct_ = nullptr;
  if (QGuiApplication::platformName() == QLatin1String("xcb")) {
    allWorkspacesAct_ = new QAction(this);
    allWorkspacesAct_->setObjectName("allWorkspacesAct");
    allWorkspacesAct_->setCheckable(true);
    connect(allWorkspacesAct_, &QAction::triggered, this, &MainWindow::setAllWorkspaces);
  }
#endif

  newsSortByColumnGroup_ = new QActionGroup(this);
  newsSortByColumnGroup_->setExclusive(true);
  connect(newsSortByColumnGroup_, &QActionGroup::triggered, this, &MainWindow::setNewsSortByColumn);

  newsSortOrderGroup_ = new QActionGroup(this);
  newsSortOrderGroup_->setExclusive(true);
  for (const char *actionStr : { "sortOrderAsc", "sortOrderDesc" }) {
    QAction *newsSortOrderAct = new QAction(this);
    newsSortOrderAct->setObjectName(QString::fromLatin1(actionStr));
    newsSortOrderAct->setCheckable(true);
    newsSortOrderGroup_->addAction(newsSortOrderAct);
  }
  connect(newsSortOrderGroup_, &QActionGroup::triggered, this, &MainWindow::setNewsSortByColumn);

  newsLabelGroup_ = new QActionGroup(this);
  newsLabelGroup_->setExclusive(false);
  QSqlQuery q;
  q.exec("SELECT id, name, image FROM labels ORDER BY num");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    QByteArray byteArray = q.value(2).toByteArray();
    QPixmap imageLabel;
    if (!byteArray.isNull())
      imageLabel.loadFromData(byteArray);
    QAction *action = new QAction(QIcon(imageLabel), nameLabel, this);
    action->setObjectName(QString("labelAction_%1").arg(idLabel));
    action->setCheckable(true);
    action->setData(idLabel);
    newsLabelGroup_->addAction(action);
  }
  this->addActions(newsLabelGroup_->actions());

  if (newsLabelGroup_->actions().count()) {
    newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
    newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
    newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
  }

  shareGroup_ = new QActionGroup(this);
  shareGroup_->setExclusive(false);

  Settings applicationSettings;
  const QString userConfigurationDirectory =
      QFileInfo(applicationSettings.fileName()).absolutePath();
  const ShareServiceConfiguration shareConfiguration =
      ShareServiceLoader::load(mainApp->resourcesDir(), userConfigurationDirectory);
  for (const ShareService &service : shareConfiguration.services) {
    QAction *action = new QAction(QIcon(service.iconPath), service.name, this);
    action->setObjectName(service.actionName);
    action->setData(service.urlTemplate);
    shareGroup_->addAction(action);
  }

  if (shareGroup_->actions().isEmpty()) {
    QAction *missingConfiguration = new QAction(
        style()->standardIcon(QStyle::SP_MessageBoxCritical),
        tr("Missing configuration!"), this);
    missingConfiguration->setObjectName("missingShareConfigurationAct");
    missingConfiguration->setProperty("missingShareConfiguration", true);
    shareGroup_->addAction(missingConfiguration);

    shareConfigurationMessage_ =
        tr("Article sharing configuration could not be loaded.\n\n"
           "Install %3 and its sharing icons in:\n%1\n\n"
           "If no installed definition is present, the complete user configuration may be placed in:\n%2")
        .arg(QFileInfo(shareConfiguration.installedFile).absolutePath(),
             QFileInfo(shareConfiguration.userFile).absolutePath(), ProjectMetadata::sharingConfig());
    if (!shareConfiguration.errors.isEmpty()) {
      shareConfigurationMessage_.append(
          tr("\n\nDetails:\n%1").arg(shareConfiguration.errors.join(QStringLiteral("\n"))));
    }
  }

  this->addActions(shareGroup_->actions());

  struct SlotBinding {
    QAction *MainWindow::*action;
    void (MainWindow::*slot)();
  };
  static const SlotBinding kSlotBindings[] = {
    { &MainWindow::addAct_,                   &MainWindow::addFeed },
    { &MainWindow::addFeedAct_,               &MainWindow::addFeed },
    { &MainWindow::addFeedTrayAct_,           &MainWindow::addFeed },
    { &MainWindow::addFolderAct_,             &MainWindow::addFolder },
    { &MainWindow::openFeedNewTabAct_,        &MainWindow::slotOpenFeedNewTab },
    { &MainWindow::deleteFeedAct_,            &MainWindow::deleteItemFeedsTree },
    { &MainWindow::importFeedsAct_,           &MainWindow::slotImportFeeds },
    { &MainWindow::exportFeedsAct_,           &MainWindow::slotExportFeeds },
    { &MainWindow::createBackupAct_,          &MainWindow::createBackup },
    { &MainWindow::showMenuBarAct_,           &MainWindow::showMenuBar },
    { &MainWindow::exitAct_,                  &MainWindow::quitApp },
    { &MainWindow::customizeMainToolbarAct2_, &MainWindow::customizeMainToolbar },
    { &MainWindow::layoutToggle_,             QOverload<>::of(&MainWindow::setNewsLayout) },
    { &MainWindow::printAct_,                 &MainWindow::slotPrint },
    { &MainWindow::printPreviewAct_,          &MainWindow::slotPrintPreview },
    { &MainWindow::savePageAsAct_,            &MainWindow::slotSavePageAs },
    { &MainWindow::updateFeedAct_,            &MainWindow::slotGetFeed },
    { &MainWindow::updateAllFeedsAct_,        &MainWindow::slotGetAllFeeds },
    { &MainWindow::stopUpdateAct_,            &MainWindow::slotStopUpdate },
    { &MainWindow::markAllFeedsRead_,         &MainWindow::signalMarkAllFeedsRead },
    { &MainWindow::indentationFeedsTreeAct_,  &MainWindow::slotIndentationFeedsTree },
    { &MainWindow::sortedByTitleFeedsTreeAct_,&MainWindow::sortedByTitleFeedsTree },
    { &MainWindow::showCleanUpWizardAct_,     &MainWindow::cleanUp },
    { &MainWindow::bulkFeedSettingsAct_,      &MainWindow::showBulkFeedSettings },
    { &MainWindow::setFilterNewsAct_,         &MainWindow::showFilterRulesDlg },
    { &MainWindow::aboutAct_,                 &MainWindow::slotShowAboutDlg },
    { &MainWindow::updateAppAct_,             &MainWindow::slotShowUpdateAppDlg },
    { &MainWindow::reportProblemAct_,         &MainWindow::slotReportProblem },
    { &MainWindow::restoreLastNewsAct_,       &MainWindow::restoreLastNews },
    { &MainWindow::markFeedRead_,             &MainWindow::markFeedRead },
    { &MainWindow::feedProperties_,           &MainWindow::showFeedPropertiesDlg },
    { &MainWindow::feedKeyUpAct_,             &MainWindow::slotFeedPrevious },
    { &MainWindow::feedKeyDownAct_,           &MainWindow::slotFeedNext },
    { &MainWindow::switchFocusAct_,           &MainWindow::slotSwitchFocus },
    { &MainWindow::switchFocusPrevAct_,       &MainWindow::slotSwitchPrevFocus },
    { &MainWindow::feedsWidgetVisibleAct_,    &MainWindow::slotVisibledFeedsWidget },
    { &MainWindow::openDescriptionNewsAct_,   &MainWindow::slotOpenNewsWebView },
    { &MainWindow::findTextAct_,              &MainWindow::findText },
    { &MainWindow::placeToTrayAct_,           &MainWindow::slotPlaceToTray },
    { &MainWindow::findFeedAct_,              &MainWindow::findFeedVisible },
    { &MainWindow::fullScreenAct_,            &MainWindow::setFullScreen },
    { &MainWindow::stayOnTopAct_,             &MainWindow::setStayOnTop },
    { &MainWindow::newsLabelAction_,          &MainWindow::setDefaultLabelNews },
    { &MainWindow::showLabelsMenuAct_,        &MainWindow::slotShowLabelsMenu },
    { &MainWindow::reduceNewsListAct_,        &MainWindow::reduceNewsList },
    { &MainWindow::increaseNewsListAct_,      &MainWindow::increaseNewsList },
    { &MainWindow::nextUnreadNewsAct_,        &MainWindow::nextUnreadNews },
    { &MainWindow::prevUnreadNewsAct_,        &MainWindow::prevUnreadNews },
    { &MainWindow::openHomeFeedAct_,          &MainWindow::slotOpenHomeFeed },
    { &MainWindow::copyLinkAct_,              &MainWindow::slotCopyLinkNews },
    { &MainWindow::pageUpWebViewAct_,         &MainWindow::slotPageUpWebView },
    { &MainWindow::pageDownWebViewAct_,       &MainWindow::slotPageDownWebView },
    { &MainWindow::nextFolderAct_,            &MainWindow::slotNextFolder },
    { &MainWindow::prevFolderAct_,            &MainWindow::slotPrevFolder },
    { &MainWindow::expandFolderAct_,          &MainWindow::slotExpandFolder },
    { &MainWindow::settingPageLabelsAct_,     &MainWindow::showSettingPageLabels },
    { &MainWindow::markNewsRead_,             &MainWindow::markNewsRead },
    { &MainWindow::markAllNewsRead_,          &MainWindow::markAllNewsRead },
    { &MainWindow::markStarAct_,              &MainWindow::markNewsStar },
    { &MainWindow::deleteNewsAct_,            &MainWindow::deleteNews },
    { &MainWindow::deleteAllNewsAct_,         &MainWindow::deleteAllNewsList },
    { &MainWindow::restoreNewsAct_,           &MainWindow::restoreNews },
    { &MainWindow::newsKeyUpAct_,             &MainWindow::slotNewsUpPressed },
    { &MainWindow::newsKeyDownAct_,           &MainWindow::slotNewsDownPressed },
    { &MainWindow::newsKeyPageUpAct_,         &MainWindow::slotNewsPageUpPressed },
    { &MainWindow::newsKeyPageDownAct_,       &MainWindow::slotNewsPageDownPressed },
    { &MainWindow::openInExternalBrowserAct_, &MainWindow::openInExternalBrowserNews },
  };
  for (const auto &b : kSlotBindings)
    connect(this->*(b.action), &QAction::triggered, this, b.slot);

  connect(feedsToolbarToggle_, &QAction::toggled, feedsPanel_, &QWidget::setVisible);
  connect(categoriesPanelToggle_, &QAction::toggled, categoriesWidget_, &QWidget::setVisible);
  connect(statusBarToggle_, &QAction::toggled, statusBar(), &QWidget::setVisible);
  connect(collapseAllFoldersAct_, &QAction::triggered, feedsView_, &FeedsView::collapseAll);
  connect(expandAllFoldersAct_, &QAction::triggered, feedsView_, &FeedsView::expandAll);
  connect(showDownloadManagerAct_, &QAction::triggered, this, [this] { showDownloadManager(); });
  connect(setNewsFiltersAct_, &QAction::triggered, this, [this] { showNewsFiltersDlg(); });
  connect(websiteCookiesAct_, &QAction::triggered, this, [this]() {
    CookiesDialog dialog(this);
    dialog.exec();
  });
  connect(optionsAct_, &QAction::triggered, this, [this] { showOptionDlg(); });
  connect(pushButtonNull_, &QAbstractButton::clicked, feedsWidgetVisibleAct_, &QAction::trigger);
  connect(newsLabelGroup_, &QActionGroup::triggered, this, &MainWindow::setLabelNews);
  connect(shareGroup_, &QActionGroup::triggered, this, &MainWindow::slotShareNews);
}
// ---------------------------------------------------------------------------
void MainWindow::createShortcut()
{
  for (const auto &spec : UiBindings::kShortcuts) {
    QAction *action = this->*(spec.member);
    if (spec.defaultKey == -1)
      action->setShortcut(QKeySequence(QKeySequence::New));
    else if (spec.defaultKey != 0)
      action->setShortcut(QKeySequence(spec.defaultKey));
    shortcutRegistry_.append(action);
  }

  shortcutRegistry_.append(shareGroup_->actions());

  // Actions for labels do add at the end
  shortcutRegistry_.append(settingPageLabelsAct_);
  shortcutRegistry_.append(showLabelsMenuAct_);
  shortcutRegistry_.append(newsLabelGroup_->actions());

  shortcutRegistry_.load();
}
// ---------------------------------------------------------------------------
void MainWindow::createMenu()
{
  newMenu_ = new QMenu(this);
  newMenu_->addAction(addFeedAct_);
  newMenu_->addAction(addFolderAct_);
  addAct_->setMenu(newMenu_);

  fileMenu_ = new QMenu(this);
  fileMenu_->addAction(addAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(importFeedsAct_);
  fileMenu_->addAction(exportFeedsAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(createBackupAct_);
  fileMenu_->addSeparator();
#ifndef Q_OS_MAC
  fileMenu_->addAction(showMenuBarAct_);
  fileMenu_->addSeparator();
#endif
  fileMenu_->addAction(exitAct_);

  toolbarsMenu_ = new QMenu(this);
  toolbarsMenu_->addAction(feedsWidgetVisibleAct_);
  toolbarsMenu_->addAction(mainToolbarToggle_);
  toolbarsMenu_->addAction(feedsToolbarToggle_);
  toolbarsMenu_->addAction(newsToolbarToggle_);
  toolbarsMenu_->addAction(categoriesPanelToggle_);
  toolbarsMenu_->addAction(statusBarToggle_);

  customizeToolbarGroup_ = new QActionGroup(this);
  customizeToolbarGroup_->addAction(customizeMainToolbarAct_);
  customizeToolbarGroup_->addAction(customizeFeedsToolbarAct_);
  customizeToolbarGroup_->addAction(customizeNewsToolbarAct_);
  customizeToolbarMenu_ = new QMenu(this);
  customizeToolbarMenu_->addActions(customizeToolbarGroup_->actions());

  layoutGroup_ = new QActionGroup(this);
  layoutGroup_->addAction(classicLayoutAct_);
  layoutGroup_->addAction(newspaperLayoutAct_);
  layoutMenu_ = new QMenu(this);
  layoutMenu_->addActions(layoutGroup_->actions());
  layoutToggle_->setMenu(layoutMenu_);

  styleGroup_ = new QActionGroup(this);
  styleMenu_ = new QMenu(this);
  rebuildStyleMenu();
  connect(styleMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildStyleMenu);

  browserPositionGroup_ = new QActionGroup(this);
  browserPositionGroup_->addAction(topBrowserPositionAct_);
  browserPositionGroup_->addAction(bottomBrowserPositionAct_);
  browserPositionGroup_->addAction(rightBrowserPositionAct_);
  browserPositionGroup_->addAction(leftBrowserPositionAct_);

  browserPositionMenu_ = new QMenu(this);
  browserPositionMenu_->addActions(browserPositionGroup_->actions());

  viewMenu_  = new QMenu(this);
  viewMenu_->addMenu(toolbarsMenu_);
  viewMenu_->addMenu(customizeToolbarMenu_);
  viewMenu_->addSeparator();
  viewMenu_->addMenu(layoutMenu_);
  viewMenu_->addMenu(browserPositionMenu_);
  viewMenu_->addMenu(styleMenu_);
  viewMenu_->addSeparator();
  viewMenu_->addAction(stayOnTopAct_);
#ifdef HAVE_X11
  if (allWorkspacesAct_)
    viewMenu_->addAction(allWorkspacesAct_);
#endif
  viewMenu_->addAction(fullScreenAct_);

  feedsFilterGroup_ = new QActionGroup(this);
  feedsFilterGroup_->setExclusive(true);
  feedsFilterGroup_->addAction(filterFeedsAll_);
  feedsFilterGroup_->addAction(filterFeedsNew_);
  feedsFilterGroup_->addAction(filterFeedsUnread_);
  feedsFilterGroup_->addAction(filterFeedsStarred_);
  feedsFilterGroup_->addAction(filterFeedsError_);

  feedsFilterMenu_ = new QMenu(this);
  feedsFilterMenu_->addActions(feedsFilterGroup_->actions());
  feedsFilterMenu_->insertSeparator(filterFeedsNew_);

  feedsFilter_->setMenu(feedsFilterMenu_);

  feedsColumnsGroup_ = new QActionGroup(this);
  feedsColumnsGroup_->setExclusive(false);
  feedsColumnsGroup_->addAction(showUnreadCount_);
  feedsColumnsGroup_->addAction(showUndeleteCount_);
  feedsColumnsGroup_->addAction(showLastUpdated_);

  feedsColumnsMenu_ = new QMenu(this);
  feedsColumnsMenu_->addActions(feedsColumnsGroup_->actions());

  feedMenu_ = new QMenu(this);
  feedMenu_->addAction(updateFeedAct_);
  feedMenu_->addAction(updateAllFeedsAct_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(markFeedRead_);
  feedMenu_->addAction(markAllFeedsRead_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(feedsFilter_);
  feedMenu_->addMenu(feedsColumnsMenu_);
  feedMenu_->addAction(sortedByTitleFeedsTreeAct_);
  feedMenu_->addAction(indentationFeedsTreeAct_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(deleteFeedAct_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(feedProperties_);
  feedMenu_->addSeparator();

  newsLabelMenu_ = new QMenu(this);
  newsLabelMenu_->addActions(newsLabelGroup_->actions());
  newsLabelMenuAction_ = new QAction(this);
  newsLabelMenuAction_->setIcon(QIcon(":/images/label_3"));
  newsLabelAction_->setMenu(newsLabelMenu_);
  newsLabelMenuAction_->setMenu(newsLabelMenu_);

  shareMenu_ = new QMenu(this);
  shareMenu_->addActions(shareGroup_->actions());
  shareMenuAct_ = new QAction(this);
  shareMenuAct_->setObjectName("shareMenuAct");
  shareMenuAct_->setIcon(QIcon(":/images/images/share.png"));
  shareMenuAct_->setMenu(shareMenu_);
  this->addAction(shareMenuAct_);

  newsFilterGroup_ = new QActionGroup(this);
  newsFilterGroup_->setExclusive(true);
  newsFilterGroup_->addAction(filterNewsAll_);
  newsFilterGroup_->addAction(filterNewsNew_);
  newsFilterGroup_->addAction(filterNewsUnread_);
  newsFilterGroup_->addAction(filterNewsStar_);
  newsFilterGroup_->addAction(filterNewsNotStarred_);
  newsFilterGroup_->addAction(filterNewsUnreadStar_);
  newsFilterGroup_->addAction(filterNewsLastDay_);
  newsFilterGroup_->addAction(filterNewsLastWeek_);

  newsFilterMenu_ = new QMenu(this);
  newsFilterMenu_->addActions(newsFilterGroup_->actions());
  newsFilterMenu_->insertSeparator(filterNewsNew_);
  newsFilterMenu_->insertSeparator(filterNewsLastDay_);

  newsFilter_->setMenu(newsFilterMenu_);

  newsSortByMenu_ = new QMenu(this);
  newsSortByMenu_->addSeparator();
  newsSortByMenu_->addActions(newsSortOrderGroup_->actions());

  newsMenu_ = new QMenu(this);
  newsMenu_->addAction(markNewsRead_);
  newsMenu_->addAction(markAllNewsRead_);
  newsMenu_->addSeparator();
  newsMenu_->addAction(markStarAct_);
  newsMenu_->addAction(newsLabelMenuAction_);
  newsMenu_->addAction(shareMenuAct_);
  newsMenu_->addSeparator();
  newsMenu_->addAction(newsFilter_);
  newsMenu_->addMenu(newsSortByMenu_);
  newsMenu_->addSeparator();
  newsMenu_->addAction(deleteNewsAct_);
  newsMenu_->addAction(deleteAllNewsAct_);

  browserZoomGroup_ = new QActionGroup(this);
  browserZoomGroup_->addAction(zoomInAct_);
  browserZoomGroup_->addAction(zoomOutAct_);
  browserZoomGroup_->addAction(zoomTo100Act_);

  browserZoomMenu_ = new QMenu(this);
  browserZoomMenu_->setIcon(QIcon(":/images/zoom"));
  browserZoomMenu_->addActions(browserZoomGroup_->actions());
  browserZoomMenu_->insertSeparator(zoomTo100Act_);

  browserMenu_ = new QMenu(this);
  browserMenu_->addAction(autoLoadImagesToggle_);
  browserMenu_->addMenu(browserZoomMenu_);
  browserMenu_->addSeparator();
  browserMenu_->addAction(printAct_);
  browserMenu_->addAction(printPreviewAct_);
  browserMenu_->addSeparator();
  browserMenu_->addAction(savePageAsAct_);
  browserMenu_->addSeparator();

  toolsMenu_ = new QMenu(this);
  toolsMenu_->addAction(showDownloadManagerAct_);
  toolsMenu_->addSeparator();
  toolsMenu_->addAction(showCleanUpWizardAct_);
  toolsMenu_->addAction(setNewsFiltersAct_);
  toolsMenu_->addAction(bulkFeedSettingsAct_);
  toolsMenu_->addAction(websiteCookiesAct_);
  toolsMenu_->addSeparator();
  toolsMenu_->addAction(optionsAct_);

  helpMenu_ = new QMenu(this);
  helpMenu_->addAction(updateAppAct_);
  helpMenu_->addSeparator();
  helpMenu_->addAction(reportProblemAct_);
  helpMenu_->addAction(aboutAct_);

#ifndef Q_OS_MAC
  mainMenu_ = new QMenu(this);
  mainMenu_->addAction(addAct_);
  mainMenu_->addSeparator();
  mainMenu_->addAction(importFeedsAct_);
  mainMenu_->addAction(exportFeedsAct_);
  mainMenu_->addSeparator();
  mainMenu_->addAction(createBackupAct_);
  mainMenu_->addSeparator();
  mainMenu_->addMenu(viewMenu_);
  mainMenu_->addMenu(feedMenu_);
  mainMenu_->addMenu(newsMenu_);
  mainMenu_->addMenu(browserMenu_);
  mainMenu_->addSeparator();
  mainMenu_->addMenu(toolsMenu_);
  mainMenu_->addSeparator();
  mainMenu_->addMenu(helpMenu_);
  mainMenu_->addSeparator();
  mainMenu_->addAction(showMenuBarAct_);
  mainMenu_->addSeparator();
  mainMenu_->addAction(exitAct_);
#endif

  menuBar()->addMenu(fileMenu_);
  menuBar()->addMenu(viewMenu_);
  menuBar()->addMenu(feedMenu_);
  menuBar()->addMenu(newsMenu_);
  menuBar()->addMenu(browserMenu_);
  menuBar()->addMenu(toolsMenu_);
  menuBar()->addMenu(helpMenu_);

  connect(customizeToolbarGroup_, &QActionGroup::triggered, this, &MainWindow::showCustomizeToolbarDlg);
  connect(styleGroup_, &QActionGroup::triggered, this, &MainWindow::setStyleApp);
  connect(browserPositionGroup_, &QActionGroup::triggered, this, &MainWindow::setBrowserPosition);
  connect(feedsFilterGroup_, &QActionGroup::triggered, this, [this] { setFeedsFilter(); });
  connect(feedsFilter_, &QAction::triggered, this, &MainWindow::slotFeedsFilter);
  connect(feedsColumnsGroup_, &QActionGroup::triggered, this, &MainWindow::feedsColumnVisible);
  connect(feedMenu_, &QMenu::aboutToShow, this, &MainWindow::slotFeedMenuShow);
  connect(newsLabelMenu_, &QMenu::aboutToShow, this, &MainWindow::getLabelNews);
  connect(shareMenuAct_, &QAction::triggered, this, &MainWindow::showMenuShareNews);
  connect(newsFilterGroup_, &QActionGroup::triggered,
          this, [this](QAction *action) {
    setNewsFilter(action);
  });
  connect(newsFilter_, &QAction::triggered, this, &MainWindow::slotNewsFilter);
  connect(newsSortByMenu_, &QMenu::aboutToShow, this, &MainWindow::showNewsSortByMenu);
  connect(newsMenu_, &QMenu::aboutToShow, this, &MainWindow::showNewsMenu);
  connect(browserZoomGroup_, &QActionGroup::triggered, this, &MainWindow::browserZoom);
}
// ---------------------------------------------------------------------------
void MainWindow::createToolBar()
{
  mainToolbar_ = new QToolBar(this);
  mainToolbar_->setObjectName("ToolBar_General");
  mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  mainToolbar_->setContextMenuPolicy(Qt::CustomContextMenu);
  addToolBar(mainToolbar_);

  connect(mainToolbarToggle_, &QAction::toggled, mainToolbar_, &QWidget::setVisible);
  connect(mainToolbar_, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenuToolBar);

  connect(toolBarLockAct_, &QAction::toggled, this, &MainWindow::lockMainToolbar);
  connect(toolBarHideAct_, &QAction::triggered, this, &MainWindow::hideMainToolbar);
}

void MainWindow::showMainMenu()
{
#ifndef Q_OS_MAC
  mainMenu_->popup(mainMenuButton_->mapToGlobal(QPoint(0, mainMenuButton_->height())));
#endif
}

// ----------------------------------------------------------------------------
void MainWindow::retranslateStrings()
{
  QString str = statusBarController_->unreadText();
  str = str.right(str.length() - str.indexOf(':') - 1).replace(" ", "");
  const QString unreadText = QString(" " + tr("Unread: %1") + " ").arg(str);
  str = statusBarController_->allText();
  str = str.right(str.length() - str.indexOf(':') - 1).replace(" ", "");
  statusBarController_->setCountTexts(
        unreadText, QString(" " + tr("All: %1") + " ").arg(str));

  str = trayIconController_->toolTip();
  QString info =
      QGuiApplication::applicationDisplayName() + "\n" +
      QString(tr("New Articles: %1")).arg(str.section(": ", 1).section("\n", 0, 0)) +
      QString("\n") +
      QString(tr("Unread Articles: %1")).arg(str.section(": ", 2));
  trayIconController_->setToolTip(info);

  mainMenuButton_->setToolTip(tr("Menu"));
#ifdef HAVE_X11
  if (allWorkspacesAct_)
    allWorkspacesAct_->setText(tr("All Workspaces"));
#endif

  for (const auto &spec : UiBindings::kActions) {
    QAction *action = this->*(spec.member);
    if (spec.text)
      action->setText(tr(spec.text));
    if (spec.toolTip)
      action->setToolTip(tr(spec.toolTip));
  }

  for (const auto &spec : UiBindings::kMenuTitles)
    (this->*(spec.menu))->setTitle(tr(spec.title));

  updateFeedActionText();
  mainToolbar_->setWindowTitle(tr("Main Toolbar"));
  rebuildStyleMenu();
  showWindowAct_->setText(tr("Show Window"));

  categoriesLabel_->setText(tr("Categories"));
  if (categoriesTree_->isHidden())
    showCategoriesButton_->setToolTip(tr("Show Categories"));
  else
    showCategoriesButton_->setToolTip(tr("Hide Categories"));

  newsLabelMenuAction_->setText(tr("Label"));

  categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->setText(0, tr("Unread"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::StarredItem)->setText(0, tr("Starred"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::DeletedItem)->setText(0, tr("Deleted"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->setText(0, tr("Labels"));

  shareMenuAct_->setText(tr("Share"));

  newsSortOrderGroup_->actions().at(0)->setText(tr("Ascending"));
  newsSortOrderGroup_->actions().at(1)->setText(tr("Descending"));

  QApplication::translate("QDialogButtonBox", "Close");
  QApplication::translate("QDialogButtonBox", "Cancel");
  QApplication::translate("QDialogButtonBox", "&Yes");
  QApplication::translate("QDialogButtonBox", "&No");

  QApplication::translate("QLineEdit", "&Undo");
  QApplication::translate("QLineEdit", "&Redo");
  QApplication::translate("QLineEdit", "Cu&t");
  QApplication::translate("QLineEdit", "&Copy");
  QApplication::translate("QLineEdit", "&Paste");
  QApplication::translate("QLineEdit", "Delete");
  QApplication::translate("QLineEdit", "Select All");

  QApplication::translate("QTextControl", "&Undo");
  QApplication::translate("QTextControl", "&Redo");
  QApplication::translate("QTextControl", "Cu&t");
  QApplication::translate("QTextControl", "&Copy");
  QApplication::translate("QTextControl", "&Paste");
  QApplication::translate("QTextControl", "Delete");
  QApplication::translate("QTextControl", "Select All");
  QApplication::translate("QTextControl", "Copy &Link Location");

  QApplication::translate("QAbstractSpinBox", "&Step up");
  QApplication::translate("QAbstractSpinBox", "Step &down");
  QApplication::translate("QAbstractSpinBox", "&Select All");

  QApplication::translate("QMultiInputContext", "Select IM");

  QApplication::translate("QWizard", "Cancel");
  QApplication::translate("QWizard", "< &Back");
  QApplication::translate("QWizard", "&Finish");
  QApplication::translate("QWizard", "&Next >");

  QSqlQuery q;
  q.exec("SELECT id, name FROM labels WHERE id <= 6");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    QList<QTreeWidgetItem *> treeItems;
    treeItems = categoriesTree_->findItems(QString::number(idLabel),
                                           Qt::MatchFixedString|Qt::MatchRecursive,
                                           2);
    if (treeItems.count() && (nameLabels().at(idLabel-1) == nameLabel)) {
      treeItems.at(0)->setText(0, trNameLabels().at(idLabel-1));
      for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
        if (newsLabelGroup_->actions().at(i)->data().toInt() == idLabel) {
          newsLabelGroup_->actions().at(i)->setText(trNameLabels().at(idLabel-1));
          newsLabelGroup_->actions().at(i)->setToolTip(trNameLabels().at(idLabel-1));
          break;
        }
      }
      for (int i = 0; i < stackedWidget_->count(); i++) {
        NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
        if (widget->type_ == NewsTabWidget::TabTypeLabel) {
          if (widget->labelId_ == idLabel) {
            widget->setTextTab(trNameLabels().at(idLabel-1));
          }
        }
      }
    }
  }

  if (newsView_) {
    currentNewsTab->retranslateStrings();
  }
  findFeeds_->retranslateStrings();
  mainApp->downloadManager()->retranslateStrings();

  if ((mainApp->language() == "ar") || (mainApp->language() == "fa")) {
    QApplication::setLayoutDirection(Qt::RightToLeft);
    mainMenuButton_->setStyleSheet("#mainMenuButton { border: none; padding: 0px 5px 0px 0px; }");
  } else {
    QApplication::setLayoutDirection(Qt::LeftToRight);
    mainMenuButton_->setStyleSheet("#mainMenuButton { border: none; padding: 0px 0px 0px 5px; }");
  }

  /** Hack ltr <-> rtl**/
  int indexTab = tabBar_->addTab("");
  tabBar_->removeTab(indexTab);
}
// ----------------------------------------------------------------------------
void MainWindow::setToolBarStyle(const QString &styleStr)
{
  if (mainToolbar_->widgetForAction(addAct_))
    mainToolbar_->widgetForAction(addAct_)->setMinimumWidth(10);
  if (styleStr == "toolBarStyleI_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  } else if (styleStr == "toolBarStyleT_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  } else if (styleStr == "toolBarStyleTbI_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  } else {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    if (mainToolbar_->widgetForAction(addAct_))
      mainToolbar_->widgetForAction(addAct_)->setMinimumWidth(60);
  }
}
// ----------------------------------------------------------------------------
void MainWindow::setToolBarIconSize(QToolBar *toolbar, const QString &iconSizeStr)
{
  if (iconSizeStr == "toolBarIconBig_") {
    toolbar->setIconSize(QSize(32, 32));
  } else if (iconSizeStr == "toolBarIconSmall_") {
    toolbar->setIconSize(QSize(18, 18));
  } else {
    toolbar->setIconSize(QSize(24, 24));
  }
}

/** @brief Call toolbar context menu
 *----------------------------------------------------------------------------*/
void MainWindow::showContextMenuToolBar(const QPoint &pos)
{
  QMenu menu;
  menu.addAction(customizeMainToolbarAct2_);
  menu.addSeparator();
  menu.addAction(toolBarLockAct_);
  menu.addAction(toolBarHideAct_);

  menu.exec(mainToolbar_->mapToGlobal(pos));
}
// ----------------------------------------------------------------------------
void MainWindow::lockMainToolbar(bool lock)
{
  mainToolbar_->setMovable(!lock);
}
// ----------------------------------------------------------------------------
void MainWindow::hideMainToolbar()
{
  mainToolbarToggle_->setChecked(false);
  mainToolbar_->hide();
}
// ----------------------------------------------------------------------------
void MainWindow::customizeMainToolbar()
{
  showCustomizeToolbarDlg(customizeMainToolbarAct2_);
}
// ----------------------------------------------------------------------------
void MainWindow::showCustomizeToolbarDlg(QAction *action)
{
  QToolBar *toolbar = mainToolbar_;
  if (action->objectName() == "customizeFeedsToolbarAct") {
    toolbar = feedsToolBar_;
  } else if (action->objectName() == "customizeNewsToolbarAct") {
    if (currentNewsTab->type_ == NewsTabWidget::TabTypeDownloads) return;
    toolbar = currentNewsTab->newsToolBar_;
  }

  CustomizeToolbarDialog *toolbarDlg = new CustomizeToolbarDialog(this, toolbar);

  toolbarDlg->exec();

  delete toolbarDlg;
}

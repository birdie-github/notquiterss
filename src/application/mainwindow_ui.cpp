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

/** @brief Create actions for main menu and toolbar
 *---------------------------------------------------------------------------*/
void MainWindow::createActions()
{
  addAct_ = new QAction(this);
  addAct_->setObjectName("newAct");
  addAct_->setIcon(QIcon(":/images/add"));
  this->addAction(addAct_);
  connect(addAct_, &QAction::triggered, this, &MainWindow::addFeed);

  addFeedAct_ = new QAction(this);
  addFeedAct_->setObjectName("addFeedAct");
  addFeedAct_->setIcon(QIcon(":/images/feed"));
  this->addAction(addFeedAct_);
  connect(addFeedAct_, &QAction::triggered, this, &MainWindow::addFeed);

  addFeedTrayAct_ = new QAction(this);
  addFeedTrayAct_->setIcon(QIcon(":/images/feed"));
  connect(addFeedTrayAct_, &QAction::triggered, this, &MainWindow::addFeed);

  addFolderAct_ = new QAction(this);
  addFolderAct_->setObjectName("addFolderAct");
  addFolderAct_->setIcon(QIcon(":/images/folder"));
  this->addAction(addFolderAct_);
  connect(addFolderAct_, &QAction::triggered, this, &MainWindow::addFolder);

  openFeedNewTabAct_ = new QAction(this);
  openFeedNewTabAct_->setObjectName("openNewTabAct");
  this->addAction(openFeedNewTabAct_);
  connect(openFeedNewTabAct_, &QAction::triggered, this, &MainWindow::slotOpenFeedNewTab);

  deleteFeedAct_ = new QAction(this);
  deleteFeedAct_->setObjectName("deleteFeedAct");
  deleteFeedAct_->setIcon(QIcon(":/images/delete"));
  this->addAction(deleteFeedAct_);
  connect(deleteFeedAct_, &QAction::triggered, this, &MainWindow::deleteItemFeedsTree);

  importFeedsAct_ = new QAction(this);
  importFeedsAct_->setObjectName("importFeedsAct");
  importFeedsAct_->setIcon(QIcon(":/images/importFeeds"));
  this->addAction(importFeedsAct_);
  connect(importFeedsAct_, &QAction::triggered, this, &MainWindow::slotImportFeeds);

  exportFeedsAct_ = new QAction(this);
  exportFeedsAct_->setObjectName("exportFeedsAct");
  exportFeedsAct_->setIcon(QIcon(":/images/exportFeeds"));
  this->addAction(exportFeedsAct_);
  connect(exportFeedsAct_, &QAction::triggered, this, &MainWindow::slotExportFeeds);

  createBackupAct_ = new QAction(this);
  createBackupAct_->setObjectName("createBackupAct");
  createBackupAct_->setIcon(QIcon(":/images/backup"));
  this->addAction(createBackupAct_);
  connect(createBackupAct_, &QAction::triggered, this, &MainWindow::createBackup);

  showMenuBarAct_ = new QAction(this);
  showMenuBarAct_->setCheckable(true);
  connect(showMenuBarAct_, &QAction::triggered, this, &MainWindow::showMenuBar);

  exitAct_ = new QAction(this);
  exitAct_->setObjectName("exitAct");
  this->addAction(exitAct_);
  connect(exitAct_, &QAction::triggered, this, &MainWindow::quitApp);

  mainToolbarToggle_ = new QAction(this);
  mainToolbarToggle_->setCheckable(true);
  feedsToolbarToggle_ = new QAction(this);
  feedsToolbarToggle_->setCheckable(true);
  newsToolbarToggle_ = new QAction(this);
  newsToolbarToggle_->setCheckable(true);
  categoriesPanelToggle_ = new QAction(this);
  categoriesPanelToggle_->setCheckable(true);
  statusBarToggle_ = new QAction(this);
  statusBarToggle_->setCheckable(true);

  connect(feedsToolbarToggle_, &QAction::toggled, feedsPanel_, &QWidget::setVisible);
  connect(categoriesPanelToggle_, &QAction::toggled, categoriesWidget_, &QWidget::setVisible);
  connect(statusBarToggle_, &QAction::toggled, statusBar(), &QWidget::setVisible);

  customizeMainToolbarAct_ = new QAction(this);
  customizeMainToolbarAct_->setObjectName("customizeMainToolbarAct");
  customizeMainToolbarAct2_ = new QAction(this);
  connect(customizeMainToolbarAct2_, &QAction::triggered, this, &MainWindow::customizeMainToolbar);

  toolBarLockAct_ = new QAction(this);
  toolBarLockAct_->setCheckable(true);
  toolBarHideAct_ = new QAction(this);

  customizeFeedsToolbarAct_ = new QAction(this);
  customizeFeedsToolbarAct_->setObjectName("customizeFeedsToolbarAct");

  customizeNewsToolbarAct_ = new QAction(this);
  customizeNewsToolbarAct_->setObjectName("customizeNewsToolbarAct");

  classicLayoutAct_ = new QAction(this);
  classicLayoutAct_->setObjectName("classicLayoutAct_");
  classicLayoutAct_->setIcon(QIcon(":/images/layout_classic"));
  classicLayoutAct_->setCheckable(true);
  classicLayoutAct_->setData(0);
  newspaperLayoutAct_ = new QAction(this);
  newspaperLayoutAct_->setObjectName("newspaperLayoutAct_");
  newspaperLayoutAct_->setIcon(QIcon(":/images/layout_newspaper"));
  newspaperLayoutAct_->setCheckable(true);
  newspaperLayoutAct_->setData(1);
  layoutToggle_ = new QAction(this);
  layoutToggle_->setObjectName("layoutToggle");
  layoutToggle_->setIcon(QIcon(":/images/layout_classic"));
  this->addAction(layoutToggle_);
  connect(layoutToggle_, &QAction::triggered, this, QOverload<>::of(&MainWindow::setNewsLayout));

  topBrowserPositionAct_ = new QAction(this);
  topBrowserPositionAct_->setCheckable(true);
  topBrowserPositionAct_->setData(TOP_POSITION);
  bottomBrowserPositionAct_ = new QAction(this);
  bottomBrowserPositionAct_->setCheckable(true);
  bottomBrowserPositionAct_->setData(BOTTOM_POSITION);
  rightBrowserPositionAct_ = new QAction(this);
  rightBrowserPositionAct_->setCheckable(true);
  rightBrowserPositionAct_->setData(RIGHT_POSITION);
  leftBrowserPositionAct_ = new QAction(this);
  leftBrowserPositionAct_->setCheckable(true);
  leftBrowserPositionAct_->setData(LEFT_POSITION);

  autoLoadImagesToggle_ = new QAction(this);
  autoLoadImagesToggle_->setObjectName("autoLoadImagesToggle");
  autoLoadImagesToggle_->setIcon(QIcon(":/images/imagesOn"));
  this->addAction(autoLoadImagesToggle_);

  printAct_ = new QAction(this);
  printAct_->setObjectName("printAct");
  printAct_->setIcon(QIcon(":/images/printer"));
  this->addAction(printAct_);
  connect(printAct_, &QAction::triggered, this, &MainWindow::slotPrint);
  printPreviewAct_ = new QAction(this);
  printPreviewAct_->setObjectName("printPreviewAct");
  printPreviewAct_->setIcon(QIcon(":/images/printer"));
  this->addAction(printPreviewAct_);
  connect(printPreviewAct_, &QAction::triggered, this, &MainWindow::slotPrintPreview);

  savePageAsAct_ = new QAction(this);
  savePageAsAct_->setObjectName("savePageAsAct");
  savePageAsAct_->setIcon(QIcon(":/images/save_as"));
  this->addAction(savePageAsAct_);
  connect(savePageAsAct_, &QAction::triggered, this, &MainWindow::slotSavePageAs);

  zoomInAct_ = new QAction(this);
  zoomInAct_->setObjectName("zoomInAct");
  zoomInAct_->setIcon(QIcon(":/images/zoomIn"));
  this->addAction(zoomInAct_);
  zoomOutAct_ = new QAction(this);
  zoomOutAct_->setObjectName("zoomOutAct");
  zoomOutAct_->setIcon(QIcon(":/images/zoomOut"));
  this->addAction(zoomOutAct_);
  zoomTo100Act_ = new QAction(this);
  zoomTo100Act_->setObjectName("zoomTo100Act");
  this->addAction(zoomTo100Act_);

  updateFeedAct_ = new QAction(this);
  updateFeedAct_->setObjectName("updateFeedAct");
  updateFeedAct_->setIcon(QIcon(":/images/updateFeed"));
  this->addAction(updateFeedAct_);
  connect(updateFeedAct_, &QAction::triggered, this, &MainWindow::slotGetFeed);

  updateAllFeedsAct_ = new QAction(this);
  updateAllFeedsAct_->setObjectName("updateAllFeedsAct");
  updateAllFeedsAct_->setIcon(QIcon(":/images/updateAllFeeds"));
  this->addAction(updateAllFeedsAct_);
  connect(updateAllFeedsAct_, &QAction::triggered, this, &MainWindow::slotGetAllFeeds);

  stopUpdateAct_ = new QAction(this);
  stopUpdateAct_->setObjectName("stopUpdateAct");
  stopUpdateAct_->setIcon(QIcon(":/images/close"));
  this->addAction(stopUpdateAct_);
  connect(stopUpdateAct_, &QAction::triggered, this, &MainWindow::slotStopUpdate);

  markAllFeedsRead_ = new QAction(this);
  markAllFeedsRead_->setObjectName("markAllFeedRead");
  markAllFeedsRead_->setIcon(QIcon(":/images/markReadAll"));
  this->addAction(markAllFeedsRead_);
  connect(markAllFeedsRead_, &QAction::triggered, this, &MainWindow::signalMarkAllFeedsRead);

  indentationFeedsTreeAct_ = new QAction(this);
  indentationFeedsTreeAct_->setCheckable(true);
  connect(indentationFeedsTreeAct_, &QAction::triggered, this, &MainWindow::slotIndentationFeedsTree);

  sortedByTitleFeedsTreeAct_ = new QAction(this);
  connect(sortedByTitleFeedsTreeAct_, &QAction::triggered, this, &MainWindow::sortedByTitleFeedsTree);

  collapseAllFoldersAct_ = new QAction(this);
  collapseAllFoldersAct_->setObjectName("collapseAllFolderAct");
  collapseAllFoldersAct_->setIcon(QIcon(":/images/bulletMinus"));
  this->addAction(collapseAllFoldersAct_);
  connect(collapseAllFoldersAct_, &QAction::triggered, feedsView_, &FeedsView::collapseAll);

  expandAllFoldersAct_ = new QAction(this);
  expandAllFoldersAct_->setObjectName("expandAllFolderAct");
  expandAllFoldersAct_->setIcon(QIcon(":/images/bulletPlus"));
  this->addAction(expandAllFoldersAct_);
  connect(expandAllFoldersAct_, &QAction::triggered, feedsView_, &FeedsView::expandAll);

  markNewsRead_ = new QAction(this);
  markNewsRead_->setObjectName("markNewsRead");
  markNewsRead_->setIcon(QIcon(":/images/markRead"));
  this->addAction(markNewsRead_);

  markAllNewsRead_ = new QAction(this);
  markAllNewsRead_->setObjectName("markAllNewsRead");
  markAllNewsRead_->setIcon(QIcon(":/images/markReadAll"));
  this->addAction(markAllNewsRead_);

  showDownloadManagerAct_ = new QAction(this);
  showDownloadManagerAct_->setObjectName("showDownloadManagerAct");
  showDownloadManagerAct_->setIcon(QIcon(":/images/download"));
  this->addAction(showDownloadManagerAct_);
  connect(showDownloadManagerAct_, &QAction::triggered, this, [this] { showDownloadManager(); });

  showCleanUpWizardAct_ = new QAction(this);
  showCleanUpWizardAct_->setObjectName("showCleanUpWizardAct");
  showCleanUpWizardAct_->setIcon(QIcon(":/images/cleanup"));
  this->addAction(showCleanUpWizardAct_);
  connect(showCleanUpWizardAct_, &QAction::triggered, this, &MainWindow::cleanUp);

  setNewsFiltersAct_ = new QAction(this);
  setNewsFiltersAct_->setObjectName("setNewsFiltersAct");
  setNewsFiltersAct_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(setNewsFiltersAct_);
  connect(setNewsFiltersAct_, &QAction::triggered, this, [this] { showNewsFiltersDlg(); });
  bulkFeedSettingsAct_ = new QAction(this);
  bulkFeedSettingsAct_->setObjectName("bulkFeedSettingsAct");
  this->addAction(bulkFeedSettingsAct_);
  connect(bulkFeedSettingsAct_, &QAction::triggered, this, &MainWindow::showBulkFeedSettings);

  websiteCookiesAct_ = new QAction(this);
  websiteCookiesAct_->setObjectName("websiteCookiesAct");
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
  this->addAction(websiteCookiesAct_);
  connect(websiteCookiesAct_, &QAction::triggered, this, [this]() {
    CookiesDialog dialog(this);
    dialog.exec();
  });

  setFilterNewsAct_ = new QAction(this);
  setFilterNewsAct_->setObjectName("setFilterNewsAct");
  setFilterNewsAct_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(setFilterNewsAct_);
  connect(setFilterNewsAct_, &QAction::triggered, this, &MainWindow::showFilterRulesDlg);

  optionsAct_ = new QAction(this);
  optionsAct_->setObjectName("optionsAct");
  optionsAct_->setIcon(QIcon(":/images/options"));
  this->addAction(optionsAct_);
  connect(optionsAct_, &QAction::triggered, this, [this] { showOptionDlg(); });

  feedsFilter_ = new QAction(this);
  feedsFilter_->setObjectName("feedsFilter");
  feedsFilter_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(feedsFilter_);
  filterFeedsAll_ = new QAction(this);
  filterFeedsAll_->setObjectName("filterFeedsAll_");
  filterFeedsAll_->setCheckable(true);
  filterFeedsAll_->setChecked(true);
  filterFeedsNew_ = new QAction(this);
  filterFeedsNew_->setObjectName("filterFeedsNew_");
  filterFeedsNew_->setCheckable(true);
  filterFeedsUnread_ = new QAction(this);
  filterFeedsUnread_->setObjectName("filterFeedsUnread_");
  filterFeedsUnread_->setCheckable(true);
  filterFeedsStarred_ = new QAction(this);
  filterFeedsStarred_->setObjectName("filterFeedsStarred_");
  filterFeedsStarred_->setCheckable(true);
  filterFeedsError_ = new QAction(this);
  filterFeedsError_->setObjectName("filterFeedsError_");
  filterFeedsError_->setCheckable(true);

  newsFilter_ = new QAction(this);
  newsFilter_->setObjectName("newsFilter");
  newsFilter_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(newsFilter_);
  filterNewsAll_ = new QAction(this);
  filterNewsAll_->setObjectName("filterNewsAll_");
  filterNewsAll_->setCheckable(true);
  filterNewsAll_->setChecked(true);
  filterNewsNew_ = new QAction(this);
  filterNewsNew_->setObjectName("filterNewsNew_");
  filterNewsNew_->setCheckable(true);
  filterNewsUnread_ = new QAction(this);
  filterNewsUnread_->setObjectName("filterNewsUnread_");
  filterNewsUnread_->setCheckable(true);
  filterNewsStar_ = new QAction(this);
  filterNewsStar_->setObjectName("filterNewsStar_");
  filterNewsStar_->setCheckable(true);
  filterNewsNotStarred_ = new QAction(this);
  filterNewsNotStarred_->setObjectName("filterNewsNotStarred_");
  filterNewsNotStarred_->setCheckable(true);
  filterNewsUnreadStar_ = new QAction(this);
  filterNewsUnreadStar_->setObjectName("filterNewsUnreadStar_");
  filterNewsUnreadStar_->setCheckable(true);
  filterNewsLastDay_ = new QAction(this);
  filterNewsLastDay_->setObjectName("filterNewsLastDay_");
  filterNewsLastDay_->setCheckable(true);
  filterNewsLastWeek_ = new QAction(this);
  filterNewsLastWeek_->setObjectName("filterNewsLastWeek_");
  filterNewsLastWeek_->setCheckable(true);

  newsSortByColumnGroup_ = new QActionGroup(this);
  newsSortByColumnGroup_->setExclusive(true);
  connect(newsSortByColumnGroup_, &QActionGroup::triggered, this, &MainWindow::setNewsSortByColumn);

  newsSortOrderGroup_ = new QActionGroup(this);
  newsSortOrderGroup_->setExclusive(true);
  QStringList listAct;
  listAct << "sortOrderAsc" << "sortOrderDesc";
  for (const QString &actionStr : std::as_const(listAct)) {
    QAction *newsSortOrderAct = new QAction(this);
    newsSortOrderAct->setObjectName(actionStr);
    newsSortOrderAct->setCheckable(true);
    newsSortOrderGroup_->addAction(newsSortOrderAct);
  }
  connect(newsSortOrderGroup_, &QActionGroup::triggered, this, &MainWindow::setNewsSortByColumn);

  aboutAct_ = new QAction(this);
  aboutAct_->setObjectName("AboutAct_");
  connect(aboutAct_, &QAction::triggered, this, &MainWindow::slotShowAboutDlg);

  updateAppAct_ = new QAction(this);
  updateAppAct_->setObjectName("UpdateApp_");
  connect(updateAppAct_, &QAction::triggered, this, &MainWindow::slotShowUpdateAppDlg);

  reportProblemAct_ = new QAction(this);
  reportProblemAct_->setObjectName("reportProblemAct_");
  connect(reportProblemAct_, &QAction::triggered, this, &MainWindow::slotReportProblem);

  openInExternalBrowserAct_ = new QAction(this);
  openInExternalBrowserAct_->setObjectName("openInExternalBrowserAct");
  openInExternalBrowserAct_->setIcon(QIcon(":/images/openBrowser"));
  this->addAction(openInExternalBrowserAct_);

  markStarAct_ = new QAction(this);
  markStarAct_->setObjectName("markStarAct");
  markStarAct_->setIcon(QIcon(":/images/starOn"));
  this->addAction(markStarAct_);

  deleteNewsAct_ = new QAction(this);
  deleteNewsAct_->setObjectName("deleteNewsAct");
  deleteNewsAct_->setIcon(QIcon(":/images/delete"));
  this->addAction(deleteNewsAct_);
  deleteAllNewsAct_ = new QAction(this);
  deleteAllNewsAct_->setObjectName("deleteAllNewsAct");
  this->addAction(deleteAllNewsAct_);

  restoreNewsAct_ = new QAction(this);
  restoreNewsAct_->setObjectName("restoreNewsAct");
  restoreNewsAct_->setIcon(QIcon(":/images/images/arrow_turn_left.png"));

  restoreLastNewsAct_ = new QAction(this);
  restoreLastNewsAct_->setObjectName("restoreLastNewsAct");
  restoreLastNewsAct_->setIcon(QIcon(":/images/images/arrow_turn_left.png"));
  this->addAction(restoreLastNewsAct_);
  connect(restoreLastNewsAct_, &QAction::triggered, this, &MainWindow::restoreLastNews);

  markFeedRead_ = new QAction(this);
  markFeedRead_->setObjectName("markFeedRead");
  markFeedRead_->setIcon(QIcon(":/images/markRead"));
  this->addAction(markFeedRead_);
  connect(markFeedRead_, &QAction::triggered, this, &MainWindow::markFeedRead);

  feedProperties_ = new QAction(this);
  feedProperties_->setObjectName("feedProperties");
  feedProperties_->setIcon(QIcon(":/images/preferencesFeed"));
  this->addAction(feedProperties_);
  connect(feedProperties_, &QAction::triggered, this, &MainWindow::showFeedPropertiesDlg);

  feedKeyUpAct_ = new QAction(this);
  feedKeyUpAct_->setObjectName("feedKeyUp");
  connect(feedKeyUpAct_, &QAction::triggered, this, &MainWindow::slotFeedPrevious);
  this->addAction(feedKeyUpAct_);

  feedKeyDownAct_ = new QAction(this);
  feedKeyDownAct_->setObjectName("feedKeyDownAct");
  connect(feedKeyDownAct_, &QAction::triggered, this, &MainWindow::slotFeedNext);
  this->addAction(feedKeyDownAct_);

  newsKeyUpAct_ = new QAction(this);
  newsKeyUpAct_->setObjectName("newsKeyUpAct");
  this->addAction(newsKeyUpAct_);
  newsKeyDownAct_ = new QAction(this);
  newsKeyDownAct_->setObjectName("newsKeyDownAct");
  this->addAction(newsKeyDownAct_);
  newsKeyPageUpAct_ = new QAction(this);
#ifdef HAVE_X11
  allWorkspacesAct_ = nullptr;
  if (QGuiApplication::platformName() == QLatin1String("xcb")) {
    allWorkspacesAct_ = new QAction(this);
    allWorkspacesAct_->setObjectName("allWorkspacesAct");
    allWorkspacesAct_->setCheckable(true);
    connect(allWorkspacesAct_, &QAction::triggered, this, &MainWindow::setAllWorkspaces);
  }
#endif
  newsKeyPageUpAct_->setObjectName("newsKeyPageUpAct");
  this->addAction(newsKeyPageUpAct_);
  newsKeyPageDownAct_ = new QAction(this);
  newsKeyPageDownAct_->setObjectName("newsKeyPageDownAct");
  this->addAction(newsKeyPageDownAct_);

  switchFocusAct_ = new QAction(this);
  switchFocusAct_->setObjectName("switchFocusAct");
  connect(switchFocusAct_, &QAction::triggered, this, &MainWindow::slotSwitchFocus);
  this->addAction(switchFocusAct_);
  switchFocusPrevAct_ = new QAction(this);
  switchFocusPrevAct_->setObjectName("switchFocusPrevAct");
  connect(switchFocusPrevAct_, &QAction::triggered, this, &MainWindow::slotSwitchPrevFocus);
  this->addAction(switchFocusPrevAct_);

  feedsWidgetVisibleAct_ = new QAction(this);
  feedsWidgetVisibleAct_->setObjectName("visibleFeedsWidgetAct");
  feedsWidgetVisibleAct_->setCheckable(true);
  connect(feedsWidgetVisibleAct_, &QAction::triggered, this, &MainWindow::slotVisibledFeedsWidget);
  connect(pushButtonNull_, &QAbstractButton::clicked, feedsWidgetVisibleAct_, &QAction::trigger);
  this->addAction(feedsWidgetVisibleAct_);

  showUnreadCount_ = new QAction(this);
  showUnreadCount_->setData(feedsView_->columnIndex("unread"));
  showUnreadCount_->setCheckable(true);
  showUndeleteCount_ = new QAction(this);
  showUndeleteCount_->setData(feedsView_->columnIndex("undeleteCount"));
  showUndeleteCount_->setCheckable(true);
  showLastUpdated_ = new QAction(this);
  showLastUpdated_->setData(feedsView_->columnIndex("updated"));
  showLastUpdated_->setCheckable(true);

  openDescriptionNewsAct_ = new QAction(this);
  openDescriptionNewsAct_->setObjectName("openDescriptionNewsAct");
  connect(openDescriptionNewsAct_, &QAction::triggered, this, &MainWindow::slotOpenNewsWebView);
  this->addAction(openDescriptionNewsAct_);

  findTextAct_ = new QAction(this);
  findTextAct_->setObjectName("findTextAct");
  connect(findTextAct_, &QAction::triggered, this, &MainWindow::findText);
  this->addAction(findTextAct_);

  placeToTrayAct_ = new QAction(this);
  placeToTrayAct_->setObjectName("placeToTrayAct");
  connect(placeToTrayAct_, &QAction::triggered, this, &MainWindow::slotPlaceToTray);
  this->addAction(placeToTrayAct_);

  findFeedAct_ = new QAction(this);
  findFeedAct_->setObjectName("findFeedAct");
  findFeedAct_->setCheckable(true);
  findFeedAct_->setChecked(false);
  findFeedAct_->setIcon(QIcon(":/images/images/findFeed.png"));
  this->addAction(findFeedAct_);
  connect(findFeedAct_, &QAction::triggered, this, &MainWindow::findFeedVisible);

  fullScreenAct_ = new QAction(this);
  fullScreenAct_->setObjectName("fullScreenAct");
  fullScreenAct_->setIcon(QIcon(":/images/images/fullScreen.png"));
  this->addAction(fullScreenAct_);
  connect(fullScreenAct_, &QAction::triggered, this, &MainWindow::setFullScreen);

  stayOnTopAct_ = new QAction(this);
  stayOnTopAct_->setObjectName("stayOnTopAct");
  stayOnTopAct_->setCheckable(true);
  this->addAction(stayOnTopAct_);
  connect(stayOnTopAct_, &QAction::triggered, this, &MainWindow::setStayOnTop);

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

  newsLabelAction_ = new QAction(this);
  newsLabelAction_->setObjectName("newsLabelAction");
  this->addAction(newsLabelAction_);
  if (newsLabelGroup_->actions().count()) {
    newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
    newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
    newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
  }
  connect(newsLabelAction_, &QAction::triggered, this, &MainWindow::setDefaultLabelNews);
  connect(newsLabelGroup_, &QActionGroup::triggered, this, &MainWindow::setLabelNews);

  showLabelsMenuAct_ = new QAction(this);
  showLabelsMenuAct_->setObjectName("showLabelsMenuAct");
  this->addAction(showLabelsMenuAct_);
  connect(showLabelsMenuAct_, &QAction::triggered, this, &MainWindow::slotShowLabelsMenu);

  closeTabAct_ = new QAction(this);
  closeTabAct_->setObjectName("closeTabAct");
  this->addAction(closeTabAct_);

  closeOtherTabsAct_ = new QAction(this);
  closeOtherTabsAct_->setObjectName("closeOtherTabsAct");
  this->addAction(closeOtherTabsAct_);

  closeAllTabsAct_ = new QAction(this);
  closeAllTabsAct_->setObjectName("closeAllTabsAct");
  this->addAction(closeAllTabsAct_);

  nextTabAct_ = new QAction(this);
  nextTabAct_->setObjectName("nextTabAct");
  this->addAction(nextTabAct_);

  prevTabAct_ = new QAction(this);
  prevTabAct_->setObjectName("prevTabAct");
  this->addAction(prevTabAct_);

  reduceNewsListAct_ = new QAction(this);
  reduceNewsListAct_->setObjectName("reduceNewsListAct");
  this->addAction(reduceNewsListAct_);
  connect(reduceNewsListAct_, &QAction::triggered, this, &MainWindow::reduceNewsList);
  increaseNewsListAct_ = new QAction(this);
  increaseNewsListAct_->setObjectName("increaseNewsListAct");
  this->addAction(increaseNewsListAct_);
  connect(increaseNewsListAct_, &QAction::triggered, this, &MainWindow::increaseNewsList);

  nextUnreadNewsAct_ = new QAction(this);
  nextUnreadNewsAct_->setObjectName("nextUnreadNewsAct");
  nextUnreadNewsAct_->setIcon(QIcon(":/images/moveDown"));
  this->addAction(nextUnreadNewsAct_);
  connect(nextUnreadNewsAct_, &QAction::triggered, this, &MainWindow::nextUnreadNews);
  prevUnreadNewsAct_ = new QAction(this);
  prevUnreadNewsAct_->setObjectName("prevUnreadNewsAct");
  prevUnreadNewsAct_->setIcon(QIcon(":/images/moveUp"));
  this->addAction(prevUnreadNewsAct_);
  connect(prevUnreadNewsAct_, &QAction::triggered, this, &MainWindow::prevUnreadNews);

  openHomeFeedAct_ = new QAction(this);
  openHomeFeedAct_->setObjectName("openHomeFeedsAct");
  openHomeFeedAct_->setIcon(QIcon(":/images/homePage"));
  this->addAction(openHomeFeedAct_);
  connect(openHomeFeedAct_, &QAction::triggered, this, &MainWindow::slotOpenHomeFeed);

  copyLinkAct_ = new QAction(this);
  copyLinkAct_->setObjectName("copyLinkAct");
  copyLinkAct_->setIcon(QIcon(":/images/copy"));
  this->addAction(copyLinkAct_);
  connect(copyLinkAct_, &QAction::triggered, this, &MainWindow::slotCopyLinkNews);

  pageUpWebViewAct_ = new QAction(this);
  pageUpWebViewAct_->setObjectName("pageUpWebViewAct");
  this->addAction(pageUpWebViewAct_);
  connect(pageUpWebViewAct_, &QAction::triggered, this, &MainWindow::slotPageUpWebView);
  pageDownWebViewAct_ = new QAction(this);
  pageDownWebViewAct_->setObjectName("pageDownWebViewAct");
  this->addAction(pageDownWebViewAct_);
  connect(pageDownWebViewAct_, &QAction::triggered, this, &MainWindow::slotPageDownWebView);

  nextFolderAct_ = new QAction(this);
  nextFolderAct_->setObjectName("nextFolderAct");
  this->addAction(nextFolderAct_);
  connect(nextFolderAct_, &QAction::triggered, this, &MainWindow::slotNextFolder);
  prevFolderAct_ = new QAction(this);
  prevFolderAct_->setObjectName("prevFolderAct");
  this->addAction(prevFolderAct_);
  connect(prevFolderAct_, &QAction::triggered, this, &MainWindow::slotPrevFolder);
  expandFolderAct_ = new QAction(this);
  expandFolderAct_->setObjectName("expandFolderAct");
  this->addAction(expandFolderAct_);
  connect(expandFolderAct_, &QAction::triggered, this, &MainWindow::slotExpandFolder);

  settingPageLabelsAct_ = new QAction(this);
  settingPageLabelsAct_->setObjectName("settingPageLabelsAct");
  this->addAction(settingPageLabelsAct_);
  connect(settingPageLabelsAct_, &QAction::triggered, this, &MainWindow::showSettingPageLabels);

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
  connect(shareGroup_, &QActionGroup::triggered,
          this, &MainWindow::slotShareNews);

  connect(markNewsRead_, &QAction::triggered, this, &MainWindow::markNewsRead);
  connect(markAllNewsRead_, &QAction::triggered, this, &MainWindow::markAllNewsRead);
  connect(markStarAct_, &QAction::triggered, this, &MainWindow::markNewsStar);
  connect(deleteNewsAct_, &QAction::triggered, this, &MainWindow::deleteNews);
  connect(deleteAllNewsAct_, &QAction::triggered, this, &MainWindow::deleteAllNewsList);
  connect(restoreNewsAct_, &QAction::triggered, this, &MainWindow::restoreNews);

  connect(newsKeyUpAct_, &QAction::triggered, this, &MainWindow::slotNewsUpPressed);
  connect(newsKeyDownAct_, &QAction::triggered, this, &MainWindow::slotNewsDownPressed);
  connect(newsKeyPageUpAct_, &QAction::triggered, this, &MainWindow::slotNewsPageUpPressed);
  connect(newsKeyPageDownAct_, &QAction::triggered, this, &MainWindow::slotNewsPageDownPressed);

  connect(openInExternalBrowserAct_, &QAction::triggered, this, &MainWindow::openInExternalBrowserNews);
}
// ---------------------------------------------------------------------------
void MainWindow::createShortcut()
{
  addFeedAct_->setShortcut(QKeySequence(QKeySequence::New));
  shortcutRegistry_.append(addFeedAct_);
  addFolderAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  shortcutRegistry_.append(addFolderAct_);
  shortcutRegistry_.append(deleteFeedAct_);
  shortcutRegistry_.append(createBackupAct_);
  exitAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));  // standart on other OS
  shortcutRegistry_.append(exitAct_);
  updateFeedAct_->setShortcut(QKeySequence(Qt::Key_F5));
  shortcutRegistry_.append(updateFeedAct_);
  updateAllFeedsAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
  shortcutRegistry_.append(updateAllFeedsAct_);
  shortcutRegistry_.append(stopUpdateAct_);
  shortcutRegistry_.append(openHomeFeedAct_);
  shortcutRegistry_.append(showDownloadManagerAct_);
  shortcutRegistry_.append(showCleanUpWizardAct_);
  shortcutRegistry_.append(setNewsFiltersAct_);
  shortcutRegistry_.append(bulkFeedSettingsAct_);
  shortcutRegistry_.append(websiteCookiesAct_);
  shortcutRegistry_.append(setFilterNewsAct_);
  optionsAct_->setShortcut(QKeySequence(Qt::Key_F8));
  shortcutRegistry_.append(optionsAct_);
  deleteNewsAct_->setShortcut(QKeySequence(Qt::Key_Delete));
  shortcutRegistry_.append(deleteNewsAct_);
  deleteAllNewsAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Delete));
  shortcutRegistry_.append(deleteAllNewsAct_);
  feedProperties_->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_E));
  shortcutRegistry_.append(feedProperties_);
  feedKeyUpAct_->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Up));
  shortcutRegistry_.append(feedKeyUpAct_);
  feedKeyDownAct_->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Down));
  shortcutRegistry_.append(feedKeyDownAct_);
  newsKeyUpAct_->setShortcut(QKeySequence(Qt::Key_Left));
  shortcutRegistry_.append(newsKeyUpAct_);
  newsKeyDownAct_->setShortcut(QKeySequence(Qt::Key_Right));
  shortcutRegistry_.append(newsKeyDownAct_);
  shortcutRegistry_.append(newsKeyPageUpAct_);
  shortcutRegistry_.append(newsKeyPageDownAct_);

  shortcutRegistry_.append(nextUnreadNewsAct_);
  shortcutRegistry_.append(prevUnreadNewsAct_);

  shortcutRegistry_.append(nextFolderAct_);
  shortcutRegistry_.append(prevFolderAct_);

  shortcutRegistry_.append(importFeedsAct_);
  shortcutRegistry_.append(exportFeedsAct_);
  shortcutRegistry_.append(createBackupAct_);
  shortcutRegistry_.append(autoLoadImagesToggle_);
  shortcutRegistry_.append(markAllFeedsRead_);
  shortcutRegistry_.append(markFeedRead_);
  shortcutRegistry_.append(markNewsRead_);
  shortcutRegistry_.append(markAllNewsRead_);
  shortcutRegistry_.append(markStarAct_);
  shortcutRegistry_.append(collapseAllFoldersAct_);
  shortcutRegistry_.append(expandAllFoldersAct_);
  shortcutRegistry_.append(expandFolderAct_);

  shortcutRegistry_.append(openDescriptionNewsAct_);
  openDescriptionNewsAct_->setShortcut(QKeySequence(Qt::Key_Return));
  shortcutRegistry_.append(openInExternalBrowserAct_);
  openInExternalBrowserAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));

  switchFocusAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
  shortcutRegistry_.append(switchFocusAct_);
  switchFocusPrevAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
  shortcutRegistry_.append(switchFocusPrevAct_);

  feedsWidgetVisibleAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
  shortcutRegistry_.append(feedsWidgetVisibleAct_);

  shortcutRegistry_.append(placeToTrayAct_);

  zoomInAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
  shortcutRegistry_.append(zoomInAct_);
  zoomOutAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  shortcutRegistry_.append(zoomOutAct_);
  zoomTo100Act_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  shortcutRegistry_.append(zoomTo100Act_);

  printAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
  shortcutRegistry_.append(printAct_);
  printPreviewAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
  shortcutRegistry_.append(printPreviewAct_);

  savePageAsAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
  shortcutRegistry_.append(savePageAsAct_);

  fullScreenAct_->setShortcut(QKeySequence(Qt::Key_F11));
  shortcutRegistry_.append(fullScreenAct_);

  stayOnTopAct_->setShortcut(QKeySequence(Qt::Key_F10));
  shortcutRegistry_.append(stayOnTopAct_);

  shortcutRegistry_.append(layoutToggle_);

  closeTabAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
  shortcutRegistry_.append(closeTabAct_);
  shortcutRegistry_.append(closeOtherTabsAct_);
  shortcutRegistry_.append(closeAllTabsAct_);
  shortcutRegistry_.append(nextTabAct_);
  shortcutRegistry_.append(prevTabAct_);

  reduceNewsListAct_->setShortcut(QKeySequence(Qt::ALT| Qt::Key_Up));
  shortcutRegistry_.append(reduceNewsListAct_);
  increaseNewsListAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
  shortcutRegistry_.append(increaseNewsListAct_);

  restoreLastNewsAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
  shortcutRegistry_.append(restoreLastNewsAct_);

  findTextAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
  shortcutRegistry_.append(findTextAct_);

  shortcutRegistry_.append(findFeedAct_);

  shortcutRegistry_.append(copyLinkAct_);

  shortcutRegistry_.append(pageUpWebViewAct_);
  shortcutRegistry_.append(pageDownWebViewAct_);

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

  addAct_->setText(tr("&Add"));
  addAct_->setToolTip(tr("Add New Feed"));

  addFeedAct_->setText(tr("&Feed..."));
  addFeedAct_->setToolTip(tr("Add New Feed"));

  addFeedTrayAct_->setText(tr("Add Feed..."));

  addFolderAct_->setText(tr("F&older..."));
  addFolderAct_->setToolTip(tr("Add New Folder"));

  openFeedNewTabAct_->setText(tr("Open in New Tab"));

  deleteFeedAct_->setText(tr("&Delete..."));
  deleteFeedAct_->setToolTip(tr("Delete Selected Feed"));

  importFeedsAct_->setText(tr("&Import Feeds..."));
  importFeedsAct_->setToolTip(tr("Import Feeds from OPML File"));

  exportFeedsAct_->setText(tr("&Export Feeds..."));
  exportFeedsAct_->setToolTip(tr("Export Feeds to OPML File"));

  createBackupAct_->setText(tr("&Create Backup..."));
  showMenuBarAct_->setText(tr("S&how Menu Bar"));

  exitAct_->setText(tr("E&xit"));

  updateFeedActionText();

  updateAllFeedsAct_->setText(tr("Update All"));
  updateAllFeedsAct_->setToolTip(tr("Update All Feeds"));

  stopUpdateAct_->setText(tr("Stop Updating Feeds"));
  stopUpdateAct_->setToolTip(tr("Stop Updating Feeds"));

  markAllFeedsRead_->setText(tr("Mark All Feeds Read"));

  markNewsRead_->setText(tr("Mark Article Read/Unread"));
  markNewsRead_->setToolTip(tr("Mark Current Article Read/Unread"));

  markAllNewsRead_->setText(tr("Mark All Articles Read"));
  markAllNewsRead_->setToolTip(tr("Mark All Articles Read"));

  showDownloadManagerAct_->setText(tr("Downloads"));

  showCleanUpWizardAct_->setText(tr("Clean Up..."));

  setNewsFiltersAct_->setText(tr("Article Filters..."));
  bulkFeedSettingsAct_->setText(tr("Bulk configure feeds..."));
  websiteCookiesAct_->setText(tr("Website cookies…"));
  setFilterNewsAct_->setText(tr("Filter Articles..."));

  optionsAct_->setText(tr("Options..."));
  optionsAct_->setToolTip(tr("Open Options Dialog"));

  feedsFilter_->setText(tr("Filter Feeds"));
  filterFeedsAll_->setText(tr("Show All"));
  filterFeedsNew_->setText(tr("Show New"));
  filterFeedsUnread_->setText(tr("Show Unread"));
  filterFeedsStarred_->setText(tr("Show Starred Feeds"));
  filterFeedsError_->setText(tr("Show Not Working Feeds"));

  newsFilter_->setText(tr("Filter News"));
  filterNewsAll_->setText(tr("Show All"));
  filterNewsNew_->setText(tr("Show New"));
  filterNewsUnread_->setText(tr("Show Unread"));
  filterNewsStar_->setText(tr("Show Starred"));
  filterNewsNotStarred_->setText(tr("Show Not Starred"));
  filterNewsUnreadStar_->setText(tr("Show Unread or Starred"));
  filterNewsLastDay_->setText(tr("Show Last Day"));
  filterNewsLastWeek_->setText(tr("Show Last 7 Days"));

  aboutAct_ ->setText(tr("About..."));
  aboutAct_->setToolTip(tr("Show 'About' Dialog"));

  updateAppAct_->setText(tr("Check for Updates..."));
  reportProblemAct_->setText(tr("Report a Problem..."));

  openDescriptionNewsAct_->setText(tr("Open Article"));
  openDescriptionNewsAct_->setToolTip(tr("Open Article Description"));
  openInExternalBrowserAct_->setText(tr("Open"));
  openInExternalBrowserAct_->setToolTip(tr("Open"));
  markStarAct_->setText(tr("Star"));
  markStarAct_->setToolTip(tr("Star Article"));
  deleteNewsAct_->setText(tr("Delete"));
  deleteNewsAct_->setToolTip(tr("Delete Selected Articles"));
  deleteAllNewsAct_->setText(tr("Delete All Articles"));
  deleteAllNewsAct_->setToolTip(tr("Delete All Articles from List"));
  restoreNewsAct_->setText(tr("Restore"));
  restoreNewsAct_->setToolTip(tr("Restore Article"));
  copyLinkAct_->setText(tr("Copy Link"));
  copyLinkAct_->setToolTip(tr("Copy Article Link"));

  restoreLastNewsAct_->setText(tr("Restore Last Deleted Article"));

  markFeedRead_->setText(tr("Mark Read"));
  markFeedRead_->setToolTip(tr("Mark Feed Read"));
  feedProperties_->setText(tr("Properties"));
  feedProperties_->setToolTip(tr("Properties"));

  fileMenu_->setTitle(tr("&File"));
  viewMenu_->setTitle(tr("&View"));
  feedMenu_->setTitle(tr("Fee&ds"));
  newsMenu_->setTitle(tr("&News"));
  browserMenu_->setTitle(tr("&Article"));
  toolsMenu_->setTitle(tr("&Tools"));
  helpMenu_->setTitle(tr("&Help"));

  mainToolbar_->setWindowTitle(tr("Main Toolbar"));
  customizeToolbarMenu_->setTitle(tr("Customize Toolbar"));
  customizeMainToolbarAct_->setText(tr("Main Toolbar..."));
  customizeMainToolbarAct2_->setText(tr("Customize Toolbar..."));
  customizeFeedsToolbarAct_->setText(tr("Feeds Toolbar..."));
  customizeNewsToolbarAct_->setText(tr("News Toolbar..."));

  toolBarLockAct_->setText(tr("Lock Toolbar"));
  toolBarHideAct_->setText(tr("Hide Toolbar"));

  layoutMenu_->setTitle(tr("Layout"));
  classicLayoutAct_->setText(tr("Classic"));
  newspaperLayoutAct_->setText(tr("Newspaper"));
  layoutToggle_->setText(tr("Layout"));

  styleMenu_->setTitle(tr("Application Theme"));
  rebuildStyleMenu();

  browserPositionMenu_->setTitle(tr("Article Pane Position"));
  topBrowserPositionAct_->setText(tr("Top"));
  bottomBrowserPositionAct_->setText(tr("Bottom"));
  rightBrowserPositionAct_->setText(tr("Right"));
  leftBrowserPositionAct_->setText(tr("Left"));

  showWindowAct_->setText(tr("Show Window"));

  feedKeyUpAct_->setText(tr("Previous Feed"));
  feedKeyDownAct_->setText(tr("Next Feed"));
  newsKeyUpAct_->setText(tr("Previous Article"));
  newsKeyDownAct_->setText(tr("Next Article"));
  newsKeyPageUpAct_->setText(tr("Article Page Up"));
  newsKeyPageDownAct_->setText(tr("Article Page Down"));

  nextUnreadNewsAct_->setText(tr("Next Unread Article"));
  prevUnreadNewsAct_->setText(tr("Previous Unread Article"));

  switchFocusAct_->setText(tr("Switch Focus to Next Panel"));
  switchFocusAct_->setToolTip(
        tr("Switch Focus to Next Panel (Feed Tree, Article List, Article Pane)"));
  switchFocusPrevAct_->setText(tr("Switch Focus to Previous Panel"));
  switchFocusPrevAct_->setToolTip(
        tr("Switch Focus to Previous Panel (Feed Tree, Article Pane, Article List)"));

  feedsWidgetVisibleAct_->setText(tr("Feeds Pane"));

  placeToTrayAct_->setText(tr("Minimize to Tray"));
  placeToTrayAct_->setToolTip(tr("Minimize Application to Tray"));

  feedsColumnsMenu_->setTitle(tr("Columns"));
  showUnreadCount_->setText(tr("Number of unread articles"));
  showUndeleteCount_->setText(tr("Total number of articles"));
  showLastUpdated_->setText(tr("Last update"));

  indentationFeedsTreeAct_->setText(tr("Enable Indentation"));

  findFeedAct_->setText(tr("Search Feed"));
  findFeedAct_->setToolTip(tr("Search Feed"));

  browserZoomMenu_->setTitle(tr("Zoom"));
  zoomInAct_->setText(tr("Zoom In"));
  zoomInAct_->setToolTip(tr("Zoom in"));
  zoomOutAct_->setText(tr("Zoom Out"));
  zoomOutAct_->setToolTip(tr("Zoom out"));
  zoomTo100Act_->setText(tr("100%"));
  zoomTo100Act_->setToolTip(tr("Reset zoom"));

  printAct_->setText(tr("Print..."));
  printAct_->setToolTip(tr("Print Article"));
  printPreviewAct_->setText(tr("Print Preview..."));
  printPreviewAct_->setToolTip(tr("Preview Article"));

  pageUpWebViewAct_->setText(tr("Page up (Article)"));
  pageDownWebViewAct_->setText(tr("Page down (Article)"));

  savePageAsAct_->setText(tr("Save As..."));
  savePageAsAct_->setToolTip(tr("Save Article As..."));

  toolbarsMenu_->setTitle(tr("Show/Hide"));
  mainToolbarToggle_->setText(tr("Main Toolbar"));
  feedsToolbarToggle_->setText(tr("Feeds Toolbar"));
  newsToolbarToggle_->setText(tr("News Toolbar"));
  categoriesPanelToggle_->setText(tr("Panel Categories"));
  statusBarToggle_->setText(tr("Status Bar"));

  fullScreenAct_->setText(tr("Full Screen"));
  fullScreenAct_->setToolTip(tr("Full Screen"));

  stayOnTopAct_->setText(tr("Stay On Top"));
  stayOnTopAct_->setToolTip(tr("Stay On Top"));

  categoriesLabel_->setText(tr("Categories"));
  if (categoriesTree_->isHidden())
    showCategoriesButton_->setToolTip(tr("Show Categories"));
  else
    showCategoriesButton_->setToolTip(tr("Hide Categories"));

  newsLabelMenuAction_->setText(tr("Label"));
  newsLabelAction_->setText(tr("Label"));
  showLabelsMenuAct_->setText(tr("Show labels menu"));

  closeTabAct_->setText(tr("Close Tab"));
  closeOtherTabsAct_->setText(tr("Close Other Tabs"));
  closeAllTabsAct_->setText(tr("Close All Tabs"));
  nextTabAct_->setText(tr("Switch to next tab"));
  prevTabAct_->setText(tr("Switch to previous tab"));

  categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->setText(0, tr("Unread"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::StarredItem)->setText(0, tr("Starred"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::DeletedItem)->setText(0, tr("Deleted"));
  categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->setText(0, tr("Labels"));

  reduceNewsListAct_->setText(tr("Decrease article list / increase article pane"));
  increaseNewsListAct_->setText(tr("Increase article list / decrease article pane"));

  findTextAct_->setText(tr("Find"));

  openHomeFeedAct_->setText(tr("Open Feed Homepage"));
  sortedByTitleFeedsTreeAct_->setText(tr("Sort by Name"));
  collapseAllFoldersAct_->setText(tr("Collapse All Folders"));
  expandAllFoldersAct_->setText(tr("Expand All Folders"));
  nextFolderAct_->setText(tr("Next Folder"));
  prevFolderAct_->setText(tr("Previous Folder"));
  expandFolderAct_->setText(tr("Expand Folder"));

  settingPageLabelsAct_->setText(tr("Settings Page: Labels"));

  shareMenuAct_->setText(tr("Share"));

  newsSortByMenu_->setTitle(tr("Sort By"));
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

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

#include "aboutdialog.h"
#include "cleanupwizard.h"
#include "database.h"
#include "databasebackup.h"
#include "downloadmanager.h"
#include "filterrulesdialog.h"
#include "mainapplication.h"
#include "networkmanager.h"
#include "newsfiltersdialog.h"
#include "projectmetadata.h"
#include "settings.h"
#include "soundplayer.h"
#include "statusbarcontroller.h"
#include "trayiconcontroller.h"
#include "updatefeeds.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif
#include <QMainWindow>
#include <QPointer>
#include <QStatusBar>
#ifdef HAVE_X11
#include <QLibrary>
#endif
#include <utility>

namespace {
#ifdef Q_OS_WIN
void requestWindowsLinkFocus(HWND window)
{
  if (!IsWindowVisible(window) || IsIconic(window)) return;
  if (GetForegroundWindow() == window || SetForegroundWindow(window)) return;

  // Like Qt's AlwaysActivateWindow path, temporarily share the foreground
  // thread's input queue. Keep this local to the opt-in article-link action.
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return;
  const DWORD foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
  const DWORD currentThread = GetCurrentThreadId();
  if (!foregroundThread || foregroundThread == currentThread) return;

  // Probe before attaching: do not deliberately couple to an unresponsive app.
  // This limits the probe, not the subsequent native activation calls.
  if (!SendMessageTimeoutW(foreground, WM_NULL, 0, 0,
                           SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                           100, nullptr)) return;
  if (GetForegroundWindow() != foreground ||
      !IsWindowVisible(window) || IsIconic(window)) return;
  if (!AttachThreadInput(currentThread, foregroundThread, TRUE)) return;

  BringWindowToTop(window);
  SetActiveWindow(window);
  // Every successful attachment must be undone before returning to Qt.
  AttachThreadInput(currentThread, foregroundThread, FALSE);
}
#endif

#ifdef HAVE_X11
// Keep X11 optional: use the small Xlib ABI surface required for the
// _NET_WM_STATE_STICKY client message without linking to libX11 or requiring
// X11 development headers. This code is reached only when Qt is using xcb.
using XWindow = unsigned long;
using XAtom = unsigned long;

struct XClientMessageEventCompat {
  int type;
  unsigned long serial;
  int sendEvent;
  void *display;
  XWindow window;
  XAtom messageType;
  int format;
  union {
    char bytes[20];
    short shorts[10];
    long longs[5];
  } data;
};

union XEventCompat {
  int type;
  XClientMessageEventCompat client;
  long padding[24];
};

bool applyX11StickyState(WId windowId, bool sticky)
{
  if (QGuiApplication::platformName() != QLatin1String("xcb"))
    return false;

  QLibrary x11(QStringLiteral("X11"), 6);
  if (!x11.load())
    return false;

  using OpenDisplay = void *(*)(const char *);
  using CloseDisplay = int (*)(void *);
  using DefaultRootWindow = XWindow (*)(void *);
  using InternAtom = XAtom (*)(void *, const char *, int);
  using SendEvent = int (*)(void *, XWindow, int, long, XEventCompat *);
  using Flush = int (*)(void *);

  const auto openDisplay = reinterpret_cast<OpenDisplay>(x11.resolve("XOpenDisplay"));
  const auto closeDisplay = reinterpret_cast<CloseDisplay>(x11.resolve("XCloseDisplay"));
  const auto defaultRootWindow = reinterpret_cast<DefaultRootWindow>(x11.resolve("XDefaultRootWindow"));
  const auto internAtom = reinterpret_cast<InternAtom>(x11.resolve("XInternAtom"));
  const auto sendEvent = reinterpret_cast<SendEvent>(x11.resolve("XSendEvent"));
  const auto flush = reinterpret_cast<Flush>(x11.resolve("XFlush"));
  if (!openDisplay || !closeDisplay || !defaultRootWindow || !internAtom || !sendEvent || !flush)
    return false;

  void *display = openDisplay(nullptr);
  if (!display)
    return false;

  const XAtom stateAtom = internAtom(display, "_NET_WM_STATE", 0);
  const XAtom stickyAtom = internAtom(display, "_NET_WM_STATE_STICKY", 0);
  bool sent = false;
  if (stateAtom && stickyAtom) {
    XEventCompat event = {};
    event.client.type = 33;  // ClientMessage
    event.client.window = static_cast<XWindow>(windowId);
    event.client.messageType = stateAtom;
    event.client.format = 32;
    event.client.data.longs[0] = sticky ? 1 : 0;  // _NET_WM_STATE_ADD / REMOVE
    event.client.data.longs[1] = static_cast<long>(stickyAtom);
    event.client.data.longs[3] = 1;  // normal application source indication
    constexpr long eventMask = (1L << 20) | (1L << 19);  // SubstructureRedirect/Notify
    sent = sendEvent(display, defaultRootWindow(display), 0, eventMask, &event) != 0;
    flush(display);
  }
  closeDisplay(display);
  return sent;
}
#endif
}

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , isMinimizeToTray_(true)
  , currentNewsTab(nullptr)
  , feedsFilterAction_(nullptr)
  , newsFilterAction_(nullptr)
  , newsView_(nullptr)
  , soundPlayer_(new SoundPlayer(this))
  , updateAppDialog_(nullptr)
  , notificationWidget(nullptr)
  , feedIdOld_(-2)
  , isStartImportFeed_(false)
  , recountCategoryCountsOn_(false)
  , optionsDialog_(nullptr)
{
  quitTimer_.setInterval(25);
  connect(&quitTimer_, &QTimer::timeout, this, &MainWindow::finishQuitApp);

  returnFocusAfterLinkTimer_.setSingleShot(true);
  returnFocusAfterLinkTimer_.setInterval(1000);
  connect(&returnFocusAfterLinkTimer_, &QTimer::timeout, this, [this]() {
    if (!AppSettings::returnFocusAfterLink.get() || mainApp->isClosing() ||
        quitTimer_.isActive() || !isVisible() || isMinimized() || isActiveWindow() ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
    // Request activation once; the desktop may refuse it. Never unhide a window.
#ifdef Q_OS_WIN
    requestWindowsLinkFocus(reinterpret_cast<HWND>(winId()));
#else
    raise();
    activateWindow();
#endif
  });

  databaseUiTimer_.setSingleShot(true);
  databaseUiTimer_.setInterval(25);
  connect(&databaseUiTimer_, &QTimer::timeout, this, &MainWindow::retryDatabaseUi);

  updateNewsTimer_.setSingleShot(true);
  updateNewsTimer_.setInterval(1000);
  connect(&updateNewsTimer_, &QTimer::timeout, this,
          [this] { slotUpdateNews(NewsTabWidget::RefreshInsert); });

  setObjectName("mainWindow");
  setWindowTitle(QGuiApplication::applicationDisplayName());
  setContextMenuPolicy(Qt::CustomContextMenu);

  connect(soundPlayer_, &SoundPlayer::playbackFailed, this,
          [this](const QString &soundPath, const QString &errorText) {
    if (mainApp->isClosing()) return;
    // A warning owned by MainWindow would sit behind (and be blocked by)
    // an open modal dialog, such as Settings or the filter editor.
    QWidget *warningParent = QApplication::activeModalWidget();
    if (!warningParent) warningParent = this;
    QMessageBox *warning = new QMessageBox(
          QMessageBox::Warning, tr("Sound playback failed"),
          tr("The notification sound could not be played.\n\nFile: %1\nError: %2")
              .arg(soundPath, errorText),
          QMessageBox::Ok, warningParent);
    warning->setTextFormat(Qt::PlainText);
    warning->setAttribute(Qt::WA_DeleteOnClose);
    warning->setWindowModality(Qt::NonModal);
    warning->show();
    warning->raise();
    warning->activateWindow();
  }, Qt::QueuedConnection);

  db_ = QSqlDatabase::database();

  createFeedsWidget();
  createToolBarNull();

  createActions();
  createShortcut();
  createMenu();
  createToolBar();

  createStatusBar();
  createTray();

  createTabBarWidget();
  createCentralWidget();

  loadSettingsFeeds();

  setStyleSheet("QMainWindow::separator { width: 1px; }");

  loadSettings();

  addDefaultFeed();

  initUpdateFeeds();

  QTimer::singleShot(5000, this, &MainWindow::slotUpdateAppCheck);

  connect(this, &MainWindow::signalShowNotification,
          this, &MainWindow::showNotification, Qt::QueuedConnection);
  connect(this, &MainWindow::signalPlaySoundNewNews,
          this, &MainWindow::slotPlaySoundNewNews, Qt::QueuedConnection);

  connect(mainApp->downloadManager(), &DownloadManager::signalShowDownloads,
          this, &MainWindow::showDownloadManager);
  connect(mainApp->downloadManager(), &DownloadManager::signalUpdateInfo,
          this, &MainWindow::updateInfoDownloads);

  connect(&timerTrayOpenNotify, &QTimer::timeout, this, &MainWindow::slotTrayOpenNotifyTimer);
  timerTrayOpenNotify.setSingleShot(true);

  retranslateStrings();

  // Queue selection notifications so the view has updated its selected feed ID.
  connect(feedsView_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &MainWindow::updateFeedActionText, Qt::QueuedConnection);
  connect(feedsView_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::updateFeedActionText, Qt::QueuedConnection);
  connect(feedsModel_, &QAbstractItemModel::dataChanged,
          this, &MainWindow::updateFeedActionText, Qt::QueuedConnection);
  connect(feedsModel_, &QAbstractItemModel::modelReset,
          this, &MainWindow::updateFeedActionText, Qt::QueuedConnection);

  installEventFilter(this);
}

// ---------------------------------------------------------------------------
MainWindow::~MainWindow()
{

}

// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
  returnFocusAfterLinkTimer_.stop();
  if (mainApp->isClosing())
    return;

  if (closingTray_ && showTrayIcon_) {
    event->ignore();

    oldState = windowState();
    emit signalPlaceToTray();
  } else {
#if defined(Q_OS_MAC)
    event->ignore();
#endif
    quitApp();
  }
}

/** @brief Process quit application
 *---------------------------------------------------------------------------*/
void MainWindow::quitApp()
{
  returnFocusAfterLinkTimer_.stop();
  if (mainApp->isClosing() || quitTimer_.isActive()) return;
  // Always return to the event loop first, including for a tray menu action.
  quitTimer_.start();
}

void MainWindow::finishQuitApp()
{
  if (!mainApp->finishModalOperations()) return;
  quitTimer_.stop();
  mainApp->setClosing();
  databaseUiTimer_.stop();
  deferredDatabaseUi_.clear();
  DatabaseBackup::instance()->stop();
  isMinimizeToTray_ = true;
  disconnect(this);
  if (updateAppDialog_) {
    updateAppDialog_->disconnectObjects();
    updateAppDialog_->deleteLater();
  }
  if (optionsDialog_) {
    optionsDialog_->close();
  }

  mainApp->showClosingWidget();

  hide();
  trayIconController_->hide();

  saveSettings();

  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    widget->disconnectObjects();
  }

  mainApp->networkManager()->disconnect(mainApp->networkManager());
  mainApp->updateFeeds()->disconnectObjects();
  emit signalQuitApp();
}

// ---------------------------------------------------------------------------
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == this) {
    if (event->type() == QEvent::Hide ||
        (event->type() == QEvent::WindowStateChange && isMinimized()))
      returnFocusAfterLinkTimer_.stop();
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
      if ((keyEvent->key() == Qt::Key_Up) ||
          (keyEvent->key() == Qt::Key_Down) ||
          (keyEvent->key() == Qt::Key_Left) ||
          (keyEvent->key() == Qt::Key_Right)) {
        QListIterator<QAction *> iter(shortcutRegistry_.actions());
        while (iter.hasNext()) {
          QAction *pAction = iter.next();
          if (pAction->shortcut() == QKeySequence(keyEvent->key())) {
            pAction->activate(QAction::Trigger);
            break;
          }
        }
      }
    }
  }

  if (obj == statusBar()) {
    if (event->type() == QEvent::MouseButtonRelease) {
      if (windowState() & Qt::WindowMaximized) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if ((mouseEvent->pos().x() > (statusBar()->width()-statusBar()->height())) &&
            (mouseEvent->pos().y() > 0)) {
          setFullScreen();
        }
      }
    }
    return false;
  } else if (obj == categoriesLabel_) {
    if (event->type() == QEvent::MouseButtonRelease) {
      showNewsCategoriesTree();
    }
    return false;
  } else if (obj == categoriesTree_) {
    if (event->type() == QEvent::Show) {
      recountCategoryCounts();
    }
  }
  else if (event->type() == QEvent::WindowActivate) {
    activationStateChangedTime_ = QDateTime::currentMSecsSinceEpoch();
  }
  else if (event->type() == QEvent::WindowDeactivate) {
    activationStateChangedTime_ = QDateTime::currentMSecsSinceEpoch();
  } else if (event->type() == QEvent::Hide) {
    if (minimizingTray_  && showTrayIcon_ && !isMinimizeToTray_) {
      emit signalPlaceToTray();
    }
  }

  // pass the event on to the parent class
  return QMainWindow::eventFilter(obj, event);
}

/** @brief Process changing window state
 *---------------------------------------------------------------------------*/
void MainWindow::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::WindowStateChange) {
    if (isMinimized()) {
      oldState = static_cast<QWindowStateChangeEvent *>(event)->oldState();
    } else {
      oldState = windowState();
    }
  } else if (event->type() == QEvent::ActivationChange) {
    if (isActiveWindow() && (behaviorIconTray_ == CHANGE_ICON_TRAY)) {
      trayIconController_->showDefaultIcon();
    }
  } else if (event->type() == QEvent::LanguageChange) {
    retranslateStrings();
  } else if ((event->type() == QEvent::PaletteChange ||
              event->type() == QEvent::ApplicationPaletteChange) &&
             mainApp->applicationStyle().followsSystem()) {
    // Automatic follows the palette supplied by Qt's platform integration.
    // Recompute only theme-derived colors; explicit user overrides remain.
    QTimer::singleShot(0, this, [this]() {
      if (mainApp->applicationStyle().followsSystem()) applyThemeColors(false);
    });
  }
  QMainWindow::changeEvent(event);
}

/** @brief Process placing to tray event
 *---------------------------------------------------------------------------*/
void MainWindow::slotPlaceToTray()
{
  isMinimizeToTray_ = true;
  hide();

  if (emptyWorking_)
    QTimer::singleShot(10000, this, &MainWindow::myEmptyWorkingSet);
  if (markReadMinimize_)
    setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadPlaceToTray, currentNewsTab);
  if (clearStatusNew_)
    QTimer::singleShot(0, this, &MainWindow::signalMarkAllFeedsOld);
  clearNotification(true);

  saveSettings();

  isMinimizeToTray_ = false;
}

/** @brief Process tray event
 *---------------------------------------------------------------------------*/
void MainWindow::slotActivationTray(QSystemTrayIcon::ActivationReason reason)
{
  bool activated = false;

  switch (reason) {
  case QSystemTrayIcon::Unknown:
    break;
  case QSystemTrayIcon::Context:
    trayIconController_->activateMenu();
    break;

  case QSystemTrayIcon::DoubleClick:
  {
    if (!singleClickTray_)
    {
      timerTrayOpenNotify.stop();

      if ((QDateTime::currentMSecsSinceEpoch() - activationStateChangedTime_ < 300) || isActiveWindow())
      {
        activated = true;
      }

      showWindows(activated);
    }

    break;
  }

  case QSystemTrayIcon::Trigger:
  {
    if (singleClickTray_)
    {
      qint64 lastActiveChangeTime = QDateTime::currentMSecsSinceEpoch() - activationStateChangedTime_;

      if (isActiveWindow() ? (lastActiveChangeTime >= 200) : (lastActiveChangeTime < 200))
      {
        activated = true;
      }

      showWindows(activated);
    }
    else
    {
      if (notificationWidget == nullptr)
      {
        if (!timerTrayOpenNotify.isActive())
        {
          timerTrayOpenNotify.start(400);
        }
      }
      else
      {
        delete notificationWidget;
        notificationWidget = nullptr;
      }
    }

    break;
  }

  case QSystemTrayIcon::MiddleClick:
    break;
  }
}
void MainWindow::slotTrayOpenNotifyTimer()
{
  emit signalShowNotification(true);
}

/** @brief Show window on event
 *---------------------------------------------------------------------------*/
void MainWindow::showWindows(bool trayClick)
{
  if (!trayClick || isHidden()) {
    if (oldState & Qt::WindowFullScreen) {
      show();
    } else if (oldState & Qt::WindowMaximized) {
      showMaximized();
    } else {
      showNormal();
      Settings settings;
      restoreGeometry(settings.value("GeometryState").toByteArray());
    }
    activateWindow();
#ifdef HAVE_X11
    if (allWorkspacesAct_ && allWorkspacesAct_->isChecked())
      setAllWorkspaces();
#endif
  } else {
    if (minimizingTray_)
      emit signalPlaceToTray();
    else
      showMinimized();
  }
}

void MainWindow::deferDatabaseUi(DeferredDatabaseUi operation, std::function<void()> retry)
{
  // Keep only the latest selection/refresh, rather than replaying keystrokes
  // against views the user has already left.
  deferredDatabaseUi_[operation] = std::move(retry);
  if (!databaseUiTimer_.isActive()) databaseUiTimer_.start();
}

void MainWindow::retryDatabaseUi()
{
  if (mainApp->isClosing()) {
    deferredDatabaseUi_.clear();
    return;
  }
  if (deferredDatabaseUi_.empty()) return;
  auto databaseAccess = Database::tryAccess();
  if (!databaseAccess.owns_lock()) {
    databaseUiTimer_.start();
    return;
  }
  // Navigation comes first. Run one job per turn so a model reload and an
  // article render do not accumulate into another long UI event.
  auto job = deferredDatabaseUi_.begin();
  auto retry = std::move(job->second);
  deferredDatabaseUi_.erase(job);
  retry();
  if (!deferredDatabaseUi_.empty()) databaseUiTimer_.start();
}

/** @brief Free memory working set in Windows
 *---------------------------------------------------------------------------*/
void MainWindow::myEmptyWorkingSet()
{
#if defined(Q_OS_WIN)
  if (isHidden())
    EmptyWorkingSet(GetCurrentProcess());
#endif
}

// ----------------------------------------------------------------------------
void MainWindow::slotShowAboutDlg()
{
  AboutDialog *aboutDialog = new AboutDialog(this);
  aboutDialog->exec();
  delete aboutDialog;
}

// ----------------------------------------------------------------------------
void MainWindow::slotShowUpdateAppDlg()
{
  UpdateAppDialog *updateAppDialog = new UpdateAppDialog(this);
  updateAppDialog->activateWindow();
  updateAppDialog->exec();
  delete updateAppDialog;
}

/** @brief Update tray information: icon and tooltip text
 *---------------------------------------------------------------------------*/
void MainWindow::slotRefreshInfoTray(int newCount, int unreadCount)
{
  if (!unreadCount)
    categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->setText(4, "");
  else
    categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->setText(4, QString("(%1)").arg(unreadCount));
  QFont font = categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->font(0);
  if (unreadCount)
    font.setBold(true);
  else
    font.setBold(false);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::UnreadItem)->setFont(0, font);

  if (!showTrayIcon_) return;

  // Setting tooltip text
  QString info =
      QGuiApplication::applicationDisplayName() + "\n" +
      QString(tr("New Articles: %1")).arg(newCount) +
      QString("\n") +
      QString(tr("Unread Articles: %1")).arg(unreadCount);
  trayIconController_->setToolTip(info);

  // Display new number or unread number of news
  if (behaviorIconTray_ > CHANGE_ICON_TRAY) {
    int trayCount = (behaviorIconTray_ == UNREAD_COUNT_ICON_TRAY) ? unreadCount : newCount;
    trayIconController_->showCountIcon(trayCount);
  }
}

// ----------------------------------------------------------------------------
void MainWindow::slotPlaySound(const QString &path)
{
  soundPlayer_->play(mainApp->absolutePath(path));
}

void MainWindow::slotPlaySoundNewNews()
{
  if (!playSoundNewNews_ && soundNewNews_) {
    slotPlaySound(soundNotifyPath_);
    playSoundNewNews_ = true;
  }
}

// ----------------------------------------------------------------------------
void MainWindow::showNewsFiltersDlg(bool newFilter)
{
  QPointer<NewsFiltersDialog> newsFiltersDialog = new NewsFiltersDialog(this);
  if (newFilter) {
    newsFiltersDialog->filtersTree_->setCurrentItem(
          newsFiltersDialog->filtersTree_->topLevelItem(
            newsFiltersDialog->filtersTree_->topLevelItemCount()-1));
  }

  newsFiltersDialog->exec();

  // Tray Exit can destroy the parent and this dialog inside exec().
  delete newsFiltersDialog.data();
}
// ----------------------------------------------------------------------------
void MainWindow::showFilterRulesDlg()
{
  if (!feedsView_->selectIndex().isValid()) return;

  int feedId = feedsView_->selectId_;

  QPointer<FilterRulesDialog> filterRulesDialog = new FilterRulesDialog(
        this, -1, feedId);

  QModelIndex index = feedsModel_->indexById(feedId);
  QString text = feedsModel_->dataField(index, "text").toString();
  filterRulesDialog->filterName_->setText(QString("'%1'").arg(text));

  int result = filterRulesDialog->exec();
  // The parent may have been destroyed by tray Exit during the modal loop.
  if (!filterRulesDialog) return;
  if (result == QDialog::Rejected) {
    delete filterRulesDialog.data();
    return;
  }

  delete filterRulesDialog.data();

  showNewsFiltersDlg(true);
}
// ----------------------------------------------------------------------------
void MainWindow::slotUpdateAppCheck()
{
  updateAppDialog_ = new UpdateAppDialog(this, false);
  connect(updateAppDialog_, &UpdateAppDialog::signalNewVersion,
          this, &MainWindow::slotNewVersion, Qt::QueuedConnection);
}
// ----------------------------------------------------------------------------
void MainWindow::slotNewVersion(const QString &newVersion)
{
  updateAppDialog_->disconnectObjects();
  delete updateAppDialog_;
  updateAppDialog_ = nullptr;

  if (!newVersion.isEmpty()) {
    trayIconController_->showMessage(tr("Check for updates"),
                                     tr("A new version of %1 is available").arg(QGuiApplication::applicationDisplayName()));
    connect(trayIcon(), &QSystemTrayIcon::messageClicked, this, &MainWindow::slotShowUpdateAppDlg);
  }
}

/** Switch focus forward between feed tree, news list and browser
 *---------------------------------------------------------------------------*/
void MainWindow::slotSwitchFocus()
{
  if (feedsView_->hasFocus()) {
    newsView_->setFocus();
  } else if (newsView_->hasFocus()) {
    currentNewsTab->articleView_->setFocus();
  } else {
    feedsView_->setFocus();
  }
}

/** @brief Switch focus backward between feed tree, news list and browser
 *---------------------------------------------------------------------------*/
void MainWindow::slotSwitchPrevFocus()
{
  if (feedsView_->hasFocus()) {
    currentNewsTab->articleView_->setFocus();
  } else if (currentNewsTab->articleView_->hasFocus()) {
    newsView_->setFocus();
  } else {
    feedsView_->setFocus();
  }
}

/** @brief Totally remove news
 *---------------------------------------------------------------------------*/
void MainWindow::cleanUp()
{
  CleanUpWizard *cleanUpWizard = new CleanUpWizard(this);
  cleanUpWizard->exec();
  delete cleanUpWizard;
}

/** @brief Call default e-mail application to report the problem
 *---------------------------------------------------------------------------*/
void MainWindow::slotReportProblem()
{
  mainApp->openExternalUrl(QUrl(ProjectMetadata::issuesUrl()));
}

// ----------------------------------------------------------------------------
void MainWindow::setFullScreen()
{
  if (!isFullScreen()) {
    // hide menu
    menuBar()->hide();
#ifdef HAVE_X11
    show();
    raise();
    setWindowState(windowState() | Qt::WindowFullScreen);
#else
    setWindowState(windowState() | Qt::WindowFullScreen);
#endif
  } else {
    if (showMenuBarAct_->isChecked())
      menuBar()->show();
    setWindowState(windowState() & ~Qt::WindowFullScreen);
  }
}
// ----------------------------------------------------------------------------
void MainWindow::setStayOnTop()
{
  isMinimizeToTray_ = true;

  const Qt::WindowStates state = windowState();

  if (stayOnTopAct_->isChecked())
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  else
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);

  if ((state & Qt::WindowFullScreen) || (state & Qt::WindowMaximized)) {
    Settings settings;
    restoreGeometry(settings.value("GeometryState").toByteArray());
  }
  setWindowState(state);
  show();
#ifdef HAVE_X11
  if (allWorkspacesAct_ && allWorkspacesAct_->isChecked())
    setAllWorkspaces();
#endif

  isMinimizeToTray_ = false;
}
#ifdef HAVE_X11
void MainWindow::setAllWorkspaces()
{
  if (!allWorkspacesAct_)
    return;
  applyX11StickyState(winId(), allWorkspacesAct_->isChecked());
}
#endif

void MainWindow::showMenuBar()
{
  if (showMenuBarAct_->isChecked()) {
    mainMenuButton_->hide();
    if (isFullScreen())
      menuBar()->hide();
    else
      menuBar()->show();
  } else {
    mainMenuButton_->show();
    menuBar()->hide();
  }
}

/** @brief Set application title
 *---------------------------------------------------------------------------*/
void MainWindow::setTextTitle(const QString &text, NewsTabWidget *widget)
{
  if (currentNewsTab != widget) return;

  if (text.isEmpty()) setWindowTitle(QGuiApplication::applicationDisplayName());
  else setWindowTitle(QString("%1 - %2").arg(text, QGuiApplication::applicationDisplayName()));
}

// ----------------------------------------------------------------------------
void MainWindow::showDownloadManager(bool activate)
{
  int indexTab = -1;
  NewsTabWidget *widget;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->type_ == NewsTabWidget::TabTypeDownloads) {
      indexTab = i;
      break;
    }
  }

  if (indexTab == -1) {
    widget = new NewsTabWidget(this, NewsTabWidget::TabTypeDownloads);
    indexTab = addTab(widget);
    QPixmap iconTab;
    iconTab.load(":/images/download");
    widget->newsIconTitle_->setPixmap(iconTab);
    widget->retranslateStrings();
  }

  if (activate) {
    currentNewsTab = widget;
    currentNewsTab->setTextTab(tr("Downloads"));
    statusBarController_->setCountsVisible(false, false);
    mainApp->downloadManager()->show();
    emit signalSetCurrentTab(indexTab);
  } else {
    widget->setTextTab(tr("Downloads"));
  }
}
// ----------------------------------------------------------------------------
void MainWindow::updateInfoDownloads(const QString &text)
{
  NewsTabWidget *widget;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->type_ == NewsTabWidget::TabTypeDownloads) {
      widget->setTextTab(QString("%1 %2").arg(tr("Downloads")).arg(text));
      break;
    }
  }
}

void MainWindow::createBackup()
{
  DatabaseBackup::instance()->manual(this);
}

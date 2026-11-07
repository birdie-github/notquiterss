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
#include "feeddeletion.h"
#include "opmlinput.h"
#include <utility>
#include "../network/feedurl.h"
#include "databasebackup.h"
#include "feedhealth.h"
#include "projectmetadata.h"
#include <QTextCodec>
#include "mainwindow.h"
#include "opmlexporter.h"
#include "statusbarcontroller.h"
#include "trayiconcontroller.h"
#include "soundplayer.h"
#include "articlecontent.h"

#include "common.h"
#include "mainapplication.h"
#include <QPointer>
#include "database.h"
#include "aboutdialog.h"
#include "addfeedwizard.h"
#include "addfolderdialog.h"
#include "cleanupwizard.h"
#include "customizetoolbardialog.h"
#include "feedpropertiesdialog.h"
#include "filterrulesdialog.h"
#include "newsfiltersdialog.h"
#include "cookiesdialog.h"
#include <QPainter>
#include <QPixmap>
#include "settings.h"
#include "shareservice.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif
#include <QMainWindow>
#include <QFileInfo>
#include <QStatusBar>
#include <QRegularExpression>
#include <QScreen>
#include <QStyle>
#ifdef HAVE_X11
#include <QLibrary>
#endif

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

/** @brief Add feed to feed list
 *---------------------------------------------------------------------------*/
void MainWindow::addFeed()
{
  if (mainApp->isClosing()) return;
  int curFolderId = 0;
  QPersistentModelIndex curIndex = feedsView_->selectIndex();
  if (feedsModel_->isFolder(curIndex)) {
    curFolderId = feedsModel_->idByIndex(curIndex);
  } else {
    curFolderId = feedsModel_->paridByIndex(curIndex);
  }

  QStringList urls;
  int position = 0;
  do {
    AddFeedWizard wizard(this, curFolderId);
    if (!urls.isEmpty()) {
      wizard.configureFeed(urls.at(position), position + 1, urls.size());
      ++position;
    }
    const int result = wizard.exec();
    if (mainApp->isClosing() || result == QDialog::Rejected) return;
    if (result == AddFeedWizard::FeedsSelected) {
      urls = wizard.selectedUrls();
      position = 0;
      continue;
    }
    if (result == AddFeedWizard::FeedSkipped) continue;
    emit faviconRequestUrl(wizard.htmlUrlString_, wizard.feedUrlString_);

    QList<int> categoriesList;
    categoriesList << wizard.feedParentId_;
    recountFeedCategories(categoriesList);

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    feedsView_->setCurrentIndex(QModelIndex());
    feedsModelReload();
    QModelIndex index = feedsProxyModel_->mapFromSource(wizard.feedId_);
    feedsView_->selectIdEn_ = true;
    feedsView_->setCurrentIndex(index);
    slotFeedClicked(index);
    QApplication::restoreOverrideCursor();
    slotUpdateFeed(wizard.feedId_, true, wizard.newCount_, false);

    DatabaseBackup::subscriptionsChanged("add feed");
  } while (position < urls.size());
}

/** @brief Add folder to feed list
 *---------------------------------------------------------------------------*/
void MainWindow::addFolder()
{
  int curFolderId = 0;
  QPersistentModelIndex curIndex = feedsView_->selectIndex();
  if (feedsModel_->isFolder(curIndex)) {
    curFolderId = feedsModel_->idByIndex(curIndex);
  } else {
    curFolderId = feedsModel_->paridByIndex(curIndex);
  }

  AddFolderDialog *addFolderDialog = new AddFolderDialog(this, curFolderId);

  if (addFolderDialog->exec() == QDialog::Rejected) {
    delete addFolderDialog;
    return;
  }

  QString folderText = addFolderDialog->nameFeedEdit_->text();
  int parentId = addFolderDialog->foldersTree_->currentItem()->text(1).toInt();

  QSqlQuery q;

  // Determine row number for folder
  int rowToParent = 0;
  q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
         arg(parentId));
  if (q.next()) rowToParent = q.value(0).toInt();

  // Add feed to DB
  q.prepare("INSERT INTO feeds(text, created, parentId, rowToParent) "
            "VALUES (:text, :feedCreateTime, :parentId, :rowToParent)");
  q.bindValue(":text", folderText);
  q.bindValue(":feedCreateTime",
              QLocale::c().toString(QDateTime::currentDateTimeUtc(), "yyyy-MM-ddTHH:mm:ss"));
  q.bindValue(":parentId", parentId);
  q.bindValue(":rowToParent", rowToParent);
  q.exec();

  delete addFolderDialog;
  DatabaseBackup::subscriptionsChanged("add folder");

  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
  feedsModelReload();
  QApplication::restoreOverrideCursor();
}

/** @brief Delete feed list item with confirmation
 *---------------------------------------------------------------------------*/
void MainWindow::deleteItemFeedsTree()
{
  if (mainApp->isClosing() || !feedsView_->selectIndex().isValid()) return;

  QModelIndex currentIndex = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
  const int feedIdCur = feedsModel_->idByIndex(currentIndex);
  QModelIndexList indexes = feedsView_->selectionModel()->selectedRows(0);
  if (indexes.count() <= 1) {
    indexes.clear();
    indexes.append(feedsProxyModel_->mapFromSource(feedsView_->selectIndex()));
  }
  // Capture the confirmed IDs before the modal dialog's event loop can reload
  // the model. Never replay a destructive action against a later selection.
  QList<int> ids;
  for (const QModelIndex &proxyIndex : std::as_const(indexes)) {
    const QModelIndex index = feedsProxyModel_->mapToSource(proxyIndex);
    if (index.isValid()) ids.append(feedsModel_->idByIndex(index));
  }
  if (ids.isEmpty()) return;

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Question);
  msgBox.setWindowTitle(tr("Confirm Delete"));
  msgBox.setText(tr("Are you sure you want to delete the selected items?"));
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);
  if (msgBox.exec() != QMessageBox::Yes || mainApp->isClosing()) return;

  QString error;
  bool deleted = false;
  {
    auto databaseAccess = Database::tryAccess();
    if (!databaseAccess.owns_lock()) {
      error = tr("The database is busy. Please try deleting the selected items again shortly.");
    } else {
      QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
      deleted = FeedDeletion::apply(db_, ids, error);
      if (deleted) {
        feedsModelReload();
        currentIndex = feedsProxyModel_->mapFromSource(feedIdCur);
        feedsView_->setCurrentIndex(currentIndex);
        slotFeedClicked(currentIndex);
      }
      QApplication::restoreOverrideCursor();
    }
  } // Release database access before displaying an error dialog.
  if (!deleted) {
    if (error.isEmpty()) error = tr("The database operation failed.");
    qWarning().noquote() << "Feed deletion failed:" << error;
    QMessageBox failure(QMessageBox::Warning, tr("Delete failed"),
                        tr("Could not delete the selected items.\n\n%1").arg(error),
                        QMessageBox::Ok, this);
    failure.setTextFormat(Qt::PlainText);
    failure.exec();
    return;
  }
  DatabaseBackup::subscriptionsChanged("delete feeds or folders");
}

/** @brief Import feeds from OPML-file
 *
 * Calls open file system dialog with filter *.opml.
 * Adds all feeds to DB include hierarchy, ignore duplicate feeds
 *---------------------------------------------------------------------------*/
void MainWindow::slotImportFeeds()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select OPML File"),
                                                  QDir::homePath(),
                                                  QString(tr("OPML Files (*.%1 *.%2)"))
                                                  .arg("opml").arg("xml"));

  if (fileName.isNull()) {
    statusBar()->showMessage(tr("Import canceled"), 3000);
    return;
  }

  qDebug() << "import file:" << fileName;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Import: could not open file"), 3000);
    return;
  }

  QByteArray xmlData = file.readAll();
  file.close();

  xmlData = OpmlInput::prepare(xmlData);
  int httpCount = 0;
  QString error;
  if (!OpmlInput::inspect(xmlData, httpCount, error)) {
    QMessageBox::warning(this, tr("Import failed"), error);
    return;
  }
  bool upgradeHttp = true;
  if (httpCount > 0) {
    QMessageBox options(QMessageBox::Question, tr("Import feeds"),
        tr("This file contains %1 HTTP feed URLs. HTTPS will be tried without falling back to HTTP. "
           "Nonstandard ports are preserved; port 80 becomes the HTTPS default.").arg(httpCount),
        QMessageBox::Ok | QMessageBox::Cancel, this);
    auto *upgrade = new QCheckBox(tr("Upgrade HTTP feed URLs to HTTPS"), &options);
    upgrade->setChecked(true);
    options.setCheckBox(upgrade);
    options.setDefaultButton(QMessageBox::Ok);
    if (options.exec() != QMessageBox::Ok) return;
    upgradeHttp = upgrade->isChecked();
    if (!upgradeHttp && !FeedUrl::confirmImportHttp(this, httpCount)) return;
  }
  isStartImportFeed_ = true;
  emit signalImportFeeds(xmlData, upgradeHttp);
}

void MainWindow::slotImportFinished(bool updatesQueued)
{
  // Successful imports keep requesting favicons until their updates finish.
  if (!updatesQueued) isStartImportFeed_ = false;
}

/** @brief Export feeds to OPML-file
 *---------------------------------------------------------------------------*/
void MainWindow::slotExportFeeds()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Select OPML File"),
                                                  QDir::homePath(),
                                                  QString(tr("OPML Files (*.%1)"))
                                                  .arg("opml"));

  if (fileName.isNull()) {
    statusBar()->showMessage(tr("Export canceled"), 3000);
    return;
  }

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Export: could not open file"), 3000);
    return;
  }

  OpmlExporter::write(file, *feedsView_);

  file.close();
}
// ----------------------------------------------------------------------------
void MainWindow::slotFeedsViewportUpdate()
{
  feedsView_->viewport()->update();
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("unread"), QHeaderView::ResizeToContents);
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("undeleteCount"), QHeaderView::ResizeToContents);
  feedsView_->header()->setSectionResizeMode(feedsView_->columnIndex("updated"), QHeaderView::ResizeToContents);
}
// ----------------------------------------------------------------------------
void MainWindow::slotFeedCountsUpdate(FeedCountStruct counts)
{
  QModelIndex index = feedsModel_->indexById(counts.feedId);
  if (index.isValid()) {
    QModelIndex indexUnread   = feedsModel_->indexSibling(index, "unread");
    QModelIndex indexNew      = feedsModel_->indexSibling(index, "newCount");
    QModelIndex indexUndelete = feedsModel_->indexSibling(index, "undeleteCount");
    feedsModel_->setData(indexUnread, counts.unreadCount);
    feedsModel_->setData(indexNew, counts.newCount);
    feedsModel_->setData(indexUndelete, counts.undeleteCount);

    if (!counts.updated.isEmpty()) {
      QModelIndex indexUpdated  = feedsModel_->indexSibling(index, "updated");
      feedsModel_->setData(indexUpdated, counts.updated);
    }

    if (!counts.lastBuildDate.isEmpty()) {
      QModelIndex indexLastBuildDate = feedsModel_->indexSibling(index, "lastBuildDate");
      feedsModel_->setData(indexLastBuildDate, counts.lastBuildDate);
    }

    if (!counts.htmlUrl.isEmpty()) {
      QModelIndex indexHtmlUrl = feedsModel_->indexSibling(index, "htmlUrl");
      feedsModel_->setData(indexHtmlUrl, counts.htmlUrl);
    }

    if (!counts.title.isEmpty()) {
      QModelIndex indexTitle = feedsModel_->indexSibling(index, "title");
      feedsModel_->setData(indexTitle, counts.title);
    }
  }

  if (currentNewsTab &&
      currentNewsTab->type_ == NewsTabWidget::TabTypeFeed &&
      currentNewsTab->feedId_ == counts.feedId) {
    setStatusCounts(counts.unreadCount, counts.undeleteCount);
  }

  if (isStartImportFeed_ && !counts.xmlUrl.isEmpty()) {
    emit faviconRequestUrl(counts.htmlUrl, counts.xmlUrl);
  }
}

/** @brief Recalculate counters for specified categories
 * @details Processing DB data. Model "reselect()" needed.
 * @param categoriesList - categories identifiers list for processing
 *---------------------------------------------------------------------------*/
void MainWindow::recountFeedCategories(const QList<int> &categoriesList)
{
  QSqlQuery q;
  QString qStr;

  for (int categoryIdStart : std::as_const(categoriesList)) {
    if (categoryIdStart < 1) continue;

    int categoryId = categoryIdStart;
    // Process all parents
    while (0 < categoryId) {
      int unreadCount = -1;
      int undeleteCount = -1;
      int newCount = -1;

      // Calculate sum of all feeds with same parent
      qStr = QString("SELECT sum(unread), sum(undeleteCount), sum(newCount) "
                     "FROM feeds WHERE parentId=='%1'").arg(categoryId);
      q.exec(qStr);
      if (q.next()) {
        unreadCount   = q.value(0).toInt();
        undeleteCount = q.value(1).toInt();
        newCount = q.value(2).toInt();
      }

      if (unreadCount != -1) {
        qStr = QString("UPDATE feeds SET unread='%1', undeleteCount='%2', newCount='%3' WHERE id=='%4'").
            arg(unreadCount).arg(undeleteCount).arg(newCount).arg(categoryId);
        q.exec(qStr);
      }

      // go to next parent's parent
      qStr = QString("SELECT parentId FROM feeds WHERE id=='%1'").
          arg(categoryId);
      categoryId = 0;
      q.exec(qStr);
      if (q.next()) categoryId = q.value(0).toInt();
    }
  }
}
// ----------------------------------------------------------------------------
void MainWindow::recountCategoryCounts()
{
  if (mainApp->isClosing()) return;
  if (recountCategoryCountsOn_) {
    recountCategoryCountsPending_ = true;
    return;
  }

  if (!categoriesTree_->isVisible() || !stackedWidget_->count()) return;

  recountCategoryCountsOn_ = true;
  emit signalRecountCategoryCounts();
}

/** @brief Process recalculating categories counters
 *----------------------------------------------------------------------------*/
void MainWindow::slotRecountCategoryCounts(CategoryCounts counts)
{
  recountCategoryCountsOn_ = false;
  if (recountCategoryCountsPending_) {
    recountCategoryCountsPending_ = false;
    recountCategoryCounts();
  }
  if (!counts.valid || mainApp->isClosing()) return;

  const int allStarredCount = counts.starred;
  const int unreadStarredCount = counts.unreadStarred;
  const int deletedCount = counts.deleted;
  const auto &allCountList = counts.labels;
  const auto &unreadCountList = counts.unreadLabels;
  int allLabelCount = 0;
  int unreadLabelCount = 0;
  QFont font;
  QTreeWidgetItem *labelTreeItem = categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem);

  for (int i = 0; i < labelTreeItem->childCount(); i++) {
    int id = labelTreeItem->child(i)->text(2).toInt();
    QString countStr;
    if (!unreadCountList[id] && !allCountList[id])
      countStr = "";
    else
      countStr = QString("(%1/%2)").arg(unreadCountList[id]).arg(allCountList[id]);
    labelTreeItem->child(i)->setText(4, countStr);
    font = labelTreeItem->child(i)->font(0);
    if (unreadCountList[id])
      font.setBold(true);
    else
      font.setBold(false);
    labelTreeItem->child(i)->setFont(0, font);

    unreadLabelCount = unreadLabelCount + unreadCountList[id];
    allLabelCount = allLabelCount + allCountList[id];
  }

  QString countStr;
  if (!unreadStarredCount && !allStarredCount)
    countStr = "";
  else
    countStr = QString("(%1/%2)").arg(unreadStarredCount).arg(allStarredCount);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::StarredItem)->setText(4, countStr);
  font = categoriesTree_->topLevelItem(CategoriesTreeWidget::StarredItem)->font(0);
  if (unreadStarredCount)
    font.setBold(true);
  else
    font.setBold(false);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::StarredItem)->setFont(0, font);

  if (!deletedCount)
    countStr = "";
  else
    countStr = QString("(%1)").arg(deletedCount);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::DeletedItem)->setText(4, countStr);

  if (!unreadLabelCount && !allLabelCount)
    countStr = "";
  else
    countStr = QString("(%1/%2)").arg(unreadLabelCount).arg(allLabelCount);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->setText(4, countStr);
  font = categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->font(0);
  if (unreadLabelCount)
    font.setBold(true);
  else
    font.setBold(false);
  categoriesTree_->topLevelItem(CategoriesTreeWidget::LabelsItem)->setFont(0, font);

  NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(stackedWidget_->currentIndex()));
  if (widget && (widget->type_ > NewsTabWidget::TabTypeFeed) && (widget->type_ < NewsTabWidget::TabTypeDownloads)
      && categoriesTree_->currentIndex().isValid()) {
    int unreadCount = widget->getUnreadCount(categoriesTree_->currentItem()->text(4));
    int allCount = widget->newsModel_->rowCount();
    setStatusCounts(unreadCount, allCount);
  }
}

/** @brief Update feed view
 *
 * Slot is called by UpdateDelayer after some delay
 * @param feedId Feed identifier to update
 * @param changed Flag indicating that feed is updated indeed
 *---------------------------------------------------------------------------*/
void MainWindow::slotUpdateFeed(int feedId, bool changed, int newCount, bool finish)
{
  if (finish) {
    emit signalShowNotification();
    isStartImportFeed_ = false;
  }

  if (!changed) {
    emit signalNextUpdate(finish);
    return;
  }

  if (currentNewsTab) {
    if (currentNewsTab->type_ == NewsTabWidget::TabTypeFeed) {
      if ((feedId == currentNewsTab->feedId_) ||
          feedsModel_->isFeedInFolder(feedId, currentNewsTab->feedId_)) {
        if (!updateNewsTimer_.isActive())
          updateNewsTimer_.start();
        updateCurrentTabStatusCounts(feedId);
      }
    } else if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
      if (!updateNewsTimer_.isActive())
        updateNewsTimer_.start();
    }
  }

  // Action after new news has arrived: tray, sound
  if (!isActiveWindow() && (newCount > 0) &&
      (behaviorIconTray_ == CHANGE_ICON_TRAY)) {
    trayIconController_->showNewNewsIcon();
  }
  emit signalRefreshInfoTray();
  if (newCount > 0)
    emit signalPlaySoundNewNews();

  // Manage notifications
  bool showNotify = true;

  if (showNotifyInactiveApp_)
  {
    showNotify = !isActiveWindow();
  }

  if (!showNotify)
  {
    clearNotification();
  }

  if (newCount > 0)
  {
    bool bAddRecentNews = !onlySelectedFeeds_ || idFeedsNotifyList_.contains(feedId);
    bool bAddNewNews = bAddRecentNews && showNotify;

    if (bAddNewNews)
    {
      int newFeedIdIndex = newNews.idFeedList_.indexOf(feedId);

      if (-1 < newFeedIdIndex)
      {
        newNews.cntNewsList_[newFeedIdIndex] = newCount;
      }
      else
      {
        newNews.idFeedList_.append(feedId);
        newNews.cntNewsList_.append(newCount);
      }
    }

    if (bAddRecentNews)
    {
      int recentFeedIdIndex = recentNews.idFeedList_.indexOf(feedId);

      if (-1 < recentFeedIdIndex)
      {
        recentNews.cntNewsList_[recentFeedIdIndex] += newCount;
      }
      else
      {
        recentNews.idFeedList_.append(feedId);
        recentNews.cntNewsList_.append(newCount);
      }
    }
  }

  recountCategoryCounts();

  emit signalNextUpdate(finish);
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

/** @brief Process click in feed tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedClicked(QModelIndex index)
{
  deferredDatabaseUi_.erase(DeferredDatabaseUi::FeedSelection);
  deferredDatabaseUi_.erase(DeferredDatabaseUi::TabSelection);
  if (mainApp->isClosing() || feedsView_->selectionModel()->selectedRows(0).count() > 1) return;
  auto databaseAccess = Database::tryAccess();
  if (!databaseAccess.owns_lock()) {
    const int feedId = feedsModel_->idByIndex(feedsProxyModel_->mapToSource(index));
    deferDatabaseUi(DeferredDatabaseUi::FeedSelection, [this, feedId] {
      const QModelIndex selected = feedsView_->currentIndex();
      if (feedsModel_->idByIndex(feedsProxyModel_->mapToSource(selected)) == feedId)
        slotFeedClicked(selected);
    });
    return;
  }

  int feedIdCur = feedsModel_->idByIndex(feedsProxyModel_->mapToSource(index));

  if (stackedWidget_->count() && currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
  }

  // Search open tab containing this feed
  int indexTab = -1;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->feedId_ == feedIdCur) {
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
      QModelIndex currentIndex = feedsProxyModel_->mapFromSource(feedIdCur);
      feedsView_->setCurrentIndex(currentIndex);

      currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
      newsModel_ = currentNewsTab->newsModel_;
      newsView_ = currentNewsTab->newsView_;
    } else {
      if (stackedWidget_->count() && currentNewsTab->type_ != NewsTabWidget::TabTypeFeed) {
        setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_, FeedReadSwitchingFeed, currentNewsTab);
      } else {
        // Mark previous feed Read while switching to another feed
        setFeedRead(NewsTabWidget::TabTypeFeed, feedIdOld_, FeedReadSwitchingFeed, 0, feedIdCur);
      }

      categoriesTree_->setCurrentIndex(QModelIndex());
    }

    slotFeedSelected(feedsModel_->indexById(feedIdCur));
    feedsView_->repaint();
  } else if (indexTab != -1) {
    tabBar_->setCurrentIndex(indexTab);
  }
  feedIdOld_ = feedIdCur;
}

/** @brief Process feed choosing
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedSelected(QModelIndex index, bool createTab)
{
  int feedId = feedsModel_->idByIndex(index);
  int feedParId = feedsModel_->paridByIndex(index);

  // Open or create feed tab
  if (!stackedWidget_->count() || createTab) {
    NewsTabWidget *widget = new NewsTabWidget(this, NewsTabWidget::TabTypeFeed, feedId, feedParId);
    int indexTab = addTab(widget);

    createNewsTab(indexTab);

    if (indexTab == 0)
      currentNewsTab->closeButton_->setVisible(false);
    if (!index.isValid())
      currentNewsTab->setVisible(false);

    emit signalSetCurrentTab(indexTab);
  } else {
    currentNewsTab->type_ = NewsTabWidget::TabTypeFeed;
    currentNewsTab->feedId_ = feedId;
    currentNewsTab->feedParId_ = feedParId;
    currentNewsTab->setSettings(true, false);
    currentNewsTab->setVisible(index.isValid());
  }
  statusBarController_->setCountsVisible(index.isValid(), index.isValid());

  // Set icon for tab has opened
  bool isFeed = (index.isValid() && feedsModel_->isFolder(index)) ? false : true;

  QPixmap iconTab;
  QByteArray byteArray = feedsModel_->dataField(index, "image").toByteArray();
  if (!isFeed) {
    iconTab.load(":/images/folder");
  } else {
    if (byteArray.isNull() || defaultIconFeeds_) {
      iconTab.load(":/images/feed");
    } else if (isFeed) {
      iconTab.loadFromData(QByteArray::fromBase64(byteArray));
    }
  }
  currentNewsTab->newsIconTitle_->setPixmap(iconTab);

  // Set title for tab has opened
  currentNewsTab->setTextTab(feedsModel_->dataField(index, "text").toString());

  feedProperties_->setEnabled(index.isValid());

  setNewsFilter(newsFilterGroup_->checkedAction(), false);

  // Search feed news that displayed before
  int newsRow = -1;
  if (openingFeedAction_ == 0) {
    QModelIndex feedIndex = feedsModel_->indexById(feedId);
    int newsIdCur = feedsModel_->dataField(feedIndex, "currentNews").toInt();
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);

    if (!indexList.isEmpty()) newsRow = indexList.first().row();
  } else if (openingFeedAction_ == 1) {
    newsRow = 0;
  } else if ((openingFeedAction_ == 3) || (openingFeedAction_ == 4)) {
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("read"));
    QModelIndexList indexList;
    if ((newsView_->header()->sortIndicatorOrder() == Qt::DescendingOrder) &&
        (openingFeedAction_ != 4))
      indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
    else
      indexList = newsModel_->match(index, Qt::EditRole, 0);

    if (!indexList.isEmpty()) newsRow = indexList.last().row();
  }

  // Focus feed news that displayed before
  newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
  if (newsRow == -1) newsView_->verticalScrollBar()->setValue(newsRow);

  if ((openingFeedAction_ != 2) && openNewsWebViewOn_) {
    currentNewsTab->slotNewsViewSelected(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
  } else {
    currentNewsTab->slotNewsViewSelected(newsModel_->index(-1, newsModel_->fieldIndex("title")));
    int newsId = newsModel_->index(newsRow, newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();
    QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").arg(newsId).arg(feedId);
    mainApp->sqlQueryExec(qStr);
    QModelIndex feedIndex = feedsModel_->indexById(feedId);
    feedsModel_->setData(feedsModel_->indexSibling(feedIndex, "currentNews"), newsId);
  }
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
void MainWindow::initUpdateFeeds()
{
  QSqlQuery q;

  if (onlySelectedFeeds_) {
    q.exec("SELECT feedId FROM feeds_ex WHERE value=1 AND name='showNotification'");
    while (q.next()) {
      idFeedsNotifyList_.append(q.value(0).toInt());
    }
  }

  updateScheduleClock_.start();

  updateFeedsTimer_ = new QTimer(this);
  connect(updateFeedsTimer_, &QTimer::timeout, this, &MainWindow::slotGetFeedsTimer);
  updateFeedsTimer_->start(1000);
}
// ----------------------------------------------------------------------------
void MainWindow::slotScheduledFeedsChecked()
{
  scheduledUpdatePending_ = false;
}

void MainWindow::slotGetFeedsTimer()
{
  // Permit startup to settle, including when intervals are shorter than a minute.
  // Explicit startup/manual updates still use their existing immediate paths.
  if (mainApp->isClosing() || updateScheduleClock_.elapsed() < 60000 || scheduledUpdatePending_)
    return;
  const int multiplier = updateFeedsIntervalType_ == -1 ? 1 :
                         (updateFeedsIntervalType_ == 0 ? 60 : 3600);
  scheduledUpdatePending_ = true;
  emit signalGetScheduledFeeds(updateFeedsEnable_, updateFeedsInterval_ * multiplier);
}
/** @brief Process update feed action
 *---------------------------------------------------------------------------*/
QModelIndexList MainWindow::selectedFeedIndexes() const
{
  QModelIndexList indexes = feedsView_->selectionModel()->selectedRows(0);
  if (indexes.count() <= 1) {
    indexes.clear();
    indexes.append(feedsProxyModel_->mapFromSource(feedsView_->selectIndex()));
  }
  return indexes;
}

void MainWindow::updateFeedActionText()
{
  bool force = false;
  for (const QModelIndex &proxyIndex : selectedFeedIndexes()) {
    const QModelIndex index = feedsProxyModel_->mapToSource(proxyIndex);
    if (index.isValid() && !feedsModel_->isFolder(index) &&
        feedsModel_->dataField(index, "disableUpdate").toBool()) {
      force = true;
      break;
    }
  }
  updateFeedAct_->setText(force ? tr("Force Update") : tr("Update Feed"));
  updateFeedAct_->setToolTip(force
      ? tr("Update selected feeds once, including disabled feeds")
      : tr("Update Current Feed"));
}

void MainWindow::slotGetFeed()
{
  const QModelIndexList indexes = selectedFeedIndexes();
  QList<int> ids;
  QSet<int> explicitFeeds;
  // Collect direct selections first: they override a selected folder's policy,
  // regardless of tree order, and each feed must be dispatched only once.
  for (const QModelIndex &proxyIndex : indexes) {
    const QModelIndex index = feedsProxyModel_->mapToSource(proxyIndex);
    if (!index.isValid() || feedsModel_->isFolder(index)) continue;
    const int id = feedsModel_->idByIndex(index);
    if (!explicitFeeds.contains(id)) ids.append(id);
    explicitFeeds.insert(id);
  }
  for (const QModelIndex &proxyIndex : indexes) {
    const QModelIndex index = feedsProxyModel_->mapToSource(proxyIndex);
    if (!index.isValid() || !feedsModel_->isFolder(index)) continue;
    const QList<int> children = UpdateObject::getIdFeedsInList(db_, feedsModel_->idByIndex(index));
    for (int id : children) {
      if (!ids.contains(id)) ids.append(id);
    }
  }
  for (int id : ids) {
    const QModelIndex index = feedsModel_->indexById(id);
    if (!index.isValid()) continue;
    const bool force = explicitFeeds.contains(id);
    if (!force && feedsModel_->dataField(index, "disableUpdate").toBool()) continue;
    emit signalGetFeed(id, feedsModel_->dataField(index, "xmlUrl").toString(),
                      feedsModel_->dataField(index, "lastBuildDate").toDateTime(),
                      feedsModel_->dataField(index, "authentication").toInt(), force);
  }
}

/** @brief Process update all feeds action
 *---------------------------------------------------------------------------*/
void MainWindow::slotGetAllFeeds()
{
  emit signalGetAllFeeds();
}

void MainWindow::slotStopUpdate()
{
  statusBarController_->hideProgress();
  emit signalStopUpdate();
}

void MainWindow::startFeedUpdateCycle()
{
  playSoundNewNews_ = false;
}

/** @brief Show update progress bar after feed update has started
 *---------------------------------------------------------------------------*/
void MainWindow::showProgressBar(int maximum)
{
  if (maximum == 0) {
    isStartImportFeed_ = false;
    return;
  }

  Settings settings;
  settings.setValue("Flags/updatingFeeds", true);
}
void MainWindow::queueFeedProgress(int feedId, QString name)
{
  if (!mainApp->isClosing()) statusBarController_->queueFeed(feedId, name);
}
void MainWindow::setFeedProgressStage(int feedId, QString stage)
{
  if (!mainApp->isClosing()) statusBarController_->setFeedStage(feedId, stage);
}
void MainWindow::finishFeedProgress(int feedId)
{
  if (!mainApp->isClosing()) statusBarController_->finishFeed(feedId);
}
void MainWindow::showMessageStatusBar(QString message, int timeout)
{
  statusBarController_->showMessage(message, timeout);
}
void MainWindow::slotCountsStatusBar(int unreadCount, int allCount)
{
  setStatusCounts(unreadCount, allCount);
}

void MainWindow::setStatusCounts(int unreadCount, int allCount)
{
  statusBarController_->setCountTexts(
        QString(" " + tr("Unread: %1") + " ").arg(unreadCount),
        QString(" " + tr("All: %1") + " ").arg(allCount));
}

void MainWindow::updateCurrentTabStatusCounts(int updatedFeedId)
{
  if (!currentNewsTab || currentNewsTab->type_ != NewsTabWidget::TabTypeFeed ||
      currentNewsTab->feedId_ <= 0) {
    return;
  }
  const int tabFeedId = currentNewsTab->feedId_;
  if (updatedFeedId > 0 && updatedFeedId != tabFeedId &&
      !feedsModel_->isFeedInFolder(updatedFeedId, tabFeedId)) {
    return;
  }
  const QModelIndex tabIndex = feedsModel_->indexById(tabFeedId);
  if (tabIndex.isValid()) {
    setStatusCounts(feedsModel_->dataField(tabIndex, "unread").toInt(),
                    feedsModel_->dataField(tabIndex, "undeleteCount").toInt());
  }
}
// ----------------------------------------------------------------------------
void MainWindow::slotVisibledFeedsWidget()
{
  if (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) {
    showFeedsTabPermanent_ = feedsWidgetVisibleAct_->isChecked();
  }

  feedsWidget_->setVisible(feedsWidgetVisibleAct_->isChecked());
  updateIconToolBarNull(feedsWidgetVisibleAct_->isChecked());
}
// ----------------------------------------------------------------------------
void MainWindow::updateIconToolBarNull(bool feedsWidgetVisible)
{
  if (feedsWidgetVisible)
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleR.png"));
  else
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleL.png"));
}

/** @brief Update status of current feed or feed of current tab
 *---------------------------------------------------------------------------*/
void MainWindow::slotUpdateStatus(int feedId, bool changed)
{
  if (!changed && feedId > 0) {
    updateCurrentTabStatusCounts(feedId);
  }
  emit signalUpdateStatus(feedId, changed);
}

/** @brief Set filter for viewing feeds and categories
 * @param pAct Filter mode
 * @param clicked Flag to call function after user click or from programm code
 *    true  - user click
 *    false - programm call
 *----------------------------------------------------------------------------*/
void MainWindow::setFeedsFilter(bool clicked)
{
  QList<int> idList;
  static bool setFilter = false;

  if (setFilter) return;
  setFilter = true;

  QAction* filterAct = feedsFilterGroup_->checkedAction();

  if (filterAct->objectName() == "filterFeedsNew_") {
    QModelIndex index = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
    int newCount = feedsModel_->dataField(index, "newCount").toInt();
    if (!(clicked && !newCount)) {
      while (index.isValid()) {
        idList << feedsModel_->idByIndex(index);
        index = index.parent();
      }
    }
  } else if (filterAct->objectName() == "filterFeedsUnread_") {
    QModelIndex index = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
    int unRead = feedsModel_->dataField(index, "unread").toInt();
    if (!(clicked && !unRead)) {
      while (index.isValid()) {
        idList << feedsModel_->idByIndex(index);
        index = index.parent();
      }
    }
  } else if (filterAct->objectName() == "filterFeedsStarred_") {
    QSqlQuery q;
    q.exec(QString("SELECT id, parentId FROM feeds WHERE label LIKE '%starred%'"));
    while (q.next()) {
      idList.append(q.value(0).toInt());
      int parentId = q.value(1).toInt();
      if (!idList.contains(parentId)) {
        while (parentId) {
          if (!idList.contains(parentId))
            idList.append(parentId);
          else break;
          QSqlQuery q1;
          q1.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(parentId));
          if (q1.next()) parentId = q1.value(0).toInt();
        }
      }
    }
  } else if (filterAct->objectName() == "filterFeedsError_") {
    QSqlQuery q;
    q.exec(QString("SELECT id, parentId FROM feeds WHERE status!=0 AND status!=''"));
    while (q.next()) {
      idList.append(q.value(0).toInt());
      int parentId = q.value(1).toInt();
      if (!idList.contains(parentId)) {
        while (parentId) {
          if (!idList.contains(parentId))
            idList.append(parentId);
          else break;
          QSqlQuery q1;
          q1.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(parentId));
          if (q1.next()) parentId = q1.value(0).toInt();
        }
      }
    }
  }

  if ((filterAct->objectName() == "filterFeedsNew_") ||
      (filterAct->objectName() == "filterFeedsUnread_")) {
    for (int i = 0; i < stackedWidget_->count(); i++) {
      if ((i == 0) && clicked && (stackedWidget_->currentIndex() == 0)) continue;
      NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
      if (!idList.contains(widget->feedId_)) {
        idList.append(widget->feedId_);
        QModelIndex index = feedsModel_->indexById(widget->feedId_).parent();
        while (index.isValid()) {
          int id = feedsModel_->idByIndex(index);
          if (!idList.contains(id))
            idList.append(id);
          index = index.parent();
        }
      }
    }
  }

  // Set filter
  feedsProxyModel_->setFilter(filterAct->objectName(), idList,
                              findFeeds_->findGroup_->checkedAction()->objectName(),
                              findFeeds_->text());

  feedsView_->restoreExpanded();

  feedsView_->clearSelection();
  feedsView_->setCurrentIndex(feedsView_->currentIndex());

  if (clicked && (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT)) {
    slotFeedClicked(feedsView_->currentIndex());
  }

  if (filterAct->objectName() == "filterFeedsAll_") feedsFilter_->setIcon(QIcon(":/images/filterOff"));
  else feedsFilter_->setIcon(QIcon(":/images/filterOn"));

  // Store filter to enable it as "last used filter"
  if (filterAct->objectName() != "filterFeedsAll_")
    feedsFilterAction_ = filterAct;

  setFilter = false;
}

/** @brief Mark feed read
 *---------------------------------------------------------------------------*/
void MainWindow::markFeedRead()
{
  bool openFeed = false;
  bool isFolder = false;

  QModelIndexList indexList = feedsView_->selectionModel()->selectedRows(0);
  if (indexList.count() <= 1) {
    indexList.clear();
    indexList.append(feedsProxyModel_->mapFromSource(feedsView_->selectIndex()));
  }
  QList<int> idList;
  for (int i = indexList.count()-1; i >= 0; --i) {
    QModelIndex index = feedsProxyModel_->mapToSource(indexList[i]);
    if (feedsModel_->isFolder(index)) {
      idList.append(feedsModel_->dataField(index, "id").toInt());
      indexList.removeAt(i);
    }
  }
  for (int i = indexList.count()-1; i >= 0; --i) {
    QModelIndex index = feedsProxyModel_->mapToSource(indexList[i]);
    int parentId = feedsModel_->dataField(index, "parentId").toInt();
    if (!idList.contains(parentId)) {
      idList.append(feedsModel_->dataField(index, "id").toInt());
    }
    indexList.removeAt(i);
  }
  for (int id : std::as_const(idList)) {
    bool openFeedT = false;
    QModelIndex index = feedsModel_->indexById(id);
    int parentId = feedsModel_->dataField(index, "parentId").toInt();
    if (currentNewsTab->feedId_ == id) {
      openFeedT = true;
      openFeed = true;
    }
    if (currentNewsTab->feedParId_ == id) {
      openFeed = true;
    }
    if (currentNewsTab->feedId_ == parentId) {
      openFeed = true;
      isFolder = true;
    }
    if (feedsModel_->isFolder(index)) {
      if (currentNewsTab->feedId_ == id) {
        isFolder = true;
      }
      QList<int> list = UpdateObject::getIdFeedsInList(db_, id);
      for (int id1 : std::as_const(list)) {
        QModelIndex index1 = feedsModel_->indexById(id1);
        QModelIndex indexUnread = feedsModel_->indexSibling(index1, "unread");
        QModelIndex indexNew    = feedsModel_->indexSibling(index1, "newCount");
        feedsModel_->setData(indexUnread, 0);
        feedsModel_->setData(indexNew, 0);

        if (!openFeed) {
          int parentId1 = feedsModel_->dataField(index1, "parentId").toInt();
          if ((currentNewsTab->feedId_ == id1) || (currentNewsTab->feedId_ == parentId1)) {
            openFeed = true;
          }
        }
      }
    }

    QModelIndex indexUnread = feedsModel_->indexSibling(index, "unread");
    QModelIndex indexNew    = feedsModel_->indexSibling(index, "newCount");
    feedsModel_->setData(indexUnread, 0);
    feedsModel_->setData(indexNew, 0);
    emit signalMarkFeedRead(id, feedsModel_->isFolder(index), openFeedT);
  }

  // Update feed view with focus
  if (openFeed) {
    if ((tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) && !isFolder) {
      signalRefreshNewsView(1);
    } else {
      signalRefreshNewsView(0);
      recountCategoryCounts();
      emit signalSetFeedsFilter(true);
    }
  }
  // Update feeds view without focus
  else {
    recountCategoryCounts();
    emit signalSetFeedsFilter();
  }
}

// ----------------------------------------------------------------------------
void MainWindow::slotShowAboutDlg()
{
  AboutDialog *aboutDialog = new AboutDialog(this);
  aboutDialog->exec();
  delete aboutDialog;
}

/** @brief Call context menu of the feeds tree
 *----------------------------------------------------------------------------*/
void MainWindow::showContextMenuFeed(const QPoint &pos)
{
  slotFeedMenuShow();

  QModelIndex index = feedsView_->indexAt(pos);
  if (index.isValid()) {
    QRect rectText = feedsView_->visualRect(index);
    if (pos.x() >= rectText.x()) {
      QMenu menu;
      menu.addAction(addAct_);
      menu.addSeparator();
      menu.addAction(openFeedNewTabAct_);
      menu.addSeparator();
      menu.addAction(updateFeedAct_);
      menu.addSeparator();
      menu.addAction(markFeedRead_);
      menu.addSeparator();
      menu.addAction(deleteFeedAct_);
      menu.addSeparator();
      menu.addAction(setFilterNewsAct_);
      menu.addAction(feedProperties_);

      menu.exec(feedsView_->viewport()->mapToGlobal(pos));
    }
  } else {
    QMenu menu;
    menu.addAction(addAct_);

    menu.exec(feedsView_->viewport()->mapToGlobal(pos));
  }

  index = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
  feedsView_->selectId_ = feedsModel_->idByIndex(index);
  updateFeedActionText();

  feedProperties_->setEnabled(feedsView_->selectIndex().isValid());
}
// ----------------------------------------------------------------------------
void MainWindow::slotFeedMenuShow()
{
  updateFeedActionText();
  feedProperties_->setEnabled(feedsView_->selectIndex().isValid());
}

/** @brief Restore feeds state on application startup
 *---------------------------------------------------------------------------*/
void MainWindow::restoreFeedsOnStartUp()
{
  qApp->processEvents();

  //* Restore current feed
  QModelIndex feedIndex = QModelIndex();
  if (reopenFeedStartup_) {
    Settings settings;
    int feedId = settings.value("feedSettings/currentId", 0).toInt();
    feedIndex = feedsProxyModel_->mapFromSource(feedId);
  }
  feedsView_->setCurrentIndex(feedIndex);
  updateCurrentTab_ = false;
  slotFeedClicked(feedIndex);
  currentNewsTab->articleView_->setFocus();
  updateCurrentTab_ = true;

  slotUpdateStatus(-1, false);
  recountCategoryCounts();

  // Open feeds in tabs
  QSqlQuery q;
  q.exec(QString("SELECT id, parentId FROM feeds WHERE displayOnStartup=1"));
  while (q.next()) {
    creatFeedTab(q.value(0).toInt(), q.value(1).toInt());
  }
}
// ----------------------------------------------------------------------------
void MainWindow::slotFeedsFilter()
{
  if (feedsFilterGroup_->checkedAction()->objectName() == "filterFeedsAll_") {
    if (feedsFilterAction_ != nullptr) {
      feedsFilterAction_->setChecked(true);
      setFeedsFilter();
    } else {
      if (mainToolbar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = mainToolbar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
        }
      }
      if (feedsToolBar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = feedsToolBar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
        }
      }
      if (currentNewsTab->newsToolBar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
        }
      }
    }
  } else {
    filterFeedsAll_->setChecked(true);
    setFeedsFilter();
  }
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

/** @brief Prepare feed icon for storing in DB
 *---------------------------------------------------------------------------*/
void MainWindow::slotIconFeedPreparing(QString feedUrl, QByteArray byteArray,
                                       QString format)
{
  QPixmap icon;
  if (icon.loadFromData(byteArray)) {
    icon = icon.scaled(16, 16, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
    QByteArray faviconData;
    QBuffer    buffer(&faviconData);
    buffer.open(QIODevice::WriteOnly);
    if (icon.save(&buffer, "ICO")) {
      emit signalIconFeedReady(feedUrl, faviconData);
    }
  } else if (icon.loadFromData(byteArray, format.toUtf8().data())) {
    icon = icon.scaled(16, 16, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
    QByteArray faviconData;
    QBuffer    buffer(&faviconData);
    buffer.open(QIODevice::WriteOnly);
    if (icon.save(&buffer, "ICO")) {
      emit signalIconFeedReady(feedUrl, faviconData);
    }
  }
}

/** @brief Update feed icon in model and view
 *---------------------------------------------------------------------------*/
void MainWindow::slotIconFeedUpdate(int feedId, QByteArray faviconData)
{
  QModelIndex index = feedsModel_->indexById(feedId);
  if (index.isValid()) {
    QModelIndex indexImage = feedsModel_->indexSibling(index, "image");
    feedsModel_->setData(indexImage, faviconData.toBase64());
    feedsView_->viewport()->update();
  }

  if (defaultIconFeeds_) return;

  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->feedId_ == feedId) {
      QPixmap iconTab;
      if (!faviconData.isNull()) {
        iconTab.loadFromData(faviconData);
      } else {
        iconTab.load(":/images/feed");
      }
      widget->newsIconTitle_->setPixmap(iconTab);
    }
  }
  if (currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads)
    currentNewsTab->newsView_->viewport()->update();
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

/** @brief Process Key_Up in feeds tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedUpPressed()
{
  feedsView_->navigateUp();
}
/** @brief Process Key_Down in feeds tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedDownPressed()
{
  feedsView_->navigateDown();
}
/** @brief Process previous feed shortcut
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedPrevious()
{
  feedsView_->navigatePrevious();
}
/** @brief Process next feed shortcut
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedNext()
{
  feedsView_->navigateNext();
}
/** @brief Process Key_Home in feeds tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedHomePressed()
{
  feedsView_->navigateHome();
}
/** @brief Process Key_End in feeds tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotFeedEndPressed()
{
  feedsView_->navigateEnd();
}
/** @brief Process pressing PageUp-key in feeds tree
 *----------------------------------------------------------------------------*/
void MainWindow::slotFeedPageUpPressed()
{
  feedsView_->navigatePageUp();
}
/** @brief Process pressing PageDown-key in feeds tree
 *----------------------------------------------------------------------------*/
void MainWindow::slotFeedPageDownPressed()
{
  feedsView_->navigatePageDown();
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

/** @brief Manage displaying columns in feed tree
 *---------------------------------------------------------------------------*/
void MainWindow::feedsColumnVisible(QAction *action)
{
  int idx = action->data().toInt();
  if (action->isChecked())
    feedsView_->showColumn(idx);
  else
    feedsView_->hideColumn(idx);
}

/** @brief Reload full model
 * @details Performs: reload model, reset proxy model, restore focus
 *---------------------------------------------------------------------------*/
void MainWindow::feedsModelReload(bool checkFilter)
{
  if (checkFilter) {
    if (feedsFilterGroup_->checkedAction()->objectName() != "filterFeedsAll_") {
      setFeedsFilter(false);
    }
    slotFeedsViewportUpdate();
    return;
  }

  if (mainApp->isClosing()) return;
  auto databaseAccess = Database::tryAccess();
  if (!databaseAccess.owns_lock()) {
    deferDatabaseUi(DeferredDatabaseUi::FeedsReload, [this] { feedsModelReload(false); });
    return;
  }
  deferredDatabaseUi_.erase(DeferredDatabaseUi::FeedsReload);

  int topRow = feedsView_->verticalScrollBar()->value();
  QModelIndex feedIndex = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
  int feedId = feedsModel_->idByIndex(feedIndex);

  feedsView_->refresh();

  feedIndex = feedsProxyModel_->mapFromSource(feedId);
  feedsView_->selectIdEn_ = false;
  feedsView_->setCurrentIndex(feedIndex);
  feedsView_->verticalScrollBar()->setValue(topRow);
  updateCurrentTabStatusCounts();
}
// ----------------------------------------------------------------------------
void MainWindow::slotFindFeeds(QString)
{
  if (!findFeedsWidget_->isVisible()) return;

  setFeedsFilter(false);
}
// ----------------------------------------------------------------------------
void MainWindow::slotSelectFind()
{
  slotFindFeeds(findFeeds_->text());
}
// ----------------------------------------------------------------------------
void MainWindow::findFeedVisible(bool visible)
{
  findFeedsWidget_->setVisible(visible);
  if (visible) {
    findFeeds_->setFocus();
  } else {
    findFeedAct_->setChecked(false);
    findFeeds_->clear();
    // Call filter explicitly, because invisible widget don't calls it
    setFeedsFilter(false);
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

/** @brief Move index after drag&drop operations
 * @param indexWhat index that is moving
 * @param indexWhere index, where to move
 *---------------------------------------------------------------------------*/
void MainWindow::slotMoveIndex(const QModelIndex &indexWhere, int how)
{
  feedsView_->setCursor(Qt::WaitCursor);

  QModelIndexList indexList = feedsView_->selectionModel()->selectedRows(0);
  for (int i = 0; i < indexList.count(); i++) {
    QModelIndex indexWhat = feedsProxyModel_->mapToSource(indexList[i]);
    int feedIdWhat = feedsModel_->idByIndex(indexWhat);
    int feedParIdWhat = feedsModel_->paridByIndex(indexWhat);
    int feedIdWhere = feedsModel_->idByIndex(indexWhere);
    int feedParIdWhere = feedsModel_->paridByIndex(indexWhere);

    // Repair rowToParent
    QSqlQuery q;
    if (how == 2) {
      // Move to another folder
      QList<int> idList;
      q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
             arg(feedParIdWhat));
      while (q.next()) {
        if (feedIdWhat != q.value(0).toInt())
          idList << q.value(0).toInt();
      }
      for (int i = 0; i < idList.count(); i++) {
        q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
               arg(i).arg(idList.at(i)));
      }

      int rowToParent = 0;
      q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
             arg(feedIdWhere));
      if (q.next()) rowToParent = q.value(0).toInt();

      q.exec(QString("UPDATE feeds SET parentId='%1', rowToParent='%2' WHERE id=='%3'").
             arg(feedIdWhere).arg(rowToParent).arg(feedIdWhat));

      QList<int> categoriesList;
      categoriesList << feedParIdWhat << feedIdWhere;
      recountFeedCategories(categoriesList);
    } else if (feedParIdWhat == feedParIdWhere) {
      // Move inside folder
      QList<int> idList;
      q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
             arg(feedParIdWhat));
      while (q.next()) {
        idList << q.value(0).toInt();
      }

      int rowWhat = feedsModel_->dataField(indexWhat, "rowToParent").toInt();
      int rowWhere = feedsModel_->dataField(indexWhere, "rowToParent").toInt();
      if ((rowWhat < rowWhere) && (how != 1)) rowWhere--;
      else if (how == 1) rowWhere++;
      idList.insert(rowWhere, idList.takeAt(rowWhat));

      for (int i = 0; i < idList.count(); i++) {
        q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
               arg(i).arg(idList.at(i)));
      }
    } else {
      // Move in another folder beside feeds
      QList<int> idList;
      q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
             arg(feedParIdWhat));
      while (q.next()) {
        if (feedIdWhat != q.value(0).toInt())
          idList << q.value(0).toInt();
      }
      for (int i = 0; i < idList.count(); i++) {
        q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
               arg(i).arg(idList.at(i)));
      }

      //

      idList.clear();
      q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
             arg(feedParIdWhere));
      while (q.next()) {
        idList << q.value(0).toInt();
      }

      int rowWhere = feedsModel_->dataField(indexWhere, "rowToParent").toInt();
      if (how == 1) rowWhere++;
      idList.insert(rowWhere, feedIdWhat);

      for (int i = 0; i < idList.count(); i++) {
        q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
               arg(i).arg(idList.at(i)));
      }

      q.exec(QString("UPDATE feeds SET parentId='%1' WHERE id=='%2'").
             arg(feedParIdWhere).arg(feedIdWhat));

      QList<int> categoriesList;
      categoriesList << feedParIdWhat << feedParIdWhere;
      recountFeedCategories(categoriesList);
    }
  }

  feedsView_->refresh();

  feedsView_->setCurrentIndex(feedsProxyModel_->mapFromSource(feedIdOld_));

  feedsView_->setCursor(Qt::ArrowCursor);
  DatabaseBackup::subscriptionsChanged("move feeds or folders");
}

/** @brief Process clicks in feeds tree
 * @param item Item that was clicked
 *---------------------------------------------------------------------------*/
void MainWindow::slotCategoriesClicked(QTreeWidgetItem *item, int, bool createTab)
{
  deferredDatabaseUi_.erase(DeferredDatabaseUi::FeedSelection);
  deferredDatabaseUi_.erase(DeferredDatabaseUi::TabSelection);
  if (mainApp->isClosing() || !item) return;
  auto databaseAccess = Database::tryAccess();
  if (!createTab && !databaseAccess.owns_lock()) {
    const QPersistentModelIndex selected(categoriesTree_->currentIndex());
    deferDatabaseUi(DeferredDatabaseUi::FeedSelection, [this, selected] {
      if (selected.isValid() && categoriesTree_->currentIndex() == selected)
        slotCategoriesClicked(categoriesTree_->currentItem(), 0);
    });
    return;
  }

  if (stackedWidget_->count() && currentNewsTab->type_ < NewsTabWidget::TabTypeDownloads) {
    currentNewsTab->newsHeader_->saveStateColumns(currentNewsTab);
    Settings settings;
    settings.setValue("NewsTabSplitterState", currentNewsTab->newsTabWidgetSplitter_->saveState());
  }

  int type = item->text(1).toInt();
  NewsTabWidget::TabType tabType = static_cast<NewsTabWidget::TabType>(type);

  int indexTab = -1;
  if (!createTab) {
    for (int i = 0; i < stackedWidget_->count(); i++) {
      NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
      if (widget->type_ == tabType) {
        if (widget->type_ == NewsTabWidget::TabTypeLabel) {
          if (widget->labelId_ == item->text(2).toInt()) {
            indexTab = i;
            break;
          }
        }
        else {
          indexTab = i;
          break;
        }
      }
    }
  }

  if (indexTab == -1) {
    if (createTab) {
      setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_,
                  FeedReadSwitchingTab, currentNewsTab);

      NewsTabWidget *widget = new NewsTabWidget(this, tabType);
      indexTab = addTab(widget);
      createNewsTab(indexTab);
    }
    else {
      feedsView_->setCurrentIndex(QModelIndex());
      feedProperties_->setEnabled(false);

      if (tabBar_->currentIndex() != TAB_WIDGET_PERMANENT) {
        setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_,
                    FeedReadSwitchingTab, currentNewsTab);

        QModelIndex curIndex = categoriesTree_->currentIndex();
        updateCurrentTab_ = false;
        tabBar_->setCurrentIndex(TAB_WIDGET_PERMANENT);
        updateCurrentTab_ = true;
        categoriesTree_->setCurrentIndex(curIndex);

        currentNewsTab = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
        newsModel_ = currentNewsTab->newsModel_;
        newsView_ = currentNewsTab->newsView_;
      } else {
        setFeedRead(currentNewsTab->type_, currentNewsTab->feedId_,
                    FeedReadSwitchingFeed, currentNewsTab);
      }

      currentNewsTab->type_ = tabType;
      currentNewsTab->feedId_ = -1;
      currentNewsTab->feedParId_ = -1;
      currentNewsTab->setSettings(true, false);
      currentNewsTab->setVisible(true);
    }

    // Set icon and title of current tab
    currentNewsTab->newsIconTitle_->setPixmap(item->icon(0).pixmap(16,16));
    currentNewsTab->setTextTab(item->text(0));

    currentNewsTab->labelId_ = item->text(2).toInt();

    switch (type) {
    case NewsTabWidget::TabTypeUnread:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 0 AND read < 2";
      break;
    case NewsTabWidget::TabTypeStar:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 0 AND starred = 1";
      break;
    case NewsTabWidget::TabTypeDel:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 1";
      break;
    case NewsTabWidget::TabTypeLabel:
      if (currentNewsTab->labelId_ != 0) {
        currentNewsTab->categoryFilterStr_ =
            QString("feedId > 0 AND deleted = 0 AND label LIKE '%,%1,%'").
            arg(currentNewsTab->labelId_);
      } else {
        currentNewsTab->categoryFilterStr_ =
            QString("feedId > 0 AND deleted = 0 AND label!='' AND label!=','");
      }
      break;
    }
    // ... add filter from "search"
    QString filterStr = currentNewsTab->categoryFilterStr_;
    currentNewsTab->appendSearchFilter(filterStr);
    newsModel_->setFilter(filterStr);

    if (newsModel_->rowCount() != 0) {
      while (newsModel_->canFetchMore())
        newsModel_->fetchMore();
    }

    if (type == NewsTabWidget::TabTypeDel){
      currentNewsTab->newsHeader_->setSortIndicator(newsModel_->fieldIndex("deleteDate"),
                                                    Qt::DescendingOrder);
    }

    currentNewsTab->loadNewspaper();

    // Search previous displayed news of the feed
    int newsRow = -1;
    if (openingFeedAction_ == 0) {
      int newsIdCur = item->text(3).toInt();
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
      QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);

      if (!indexList.isEmpty()) newsRow = indexList.first().row();
    } else if (openingFeedAction_ == 1) {
      newsRow = 0;
    } else if (openingFeedAction_ == 3) {
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("read"));
      QModelIndexList indexList;
      if (newsView_->header()->sortIndicatorOrder() == Qt::DescendingOrder)
        indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
      else
        indexList = newsModel_->match(index, Qt::EditRole, 0);

      if (!indexList.isEmpty()) newsRow = indexList.last().row();
    }

    // Display previous displayed news of the feed
    newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    if (newsRow == -1) newsView_->verticalScrollBar()->setValue(newsRow);

    if ((openingFeedAction_ != 2) && openNewsWebViewOn_) {
      currentNewsTab->slotNewsViewSelected(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    } else {
      currentNewsTab->slotNewsViewSelected(newsModel_->index(-1, newsModel_->fieldIndex("title")));
    }

    if (createTab)
      emit signalSetCurrentTab(indexTab);
  } else {
    emit signalSetCurrentTab(indexTab, true);
  }

  int unreadCount = currentNewsTab->getUnreadCount(categoriesTree_->currentItem()->text(4));
  int allCount = currentNewsTab->newsModel_->rowCount();
  setStatusCounts(unreadCount, allCount);

  statusBarController_->setCountsVisible(
        currentNewsTab->type_ != NewsTabWidget::TabTypeDel, true);
}

void MainWindow::clearDeleted()
{
  QSqlQuery q;
  q.exec("UPDATE news SET description='', content='', received='', "
         "author_name='', author_uri='', author_email='', "
         "category='', new='', read='', starred='', label='', "
         "deleteDate='', feedParentId='', deleted=2 WHERE deleted==1");

  if (currentNewsTab->type_ == NewsTabWidget::TabTypeDel) {
    currentNewsTab->newsModel_->select();
    currentNewsTab->slotNewsViewSelected(QModelIndex());
  }

  recountCategoryCounts();
}

void MainWindow::slotMarkReadCategory(QTreeWidgetItem *item)
{
  int type = item->text(1).toInt();
  int labelId = item->text(2).toInt();

  if ((currentNewsTab->type_ == type) && (currentNewsTab->labelId_ == labelId)) {
    currentNewsTab->markAllNewsRead();
  }
  else {
    emit signalMarkReadCategory(type, labelId);
    recountCategoryCounts();
  }
}

/** @brief Show/Hide categories tree
 *---------------------------------------------------------------------------*/
void MainWindow::showNewsCategoriesTree()
{
  if (categoriesTree_->isHidden()) {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
    showCategoriesButton_->setToolTip(tr("Hide Categories"));
    categoriesTree_->show();
    feedsSplitter_->restoreState(feedsWidgetSplitterState_);
  } else {
    feedsWidgetSplitterState_ = feedsSplitter_->saveState();
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_show.png"));
    showCategoriesButton_->setToolTip(tr("Show Categories"));
    categoriesTree_->hide();
    QList <int> sizes;
    sizes << height() << 20;
    feedsSplitter_->setSizes(sizes);
  }
}

/** @brief Move splitter between feeds tree and categories tree
 *---------------------------------------------------------------------------*/
void MainWindow::feedsSplitterMoved(int pos, int)
{
  if (categoriesTree_->isHidden()) {
    int height = pos + categoriesPanel_->height() + 2;
    if (height < feedsSplitter_->height()) {
      showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
      showCategoriesButton_->setToolTip(tr("Hide Categories"));
      categoriesTree_->show();
      recountCategoryCounts();
    }
  }
}

/** @brief Get feeds ids list string of folder \a idFolder
 *---------------------------------------------------------------------------*/
QString MainWindow::getIdFeedsString(int idFolder, int idException)
{
  QList<int> idList = UpdateObject::getIdFeedsInList(db_, idFolder);
  if (idList.count()) {
    QString str;
    for (int id : std::as_const(idList)) {
      if (id == idException) continue;
      if (!str.isEmpty()) str.append(" OR ");
      str.append(QString("feedId=%1").arg(id));
    }
    return str;
  } else {
    return QString("feedId=-1");
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

/** @brief Enable|Disable indent in feeds tree
 *---------------------------------------------------------------------------*/
void MainWindow::slotIndentationFeedsTree()
{
  feedsView_->setRootIsDecorated(indentationFeedsTreeAct_->isChecked());
}

/** @brief Open feed home page in external browser
 *---------------------------------------------------------------------------*/
void MainWindow::slotOpenHomeFeed()
{
  QModelIndex index = feedsView_->currentIndex();
  if (!index.isValid()) return;
  index = feedsProxyModel_->mapToSource(index);

  QString homePage = feedsModel_->dataField(index, "htmlUrl").toString();
  if (ArticleContent::isExternalLink(homePage)) mainApp->openExternalUrl(homePage);
}

/** @brief Sort feed and folders by title
 *---------------------------------------------------------------------------*/
void MainWindow::sortedByTitleFeedsTree()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QList<int> parentIdsPotential;
  parentIdsPotential << 0;
  while (!parentIdsPotential.empty()) {
    int parentId = parentIdsPotential.takeFirst();

    // Search children of parent <parentId>
    QSqlQuery q;
    q.prepare(QString("SELECT id, xmlUrl FROM feeds WHERE parentId=? ORDER BY text COLLATE LOCALE"));
    q.addBindValue(parentId);
    q.exec();

    // assing each child his <rowToParent>
    // ... store it in prospactive parent list
    int rowToParent = 0;
    while (q.next()) {
      int parentIdNew = q.value(0).toInt();
      QString xmlUrl = q.value(1).toString();

      QSqlQuery q2;
      q2.prepare("UPDATE feeds SET rowToParent=? WHERE id=?");
      q2.addBindValue(rowToParent);
      q2.addBindValue(parentIdNew);
      q2.exec();

      if (xmlUrl.isEmpty())
        parentIdsPotential << parentIdNew;
      ++rowToParent;
    }
  }

  feedsModelReload();
  QApplication::restoreOverrideCursor();
  DatabaseBackup::subscriptionsChanged("sort feed tree by name");
}

// ----------------------------------------------------------------------------
void MainWindow::slotPrevFolder()
{
  QModelIndex indexBefore = feedsView_->currentIndex();
  QModelIndex indexAfter;

  // Set to bottom folder, if there's no above folder
  if (!indexBefore.isValid())
    indexAfter = feedsView_->lastFolderInFolder(QModelIndex());
  else
    indexAfter = feedsView_->indexPreviousFolder(indexBefore);

  // there's no "upper" index
  if (!indexAfter.isValid()) return;
  feedsView_->clearSelection();

  feedsView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}
// ----------------------------------------------------------------------------
void MainWindow::slotNextFolder()
{
  QModelIndex indexBefore = feedsView_->currentIndex();
  QModelIndex indexAfter;

  // Set to top index, if there's no below index
  if (!indexBefore.isValid())
    indexAfter = feedsView_->firstFolderInFolder(QModelIndex());
  else
    indexAfter = feedsView_->indexNextFolder(indexBefore);

  // there's no "downer" index
  if (!indexAfter.isValid()) return;
  feedsView_->clearSelection();

  feedsView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}
// ----------------------------------------------------------------------------
void MainWindow::slotExpandFolder()
{
  QModelIndex index = feedsView_->currentIndex();
  if (!feedsModel_->isFolder(index)) {
    index = feedsModel_->parent(index);
  }
  feedsView_->setExpanded(index, !feedsView_->isExpanded(index));
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

void MainWindow::setStatusFeed(int feedId, QString status)
{
  QModelIndex index = feedsModel_->indexById(feedId);
  if (index.isValid()) {
    QModelIndex indexStatus = feedsModel_->indexSibling(index, "status");
    if (status.startsWith("1 ") && !FeedHealth::read(indexStatus.data(Qt::EditRole).toString()).error.isEmpty())
      return;
    feedsModel_->setData(indexStatus, status);
    feedsView_->viewport()->update();
  }
}

void MainWindow::addDefaultFeed()
{
  if (mainApp->dbFileExists()) return;

  const auto feeds = ProjectMetadata::defaultFeeds();
  if (feeds.isEmpty()) return;
  if (!db_.transaction()) {
    qWarning() << "Cannot begin default feed transaction:" << db_.lastError().text();
    return;
  }

  QSqlQuery q(db_);
  if (!q.prepare("INSERT INTO feeds(text, title, xmlUrl, htmlUrl, created, parentId, rowToParent) "
                 "VALUES(?, ?, ?, ?, ?, ?, ?)")) {
    qWarning() << "Cannot prepare default feed insertion:" << q.lastError().text();
    db_.rollback();
    return;
  }
  const QString created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  int row = 0;
  for (const auto &feed : feeds) {
    q.bindValue(0, feed.title);
    q.bindValue(1, feed.title);
    q.bindValue(2, feed.feedUrl);
    q.bindValue(3, feed.websiteUrl);
    q.bindValue(4, created);
    q.bindValue(5, 0);
    q.bindValue(6, row++);
    if (!q.exec()) {
      qWarning() << "Cannot insert default feed:" << q.lastError().text();
      q.finish();
      db_.rollback();
      return;
    }
  }
  q.finish();
  if (!db_.commit()) {
    qWarning() << "Cannot commit default feeds:" << db_.lastError().text();
    db_.rollback();
    return;
  }
  defaultFeedIconsPending_ = true;
  feedsModelReload();
}

void MainWindow::requestDefaultFeedIcons()
{
  if (!defaultFeedIconsPending_) return;
  defaultFeedIconsPending_ = false;
  for (const auto &feed : ProjectMetadata::defaultFeeds()) {
    const QString website = feed.websiteUrl.isEmpty() ? feed.feedUrl : feed.websiteUrl;
    emit faviconRequestUrl(website, feed.feedUrl);
  }
}

void MainWindow::createBackup()
{
  DatabaseBackup::instance()->manual(this);
}

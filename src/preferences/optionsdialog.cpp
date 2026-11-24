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
#include "optionsdialog.h"

#include "app/mainapplication.h"
#include "mainwindow/mainwindow.h"
#include "preferences/optionssearchpanel.h"
#include "preferences/pages/backupsettingspage.h"
#include "preferences/pages/browsersettingspage.h"
#include "preferences/pages/feedssettingspage.h"
#include "preferences/pages/fontscolorssettingspage.h"
#include "preferences/pages/generalsettingspage.h"
#include "preferences/pages/labelssettingspage.h"
#include "preferences/pages/languagesettingspage.h"
#include "preferences/pages/networksettingspage.h"
#include "preferences/pages/notificationssettingspage.h"
#include "preferences/pages/notifierfeedsdialog.h"
#include "preferences/pages/passwordssettingspage.h"
#include "preferences/pages/shortcutssettingspage.h"
#include "preferences/pages/systemtraysettingspage.h"
#include "settings.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QWindow>
#include <utility>

const OptionsDialog::PageDescriptor OptionsDialog::kPages[OptionsDialog::PageCount] = {
  { QT_TR_NOOP("General"),            &OptionsDialog::createGeneralWidget,            nullptr },
  { QT_TR_NOOP("System Tray"),        &OptionsDialog::createTraySystemWidget,         nullptr },
  { QT_TR_NOOP("Network"),            &OptionsDialog::createNetworkConnectionsWidget, nullptr },
  { QT_TR_NOOP("Article View"),       &OptionsDialog::createBrowserWidget,            nullptr },
  { QT_TR_NOOP("Feeds"),              &OptionsDialog::createFeedsWidget,              &OptionsDialog::onFeedsPageEntered },
  { QT_TR_NOOP("Labels"),             &OptionsDialog::createLabelsWidget,             &OptionsDialog::loadLabels },
  { QT_TR_NOOP("Notifications"),      &OptionsDialog::createNotifierWidget,           nullptr },
  { QT_TR_NOOP("Passwords"),          &OptionsDialog::createPasswordsWidget,          &OptionsDialog::loadPasswords },
  { QT_TR_NOOP("Language"),           &OptionsDialog::createLanguageWidget,           &OptionsDialog::loadLanguages },
  { QT_TR_NOOP("Fonts & Colors"),     &OptionsDialog::createFontsColorsWidget,        nullptr },
  { QT_TR_NOOP("Keyboard Shortcuts"), &OptionsDialog::createShortcutWidget,           &OptionsDialog::loadShortcuts },
  { QT_TR_NOOP("Database Backup"),    &OptionsDialog::createBackupWidget,             nullptr },
};

OptionsDialog::OptionsDialog(QWidget *parent)
  : Dialog(parent)
{
  openTimer_.start();
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Options"));
  setSizeGripEnabled(true);

  db_ = QSqlDatabase::database();

  contentLabel_ = new QLabel();
  contentLabel_->setObjectName(QStringLiteral("contentLabel_"));
  contentLabel_->setAlignment(Qt::AlignCenter);
  contentLabel_->setStyleSheet(
      QStringLiteral("#contentLabel_ {border-bottom: 1px solid %1;}")
          .arg(qApp->palette().color(QPalette::Mid).name()));
  contentLabel_->setMinimumHeight(36);
  contentLabel_->setMargin(4);
  QFont fontContentLabel = contentLabel_->font();
  fontContentLabel.setBold(true);
  fontContentLabel.setPointSize(fontContentLabel.pointSize() + 2);
  contentLabel_->setFont(fontContentLabel);
  mainLayout->insertWidget(0, contentLabel_);

  searchEdit_ = new LineEdit(this);
  searchEdit_->setObjectName(QStringLiteral("optionsSearchEdit_"));
  searchEdit_->setPlaceholderText(tr("Type to search..."));
  auto *searchLayout = new QHBoxLayout();
  searchLayout->setContentsMargins(0, 0, 0, 0);
  searchLayout->addWidget(new QLabel(tr("Search settings:")));
  searchLayout->addWidget(searchEdit_, 1);

  categoriesTree_ = new QTreeWidget();
  categoriesTree_->setObjectName(QStringLiteral("categoriesTree"));
  categoriesTree_->setStyleSheet(
      QStringLiteral("QTreeWidget::item { min-height: 20px; } QTreeWidget { padding: 1px; }"));
  categoriesTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  categoriesTree_->setHeaderHidden(true);
  categoriesTree_->setColumnCount(3);
  categoriesTree_->setColumnHidden(0, true);
  categoriesTree_->header()->setStretchLastSection(false);
  categoriesTree_->header()->setMinimumSectionSize(0);
  categoriesTree_->header()->resizeSection(2, 5);

  contentStack_ = new QStackedWidget();
  contentStack_->setObjectName(QStringLiteral("contentStack_"));

  searchCategoryItem_ = new QTreeWidgetItem(QStringList{QString::number(-1), tr("Search Results")});
  QFont searchFont = searchCategoryItem_->font(1);
  searchFont.setBold(true);
  searchFont.setItalic(true);
  searchCategoryItem_->setFont(1, searchFont);
  categoriesTree_->addTopLevelItem(searchCategoryItem_);
  searchCategoryItem_->setHidden(true);

  // Scroll only the current page, not a stack sized for its tallest hidden page.
  // Pages themselves are created and initialized on demand in ensurePageCreated().
  pageWidgets_.fill(nullptr, PageCount);
  pageScrollAreas_.reserve(PageCount);
  categoryItems_.reserve(PageCount);
  for (int i = 0; i < PageCount; ++i) {
    auto *item = new QTreeWidgetItem(QStringList{QString::number(i), tr(kPages[i].title)});
    categoriesTree_->addTopLevelItem(item);
    categoryItems_.append(item);
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameStyle(QFrame::NoFrame);
    pageScrollAreas_.append(scrollArea);
    contentStack_->addWidget(scrollArea);
  }

  searchPanel_ = new OptionsSearchPanel(this);
  contentStack_->addWidget(searchPanel_);

  splitter_ = new QSplitter();
  splitter_->setChildrenCollapsible(false);
  splitter_->addWidget(categoriesTree_);
  splitter_->addWidget(contentStack_);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);

  pageLayout->addLayout(searchLayout);
  pageLayout->addWidget(splitter_, 1);
  pageLayout->setContentsMargins(10, 5, 10, 5);

  buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &OptionsDialog::acceptDialog);
  connect(this, &QDialog::finished, this, &OptionsDialog::closeDialog);

  connect(categoriesTree_, &QTreeWidget::itemPressed, this, &OptionsDialog::slotCategoriesItemClicked);
  connect(this, &OptionsDialog::signalCategoriesTreeKeyUpDownPressed,
          this, &OptionsDialog::slotCategoriesTreeKeyUpDownPressed, Qt::QueuedConnection);
  connect(searchEdit_, &QLineEdit::textChanged, this, &OptionsDialog::searchTextChanged);
  connect(searchPanel_, &OptionsSearchPanel::entryActivated,
          this, &OptionsDialog::jumpToSearchEntry);

  auto *findShortcut = new QShortcut(QKeySequence::Find, this);
  connect(findShortcut, &QShortcut::activated, this, [this]() {
    searchEdit_->setFocus();
    searchEdit_->selectAll();
  });

  categoriesTree_->installEventFilter(this);
  searchEdit_->installEventFilter(this);
  searchPanel_->resultsTree()->installEventFilter(this);

  setMinimumSize(500, 400);
  Settings settings;
  const QByteArray savedGeometry = settings.value(QStringLiteral("options/geometry")).toByteArray();
  if (!savedGeometry.isEmpty()) {
    restoreGeometry(savedGeometry);
  } else {
    resize(700, 580);
  }

  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::ctor: %1 ms")
      .arg(openTimer_.elapsed());
}

void OptionsDialog::ensurePageCreated(int pageIndex)
{
  if (pageIndex < 0 || pageIndex >= PageCount || pageWidgets_.at(pageIndex) != nullptr)
    return;

  QElapsedTimer timer;
  timer.start();
  QWidget *page = (this->*(kPages[pageIndex].create))();
  pageWidgets_[pageIndex] = page;
  if (MainWindow *mw = qobject_cast<MainWindow *>(parent())) {
    mw->initOptionPage(pageIndex);
  }
  if (pageIndex == PageFeeds && feedsPage_) {
    feedsPage_->initCleanupTracking();
  }
  pageScrollAreas_.at(pageIndex)->setWidget(page);
  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::ensurePageCreated(%1): %2 ms")
      .arg(pageIndex)
      .arg(timer.elapsed());
}

void OptionsDialog::showEvent(QShowEvent *event)
{
  Dialog::showEvent(event);

  QScreen *screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
  if (!screen) return;
  const QRect available = screen->availableGeometry();
  const QSize frameMargins = frameSize() - size();
  const QSize availableSize = (available.size() - frameMargins).expandedTo(QSize(1, 1));
  setMinimumSize(QSize(500, 400).boundedTo(availableSize));
  resize(size().boundedTo(availableSize));

  // Keep the whole frame accessible after restoring on a smaller/different screen.
  // Do not impose a lasting maximum that prevents resizing on a larger monitor.
  const QRect frame = frameGeometry();
  const int x = qBound(available.left(), frame.left(),
                       qMax(available.left(), available.right() - frame.width() + 1));
  const int y = qBound(available.top(), frame.top(),
                       qMax(available.top(), available.bottom() - frame.height() + 1));
  move(pos() + QPoint(x, y) - frame.topLeft());

  if (!showLogged_) {
    // Measure the visible sections directly. The hidden ID column must not
    // contribute to the navigation width.
    const bool wasSearchHidden = searchCategoryItem_->isHidden();
    searchCategoryItem_->setHidden(false);
    categoriesTree_->resizeColumnToContents(1);
    searchCategoryItem_->setHidden(wasSearchHidden);
    const int categoryWidth = categoriesTree_->columnWidth(1) + categoriesTree_->columnWidth(2) +
        categoriesTree_->width() - categoriesTree_->viewport()->width();
    categoriesTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    categoriesTree_->setMinimumWidth(1); // Allow the user to narrow the pane past the labels.
    const int paneWidth = splitter_->contentsRect().width() - splitter_->handle(1)->width();
    splitter_->setSizes({categoryWidth, qMax(0, paneWidth - categoryWidth)});
    showLogged_ = true;
    qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::showEvent complete: %1 ms since ctor start")
        .arg(openTimer_.elapsed());
  }
}

void OptionsDialog::acceptDialog()
{
  if (networkPage_ && !networkPage_->validate()) return;
  if (browserPage_ && !browserPage_->validate()) return;

  if (generalPage_) generalPage_->save();
  if (networkPage_) networkPage_->save();
  if (labelsPage_) {
    labelsPage_->apply(db_);
    idLabels_ = labelsPage_->modifiedIds();
  }
  if (feedsNotifierDlg_) feedsNotifierDlg_->apply(db_);
  if (passwordsPage_) passwordsPage_->apply(db_);
  accept();
}

void OptionsDialog::closeDialog()
{
  Settings settings;
  settings.setValue(QStringLiteral("options/geometry"), saveGeometry());
}

void OptionsDialog::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape && !searchEdit_->text().isEmpty()) {
    searchEdit_->clear();
    return;
  }
  Dialog::keyPressEvent(event);
}

bool OptionsDialog::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (obj == searchEdit_) {
      if (keyEvent->key() == Qt::Key_Down && searchPanel_->hasResults()) {
        searchPanel_->focusFirstResult();
        return true;
      }
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        const int idx = searchPanel_->currentEntryIndex();
        if (idx >= 0) jumpToSearchEntry(idx);
        return true;
      }
      return false;
    }
    if (obj == searchPanel_->resultsTree()) {
      if (keyEvent->key() == Qt::Key_Up &&
          searchPanel_->resultsTree()->currentIndex().row() == 0) {
        searchEdit_->setFocus();
        return true;
      }
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        const int idx = searchPanel_->currentEntryIndex();
        if (idx >= 0) jumpToSearchEntry(idx);
        return true;
      }
      return false;
    }
    if (obj == categoriesTree_ &&
        (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) {
      emit signalCategoriesTreeKeyUpDownPressed();
    }
    return false;
  }
  return QDialog::eventFilter(obj, event);
}

QWidget *OptionsDialog::createGeneralWidget()
{
  generalPage_ = new GeneralSettingsPage(this, &searchEntries_, PageGeneral);
  showSplashScreen_ = generalPage_->showSplashScreen_;
  reopenFeedStartup_ = generalPage_->reopenFeedStartup_;
  openNewTabNextToActive_ = generalPage_->openNewTabNextToActive_;
  hideFeedsOpenTab_ = generalPage_->hideFeedsOpenTab_;
  showCloseButtonTab_ = generalPage_->showCloseButtonTab_;
#ifdef NQRSS_ENABLE_DONATE_BUTTON
  hideDonateButton_ = generalPage_->hideDonateButton_;
#endif
  showToggleFeedsTree_ = generalPage_->showToggleFeedsTree_;
  defaultIconFeeds_ = generalPage_->defaultIconFeeds_;
  autocollapseFolder_ = generalPage_->autocollapseFolder_;
  updateCheckEnabled_ = generalPage_->updateCheckEnabled_;
  storeDBMemory_ = generalPage_->storeDBMemory_;
  saveDBMemFileInterval_ = generalPage_->saveDBMemFileInterval_;
  return generalPage_;
}

QWidget *OptionsDialog::createTraySystemWidget()
{
  traySystemPage_ = new SystemTraySettingsPage(this, &searchEntries_, PageSystemTray);
  showTrayIconBox_ = traySystemPage_->showTrayIconBox_;
  startingTray_ = traySystemPage_->startingTray_;
  minimizingTray_ = traySystemPage_->minimizingTray_;
  closingTray_ = traySystemPage_->closingTray_;
  singleClickTray_ = traySystemPage_->singleClickTray_;
  clearStatusNew_ = traySystemPage_->clearStatusNew_;
  emptyWorking_ = traySystemPage_->emptyWorking_;
  return traySystemPage_;
}

QWidget *OptionsDialog::createNetworkConnectionsWidget()
{
  networkPage_ = new NetworkSettingsPage(this, &searchEntries_, PageNetwork);
  timeoutRequest_ = networkPage_->timeoutRequest_;
  numberRequests_ = networkPage_->numberRequests_;
  numberRepeats_ = networkPage_->numberRepeats_;
  customUserAgent_ = networkPage_->customUserAgent_;
  userAgentEdit_ = networkPage_->userAgentEdit_;
  return networkPage_;
}

QWidget *OptionsDialog::createBrowserWidget()
{
  browserPage_ = new BrowserSettingsPage(this, &searchEntries_, PageBrowser);
  autoLoadImages_ = browserPage_->autoLoadImages_;
  highQualityImageScaling_ = browserPage_->highQualityImageScaling_;
  preloadNewspaperPages_ = browserPage_->preloadNewspaperPages_;
  defaultZoomPages_ = browserPage_->defaultZoomPages_;
  customExternalBrowser_ = browserPage_->customExternalBrowser_;
  externalBrowser_ = browserPage_->externalBrowser_;
  returnFocusAfterLink_ = browserPage_->returnFocusAfterLink_;
  downloadLocationEdit_ = browserPage_->downloadLocationEdit_;
  askDownloadLocation_ = browserPage_->askDownloadLocation_;
  return browserPage_;
}

QWidget *OptionsDialog::createFeedsWidget()
{
  feedsPage_ = new FeedsSettingsPage(
      db_,
      [this]() {
        return categoriesTree_->currentItem() &&
               categoriesTree_->currentItem()->data(0, Qt::DisplayRole).toInt() == PageFeeds;
      },
      this, &searchEntries_, PageFeeds);
  updateFeedsStartUp_ = feedsPage_->updateFeedsStartUp_;
  updateFeedsEnable_ = feedsPage_->updateFeedsEnable_;
  updateFeedsInterval_ = feedsPage_->updateFeedsInterval_;
  updateIntervalType_ = feedsPage_->updateIntervalType_;
  positionLastNews_ = feedsPage_->positionLastNews_;
  positionFirstNews_ = feedsPage_->positionFirstNews_;
  positionUnreadNews_ = feedsPage_->positionUnreadNews_;
  openNewsWebViewOn_ = feedsPage_->openNewsWebViewOn_;
  nottoOpenNews_ = feedsPage_->nottoOpenNews_;
  markNewsReadOn_ = feedsPage_->markNewsReadOn_;
  markCurNewsRead_ = feedsPage_->markCurNewsRead_;
  markPrevNewsRead_ = feedsPage_->markPrevNewsRead_;
  markNewsReadTime_ = feedsPage_->markNewsReadTime_;
  markReadSwitchingFeed_ = feedsPage_->markReadSwitchingFeed_;
  markReadClosingTab_ = feedsPage_->markReadClosingTab_;
  markReadMinimize_ = feedsPage_->markReadMinimize_;
  formatDate_ = feedsPage_->formatDate_;
  formatTime_ = feedsPage_->formatTime_;
  alternatingRowColorsNews_ = feedsPage_->alternatingRowColorsNews_;
  simplifiedDateTime_ = feedsPage_->simplifiedDateTime_;
  mainNewsFilter_ = feedsPage_->mainNewsFilter_;
  styleSheetNewsEdit_ = feedsPage_->styleSheetNewsEdit_;
  changeBehaviorActionNUN_ = feedsPage_->changeBehaviorActionNUN_;
  notDeleteStarred_ = feedsPage_->notDeleteStarred_;
  notDeleteLabeled_ = feedsPage_->notDeleteLabeled_;
  markIdenticalNewsRead_ = feedsPage_->markIdenticalNewsRead_;
  cleanupOnShutdownBox_ = feedsPage_->cleanupOnShutdownBox_;
  dayCleanUpOn_ = feedsPage_->dayCleanUpOn_;
  maxDayCleanUp_ = feedsPage_->maxDayCleanUp_;
  newsCleanUpOn_ = feedsPage_->newsCleanUpOn_;
  maxNewsCleanUp_ = feedsPage_->maxNewsCleanUp_;
  readCleanUp_ = feedsPage_->readCleanUp_;
  neverUnreadCleanUp_ = feedsPage_->neverUnreadCleanUp_;
  neverStarCleanUp_ = feedsPage_->neverStarCleanUp_;
  neverLabelCleanUp_ = feedsPage_->neverLabelCleanUp_;
  cleanUpDeleted_ = feedsPage_->cleanUpDeleted_;
  optimizeDB_ = feedsPage_->optimizeDB_;
  avoidedOldNewsDateOn_ = feedsPage_->avoidedOldNewsDateOn_;
  avoidedOldNewsDate_ = feedsPage_->avoidedOldNewsDate_;
  return feedsPage_;
}

void OptionsDialog::onFeedsPageEntered()
{
  if (feedsPage_) feedsPage_->onPageEntered();
}

QWidget *OptionsDialog::createLabelsWidget()
{
  labelsPage_ = new LabelsSettingsPage(this, &searchEntries_, PageLabels);
  auto ensureShortcuts = [this]() -> ShortcutsSettingsPage * {
    ensurePageCreated(PageShortcuts);
    shortcutsPage_->load();
    return shortcutsPage_;
  };
  connect(labelsPage_, &LabelsSettingsPage::labelAdded, this,
          [ensureShortcuts](const QString &id, const QString &name, const QIcon &icon) {
    ensureShortcuts()->addLabelAction(id, name, icon);
  });
  connect(labelsPage_, &LabelsSettingsPage::labelEdited, this,
          [ensureShortcuts](const QString &id, const QString &name, const QIcon &icon) {
    ensureShortcuts()->updateLabelAction(id, name, icon);
  });
  connect(labelsPage_, &LabelsSettingsPage::labelDeleted, this,
          [ensureShortcuts](const QString &id) {
    ensureShortcuts()->removeLabelAction(id);
  });
  connect(labelsPage_, &LabelsSettingsPage::labelsSwapped, this,
          [ensureShortcuts](const QString &idA, const QString &idB) {
    ensureShortcuts()->swapLabelActions(idA, idB);
  });
  return labelsPage_;
}

void OptionsDialog::loadLabels()
{
  if (labelsPage_) labelsPage_->load();
}

QWidget *OptionsDialog::createNotifierWidget()
{
  feedsNotifierDlg_ = nullptr;
  notifierPage_ = new NotificationsSettingsPage(this, &searchEntries_, PageNotifications);
  soundNotifyBox_ = notifierPage_->soundNotifyBox_;
  editSoundNotifier_ = notifierPage_->editSoundNotifier_;
  selectionSoundNotifier_ = notifierPage_->selectionSoundNotifier_;
  playSoundNotifier_ = notifierPage_->playSoundNotifier_;
  showNotifyOn_ = notifierPage_->showNotifyOn_;
  screenNotify_ = notifierPage_->screenNotify_;
  positionNotify_ = notifierPage_->positionNotify_;
  transparencyNotify_ = notifierPage_->transparencyNotify_;
  countShowNewsNotify_ = notifierPage_->countShowNewsNotify_;
  timeShowNewsNotify_ = notifierPage_->timeShowNewsNotify_;
  widthTitleNewsNotify_ = notifierPage_->widthTitleNewsNotify_;
  showTitlesFeedsNotify_ = notifierPage_->showTitlesFeedsNotify_;
  showIconFeedNotify_ = notifierPage_->showIconFeedNotify_;
  showButtonMarkAllNotify_ = notifierPage_->showButtonMarkAllNotify_;
  showButtonMarkReadNotify_ = notifierPage_->showButtonMarkReadNotify_;
  showButtonExBrowserNotify_ = notifierPage_->showButtonExBrowserNotify_;
  showButtonDeleteNotify_ = notifierPage_->showButtonDeleteNotify_;
  closeNotify_ = notifierPage_->closeNotify_;
  fullscreenModeNotify_ = notifierPage_->fullscreenModeNotify_;
  showNotifyInactiveApp_ = notifierPage_->showNotifyInactiveApp_;
  onlySelectedFeeds_ = notifierPage_->onlySelectedFeeds_;

  if (auto *mw = qobject_cast<MainWindow *>(parent())) {
    connect(notifierPage_, &NotificationsSettingsPage::playSoundRequested,
            mw, &MainWindow::slotPlaySound);
  }
  connect(notifierPage_, &NotificationsSettingsPage::previewNotificationRequested,
          this, &OptionsDialog::showNotification);
  connect(notifierPage_, &NotificationsSettingsPage::selectFeedsRequested,
          this, &OptionsDialog::openFeedsNotifierDlg);
  return notifierPage_;
}

void OptionsDialog::openFeedsNotifierDlg()
{
  if (!feedsNotifierDlg_) feedsNotifierDlg_ = new NotifierFeedsDialog(this);
  feedsNotifierDlg_->load(db_, mainApp->mainWindow()->defaultIconFeeds_);
  feedsNotifierDlg_->exec();
}

QWidget *OptionsDialog::createPasswordsWidget()
{
  passwordsPage_ = new PasswordsSettingsPage(this, &searchEntries_, PagePasswords);
  return passwordsPage_;
}

void OptionsDialog::loadPasswords()
{
  if (passwordsPage_) passwordsPage_->load();
}

QWidget *OptionsDialog::createLanguageWidget()
{
  languagePage_ = new LanguageSettingsPage(this, &searchEntries_, PageLanguage);
  if (!selectedLanguage_.isEmpty()) languagePage_->setLanguage(selectedLanguage_);
  return languagePage_;
}

void OptionsDialog::loadLanguages()
{
  if (languagePage_) languagePage_->load();
}

QWidget *OptionsDialog::createFontsColorsWidget()
{
  fontsColorsPage_ = new FontsColorsSettingsPage(this, &searchEntries_, PageFontsColors);
  return fontsColorsPage_;
}

QWidget *OptionsDialog::createShortcutWidget()
{
  shortcutsPage_ = new ShortcutsSettingsPage(
      shortcutActions_, listDefaultShortcut_, this, &searchEntries_, PageShortcuts);
  return shortcutsPage_;
}

QWidget *OptionsDialog::createBackupWidget()
{
  auto *backupPage = new BackupSettingsPage(this, &searchEntries_, PageBackup);
  connect(this, &QDialog::accepted, backupPage, &BackupSettingsPage::save);
  backupWidget_ = backupPage;
  return backupWidget_;
}

void OptionsDialog::slotCategoriesTreeKeyUpDownPressed()
{
  slotCategoriesItemClicked(categoriesTree_->currentItem(), 1);
}

void OptionsDialog::slotCategoriesItemClicked(QTreeWidgetItem *item, int)
{
  if (!item) return;
  if (item == searchCategoryItem_) {
    contentLabel_->setText(tr("Search Results"));
    contentStack_->setCurrentWidget(searchPanel_);
    return;
  }
  const int pageIndex = item->data(0, Qt::DisplayRole).toInt();
  if (pageIndex < 0 || pageIndex >= PageCount) return;
  lastPageIndex_ = pageIndex;
  ensurePageCreated(pageIndex);

  contentLabel_->setText(item->data(1, Qt::DisplayRole).toString());
  contentStack_->setCurrentIndex(pageIndex);

  if (kPages[pageIndex].onEnter) {
    (this->*(kPages[pageIndex].onEnter))();
  }
}

int OptionsDialog::currentIndex()
{
  if (QTreeWidgetItem *item = categoriesTree_->currentItem()) {
    const int pageIndex = item->data(0, Qt::DisplayRole).toInt();
    if (pageIndex >= 0 && pageIndex < PageCount) return pageIndex;
  }
  return lastPageIndex_;
}

void OptionsDialog::setCurrentItem(int index)
{
  if (index < 0 || index >= categoryItems_.size()) return;
  categoriesTree_->setCurrentItem(categoryItems_.at(index), 1);
  slotCategoriesTreeKeyUpDownPressed();
}

void OptionsDialog::buildSearchIndex()
{
  if (searchIndexBuilt_) return;
  searchIndexBuilt_ = true;
  for (int i = 0; i < PageCount; ++i) {
    ensurePageCreated(i);
  }
}

QTabWidget *OptionsDialog::tabWidgetForPage(int pageIndex) const
{
  switch (pageIndex) {
  case PageBrowser:       return browserPage_;
  case PageFeeds:         return feedsPage_;
  case PageNotifications: return notifierPage_;
  case PageFontsColors:   return fontsColorsPage_;
  default:                return nullptr;
  }
}

QString OptionsDialog::searchEntrySection(const OptionSearchEntry &entry) const
{
  if (entry.page < 0 || entry.page >= PageCount) return QString();
  const QString pageTitle = tr(kPages[entry.page].title);
  if (entry.subTab >= 0) {
    if (QTabWidget *tabs = tabWidgetForPage(entry.page)) {
      const QString subTitle = tabs->tabText(entry.subTab);
      if (!subTitle.isEmpty()) {
        return QStringLiteral("%1 \u203A %2").arg(pageTitle, subTitle);
      }
    }
  }
  return pageTitle;
}

void OptionsDialog::searchTextChanged(const QString &text)
{
  const QString query = text.trimmed();
  if (query.isEmpty()) {
    searchCategoryItem_->setHidden(true);
    searchPanel_->clear();
    if (contentStack_->currentWidget() == searchPanel_) {
      setCurrentItem(lastPageIndex_);
    }
    return;
  }

  buildSearchIndex();

  searchCategoryItem_->setHidden(false);
  categoriesTree_->setCurrentItem(searchCategoryItem_, 1);
  contentLabel_->setText(tr("Search Results"));
  contentStack_->setCurrentWidget(searchPanel_);

  searchPanel_->updateResults(query, searchEntries_, [this](const OptionSearchEntry &entry) {
    return searchEntrySection(entry);
  });
}

void OptionsDialog::jumpToSearchEntry(int entryIndex)
{
  if (entryIndex < 0 || entryIndex >= searchEntries_.size()) return;
  const OptionSearchEntry entry = searchEntries_.at(entryIndex);

  setCurrentItem(entry.page);
  if (entry.subTab >= 0) {
    if (QTabWidget *tabs = tabWidgetForPage(entry.page)) {
      tabs->setCurrentIndex(entry.subTab);
    }
  }

  QTimer::singleShot(0, this, [this, entry]() {
    if (!entry.target) return;

    if (entry.target == fontsColorsPage_) {
      if (entry.subTab == 0) {
        fontsColorsPage_->selectFontItem(entry.itemId);
      } else {
        fontsColorsPage_->selectColorOption(entry.itemId);
      }
      return;
    }

    if (entry.target == shortcutsPage_ && entry.isShortcut) {
      shortcutsPage_->selectAction(entry.itemKey, entry.title);
      return;
    }

    if (entry.page >= 0 && entry.page < pageScrollAreas_.size()) {
      pageScrollAreas_.at(entry.page)->ensureWidgetVisible(entry.target, 50, 50);
    }
    if (entry.target->isEnabled() && entry.target->focusPolicy() != Qt::NoFocus) {
      entry.target->setFocus();
    }
    if (entry.page >= 0 && entry.page < pageScrollAreas_.size()) {
      searchPanel_->highlightWidget(entry.target, pageScrollAreas_.at(entry.page));
    }
  });
}

QNetworkProxy OptionsDialog::proxy()
{
  return networkPage_ ? networkPage_->proxy() : QNetworkProxy();
}

void OptionsDialog::setProxy(const QNetworkProxy proxy)
{
  if (networkPage_) networkPage_->setProxy(proxy);
}

QString OptionsDialog::language() const
{
  return languagePage_ ? languagePage_->language() : selectedLanguage_;
}

void OptionsDialog::setLanguage(const QString &langFileName)
{
  selectedLanguage_ = langFileName;
  if (languagePage_) languagePage_->setLanguage(langFileName);
}

QFont OptionsDialog::fontOptionValue(int optionId) const
{
  return fontsColorsPage_->fontOptionValue(optionId);
}

void OptionsDialog::setFontOptionValue(int optionId, const QFont &font)
{
  fontsColorsPage_->setFontOptionValue(optionId, font);
}

QString OptionsDialog::colorOptionValue(int optionId) const
{
  return fontsColorsPage_->colorOptionValue(optionId);
}

void OptionsDialog::setColorOptionValue(int optionId, const QString &colorName)
{
  fontsColorsPage_->setColorOptionValue(optionId, colorName);
}

void OptionsDialog::setBehaviorIconTray(int behavior)
{
  if (traySystemPage_) traySystemPage_->setBehaviorIconTray(behavior);
}

int OptionsDialog::behaviorIconTray()
{
  return traySystemPage_ ? traySystemPage_->behaviorIconTray() : STATIC_ICON_TRAY;
}

void OptionsDialog::loadActionShortcut(QList<QAction *> actions, const QStringList *list)
{
  shortcutActions_ = std::move(actions);
  listDefaultShortcut_ = list;
}

void OptionsDialog::loadShortcuts()
{
  ensurePageCreated(PageShortcuts);
  if (shortcutsPage_) shortcutsPage_->load();
}

void OptionsDialog::saveActionShortcut(QList<QAction *> actions, QActionGroup *labelGroup)
{
  if (shortcutsPage_) shortcutsPage_->save(std::move(actions), labelGroup, parent());
}

bool OptionsDialog::shortcutsLoaded() const
{
  return shortcutsPage_ && shortcutsPage_->isLoaded();
}

void OptionsDialog::setOpeningFeed(int action)
{
  if (feedsPage_) feedsPage_->setOpeningFeed(action);
}

int OptionsDialog::getOpeningFeed()
{
  return feedsPage_ ? feedsPage_->getOpeningFeed() : 0;
}

OptionsDialog::NotificationAppearance OptionsDialog::notificationPreviewAppearance() const
{
  if (!isPageCreated(PageFontsColors)) {
    const auto *mainWindow = mainApp->mainWindow();
    return {mainWindow->notificationFontFamily_, mainWindow->notificationFontSize_,
            mainWindow->notifierTextColor_, mainWindow->notifierBackgroundColor_,
            mainWindow->linkColor_};
  }

  constexpr int notificationFontRow = 4;
  constexpr int notificationTextColorId = 21;
  constexpr int notificationBackgroundColorId = 22;
  constexpr int linkColorId = 6;
  const QFont font = fontOptionValue(notificationFontRow);
  return {font.family(), font.pointSize(),
          colorOptionValue(notificationTextColorId),
          colorOptionValue(notificationBackgroundColorId),
          colorOptionValue(linkColorId)};
}

void OptionsDialog::showNotification()
{
  if (notificationWidget_) delete notificationWidget_;
  QList<int> idFeedList;
  QList<int> cntNewNewsList;
  QList<int> idColorList;
  QStringList colorList;
  notificationWidget_ = new NotificationWidget(idFeedList, cntNewNewsList,
                                               idColorList, colorList,
                                               this, this);

  connect(notificationWidget_, &NotificationWidget::signalClose,
          this, &OptionsDialog::deleteNotification);

  notificationWidget_->show();
}

void OptionsDialog::deleteNotification()
{
  notificationWidget_->deleteLater();
  notificationWidget_ = nullptr;
}

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
  setWindowTitle(tr("Options"));
  setSizeGripEnabled(true);

  db_ = QSqlDatabase::database();

  contentLabel_ = new QLabel();
  contentLabel_->setObjectName(QStringLiteral("contentLabel_"));
  contentLabel_->setAlignment(Qt::AlignCenter);
  contentLabel_->setStyleSheet(
      QStringLiteral("#contentLabel_ {border-bottom: 1px solid %1;}")
          .arg(qApp->palette().color(QPalette::Mid).name()));
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
  pageSaveEligible_.fill(false, PageCount);
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
  if (isPageSaveEligible(PageNetwork) && networkPage_ && !networkPage_->validate()) return;
  if (isPageSaveEligible(PageBrowser) && browserPage_ && !browserPage_->validate()) return;

  if (isPageSaveEligible(PageGeneral) && generalPage_) generalPage_->save();
  if (isPageSaveEligible(PageNetwork) && networkPage_) networkPage_->save();
  if (isPageSaveEligible(PageLabels) && labelsPage_) labelsPage_->apply(db_);
  if (isPageSaveEligible(PageNotifications) && feedsNotifierDlg_) feedsNotifierDlg_->apply(db_);
  if (isPageSaveEligible(PagePasswords) && passwordsPage_) passwordsPage_->apply(db_);
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
  return (generalPage_ = new GeneralSettingsPage(this, &searchEntries_, PageGeneral));
}

QWidget *OptionsDialog::createTraySystemWidget()
{
  return (traySystemPage_ = new SystemTraySettingsPage(this, &searchEntries_, PageSystemTray));
}

QWidget *OptionsDialog::createNetworkConnectionsWidget()
{
  return (networkPage_ = new NetworkSettingsPage(this, &searchEntries_, PageNetwork));
}

QWidget *OptionsDialog::createBrowserWidget()
{
  return (browserPage_ = new BrowserSettingsPage(this, &searchEntries_, PageBrowser));
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
  return (passwordsPage_ = new PasswordsSettingsPage(this, &searchEntries_, PagePasswords));
}

void OptionsDialog::loadPasswords()
{
  if (passwordsPage_) passwordsPage_->load();
}

QWidget *OptionsDialog::createLanguageWidget()
{
  return (languagePage_ = new LanguageSettingsPage(this, &searchEntries_, PageLanguage));
}

void OptionsDialog::loadLanguages()
{
  if (languagePage_) languagePage_->load();
}

QWidget *OptionsDialog::createFontsColorsWidget()
{
  return (fontsColorsPage_ = new FontsColorsSettingsPage(this, &searchEntries_, PageFontsColors));
}

QWidget *OptionsDialog::createShortcutWidget()
{
  return (shortcutsPage_ = new ShortcutsSettingsPage(
      shortcutActions_, listDefaultShortcut_, this, &searchEntries_, PageShortcuts));
}

void OptionsDialog::loadShortcuts()
{
  ensurePageCreated(PageShortcuts);
  if (shortcutsPage_) shortcutsPage_->load();
}

QWidget *OptionsDialog::createBackupWidget()
{
  auto *backupPage = new BackupSettingsPage(this, &searchEntries_, PageBackup);
  connect(this, &QDialog::accepted, backupPage, [this, backupPage]() {
    if (isPageSaveEligible(PageBackup)) backupPage->save();
  });
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
    const QString query = searchEdit_->text().trimmed();
    if (!query.isEmpty()) {
      searchPanel_->updateResults(query, searchEntries_, [this](const OptionSearchEntry &entry) {
        return searchEntrySection(entry);
      });
    }
    contentLabel_->setText(tr("Search Results"));
    contentStack_->setCurrentWidget(searchPanel_);
    return;
  }
  const int pageIndex = item->data(0, Qt::DisplayRole).toInt();
  if (pageIndex < 0 || pageIndex >= PageCount) return;
  lastPageIndex_ = pageIndex;
  ensurePageCreated(pageIndex);
  pageSaveEligible_[pageIndex] = true;

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

void OptionsDialog::loadActionShortcut(QList<QAction *> actions, const QStringList *list)
{
  shortcutActions_ = std::move(actions);
  listDefaultShortcut_ = list;
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
  const QFont font = fontsColorsPage_->fontOptionValue(notificationFontRow);
  return {font.family(), font.pointSize(),
          fontsColorsPage_->colorOptionValue(notificationTextColorId),
          fontsColorsPage_->colorOptionValue(notificationBackgroundColorId),
          fontsColorsPage_->colorOptionValue(linkColorId)};
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

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
#include "projectmetadata.h"
#include <utility>
#include "common/logfile.h"
#include "optionsdialog.h"
#include "preferences/optionssearchpanel.h"
#include "preferences/pages/backupsettingspage.h"
#include "preferences/pages/fontscolorssettingspage.h"
#include "preferences/pages/labelssettingspage.h"
#include "preferences/pages/languagesettingspage.h"
#include "preferences/pages/notifierfeedsdialog.h"
#include "preferences/pages/passwordssettingspage.h"
#include "preferences/pages/shortcutssettingspage.h"
#include <QElapsedTimer>
#include <QShortcut>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QGuiApplication>

#include "app/mainapplication.h"
#include "settings.h"
#include "app/globals.h"

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
  , hadStoredCustomUserAgent_(false)
  , userAgentWarningAccepted_(false)
  , notificationWidget_(nullptr)
{
  openTimer_.start();
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Options"));
  setSizeGripEnabled(true);

  db_ = QSqlDatabase::database();

  contentLabel_ = new QLabel();
  contentLabel_->setObjectName("contentLabel_");
  contentLabel_->setAlignment(Qt::AlignCenter);
  contentLabel_->setStyleSheet(
        QString("#contentLabel_ {border-bottom: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Mid).name()));
  contentLabel_->setMinimumHeight(36);
  contentLabel_->setMargin(4);
  QFont fontContentLabel = contentLabel_->font();
  fontContentLabel.setBold(true);
  fontContentLabel.setPointSize(fontContentLabel.pointSize()+2);
  contentLabel_->setFont(fontContentLabel);
  mainLayout->insertWidget(0, contentLabel_);

  searchEdit_ = new LineEdit(this);
  searchEdit_->setObjectName("optionsSearchEdit_");
  searchEdit_->setPlaceholderText(tr("Type to search..."));
  auto *searchLayout = new QHBoxLayout();
  searchLayout->setContentsMargins(0, 0, 0, 0);
  searchLayout->addWidget(new QLabel(tr("Search settings:")));
  searchLayout->addWidget(searchEdit_, 1);

  categoriesTree_ = new QTreeWidget();
  categoriesTree_->setObjectName("categoriesTree");
  categoriesTree_->setStyleSheet("QTreeWidget::item { min-height: 20px; }"
                                 "QTreeWidget { padding: 1px; }");
  categoriesTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  categoriesTree_->setHeaderHidden(true);
  categoriesTree_->setColumnCount(3);
  categoriesTree_->setColumnHidden(0, true);
  categoriesTree_->header()->setStretchLastSection(false);
  categoriesTree_->header()->setMinimumSectionSize(0);
  categoriesTree_->header()->resizeSection(2, 5);

  contentStack_ = new QStackedWidget();
  contentStack_->setObjectName("contentStack_");

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
  const QByteArray savedGeometry = settings.value("options/geometry").toByteArray();
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
  if (pageIndex == PageFeeds) {
    initialMaxDayCleanUp_ = maxDayCleanUp_->value();
    updateMaxDayCleanUpWarning();
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
  if (isPageCreated(PageNetwork) && customUserAgent_->isChecked() && userAgentEdit_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("Custom User-Agent"),
                         tr("Enter a custom User-Agent or disable the custom User-Agent option."));
    return;
  }

  if (isPageCreated(PageBrowser) && customExternalBrowser_->isChecked() && externalBrowser_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("External Browser"), tr("Enter a custom browser or select the system default browser."));
    return;
  }

#if defined(Q_OS_WIN)
  if (isPageCreated(PageGeneral)) {
    if (mainApp->isPortableAppsCom()) {
      if (autoRunEnabled_->isChecked()) {
        QFileInfo file(QCoreApplication::applicationDirPath() % "/../../" % QCoreApplication::applicationName() % "Portable.exe");
        autoRunSettings_->setValue((QCoreApplication::applicationName() + "Portable"), QDir::toNativeSeparators(file.absoluteFilePath()));
      } else {
        autoRunSettings_->remove((QCoreApplication::applicationName() + "Portable"));
      }
    } else {
      if (autoRunEnabled_->isChecked())
        autoRunSettings_->setValue(QCoreApplication::applicationName(), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
      else
        autoRunSettings_->remove(QCoreApplication::applicationName());
    }
  }
#endif

  if (isPageCreated(PageNetwork)) {
    Settings userAgentSettings("Settings");
    const bool useCustomUserAgent = customUserAgent_->isChecked();
    const QString customUserAgent = userAgentEdit_->text().trimmed();
    userAgentSettings.setValue("useCustomUserAgent", useCustomUserAgent);
    if (useCustomUserAgent || hadStoredCustomUserAgent_ || userAgentWarningAccepted_)
      userAgentSettings.setValue("userAgent", customUserAgent);
    globals.setUserAgent(useCustomUserAgent, customUserAgent);

    applyProxy();
  }
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
  settings.setValue("options/geometry", saveGeometry());
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
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
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

/** @brief Create widget "General"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createGeneralWidget()
{
  generalWidget_ = OptionsPageBuilder::buildFrame(PageGeneral, &searchEntries_, [this](OptionsPageBuilder &b) {
    b.check(showSplashScreen_, tr("Show splash screen on startup"), QString(),
            QStringLiteral("launch logo boot"));
    b.check(reopenFeedStartup_, tr("Reopen last opened feeds on startup"), QString(),
            QStringLiteral("restore session tabs"));
    b.check(openNewTabNextToActive_, tr("Open new tab next to active"), QString(),
            QStringLiteral("tab position order"));
    b.check(hideFeedsOpenTab_, tr("Hide feeds tree when opening tabs"), QString(),
            QStringLiteral("sidebar panel collapse"));
    b.check(showToggleFeedsTree_, tr("Show feeds tree toggle"), QString(),
            QStringLiteral("sidebar panel button"));
    b.check(defaultIconFeeds_, tr("Show default RSS icon instead of each feed's icon"), QString(),
            QStringLiteral("favicon"));
    b.check(autocollapseFolder_, tr("Automatically collapse folders"), QString(),
            QStringLiteral("tree expand"));
    b.check(showCloseButtonTab_, tr("Show a close button on each tab"), QString(),
            QStringLiteral("tabs close"));
#ifdef NQRSS_ENABLE_DONATE_BUTTON
    b.check(hideDonateButton_, tr("Hide the donate button"), QString(),
            QStringLiteral("support"));
#endif
    b.spacing(20);

#if defined(Q_OS_WIN)
    b.check(autoRunEnabled_, tr("Run %1 at Windows startup").arg(QGuiApplication::applicationDisplayName()),
            QString(), QStringLiteral("autostart boot login"));
    autoRunSettings_ = new QSettings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                                     QSettings::NativeFormat);
    const QString autoRunKey = mainApp->isPortableAppsCom()
        ? (QCoreApplication::applicationName() + QStringLiteral("Portable"))
        : QCoreApplication::applicationName();
    autoRunEnabled_->setChecked(autoRunSettings_->value(autoRunKey, false).toBool());
#endif

    b.check(updateCheckEnabled_, tr("Automatically check for updates"), QString(),
            QStringLiteral("version upgrade release"));
    b.check(storeDBMemory_, tr("Store the database in memory (requires application restart)"), QString(),
            QStringLiteral("ram sqlite performance cache"));
    QWidget *saveDBMemFileWidget = b.indented(QMargins(15, 0, 0, 0), [this](OptionsPageBuilder &sub) {
      sub.spinRow(saveDBMemFileInterval_,
                  tr("Save the in-memory database to disk every"), 1, 999, tr("minutes"),
                  QString(), QStringLiteral("ram sqlite flush interval"));
    });
    OptionsPageBuilder::bindEnabled(storeDBMemory_, {saveDBMemFileWidget});

    b.group(tr("Debug"), [this](OptionsPageBuilder &grp) {
      grp.row([this](OptionsRowBuilder &r) {
        QCheckBox *fileLogging = r.check(tr("Enable file logging"),
            tr("Takes effect immediately. Existing logs are kept when disabled."),
            QStringLiteral("debug log diagnostics"));
        fileLogging->setChecked(LogFile::fileLoggingEnabled());
        connect(fileLogging, &QCheckBox::toggled, this, [](bool enabled) {
          Settings().setValue("Settings/logFileOutput", enabled);
          LogFile::setFileLoggingEnabled(enabled);
        });
        QPushButton *showLog = r.button(tr("Show log location"));
        connect(showLog, &QPushButton::clicked, this, []() { LogFile::showLocation(); });
      });
    }, Qt::Horizontal);
    b.stretch(1);
  });
  return generalWidget_;
}

/** @brief Create widget "System tray"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createTraySystemWidget()
{
  traySystemWidget_ = OptionsPageBuilder::buildFrame(PageSystemTray, &searchEntries_, [this](OptionsPageBuilder &b) {
    b.group(showTrayIconBox_, tr("Show system tray icon"), true, [this](OptionsPageBuilder &grp) {
      const QString appName = QGuiApplication::applicationDisplayName();
#ifndef Q_OS_MAC
      grp.label(tr("Move to the system tray when:"));
      grp.indented(QMargins(15, 0, 5, 10), [this, appName](OptionsPageBuilder &sub) {
        sub.check(startingTray_, tr("starting %1").arg(appName), QString(),
                  QStringLiteral("minimized hidden launch"));
        sub.check(minimizingTray_, tr("minimizing %1").arg(appName), QString(),
                  QStringLiteral("minimize hide"));
        sub.check(closingTray_, tr("closing %1").arg(appName), QString(),
                  QStringLiteral("close quit exit"));
      });
#else
      startingTray_ = new QCheckBox(this);
      startingTray_->hide();
      minimizingTray_ = new QCheckBox(this);
      minimizingTray_->hide();
      closingTray_ = new QCheckBox(this);
      closingTray_->hide();
#endif
      grp.label(tr("Tray icon behavior:"));
      grp.indented(QMargins(15, 0, 5, 10), [this](OptionsPageBuilder &sub) {
        sub.radio(staticIconTray_, tr("Show a static icon"), QStringLiteral("tray badge"));
        sub.radio(changeIconTray_, tr("Change icon when new articles arrive"), QStringLiteral("tray badge"));
        sub.radio(newCountTray_, tr("Show number of new articles"), QStringLiteral("tray badge counter"));
        sub.radio(unreadCountTray_, tr("Show number of unread articles"), QStringLiteral("tray badge counter"));
      });
#ifndef Q_OS_MAC
      grp.check(singleClickTray_, tr("Use a single click instead of a double click to show the window"),
                QString(), QStringLiteral("click open"));
      grp.check(clearStatusNew_, tr("Clear 'new' status when minimizing to tray"),
                QString(), QStringLiteral("unread reset"));
#else
      singleClickTray_ = new QCheckBox(this);
      singleClickTray_->hide();
      clearStatusNew_ = new QCheckBox(this);
      clearStatusNew_->hide();
#endif
#if defined(Q_OS_WIN)
      grp.check(emptyWorking_, tr("Empty working set when minimizing to tray"),
                QString(), QStringLiteral("ram memory trim"));
#else
      emptyWorking_ = new QCheckBox(this);
      emptyWorking_->hide();
#endif
      grp.stretch(1);
    }, Qt::Vertical, std::nullopt, QStringLiteral("notification area panel"));
  });
  return traySystemWidget_;
}

/** @brief Create widget "Network connections"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createNetworkConnectionsWidget()
{
  networkConnectionsWidget_ = OptionsPageBuilder::buildFrame(PageNetwork, &searchEntries_, [this](OptionsPageBuilder &b) {
    b.radio(directConnectionButton_, tr("Direct connection to the Internet"),
            QStringLiteral("proxy network"));
    b.radio(systemProxyButton_, tr("System proxy configuration (if available)"),
            QStringLiteral("proxy network"));
    b.radio(manualProxyButton_, tr("Manual proxy configuration:"),
            QStringLiteral("HTTP SOCKS5 host port username password"));

    QWidget *manualWidget = b.indented(QMargins(0, 0, 0, 0), [this](OptionsPageBuilder &sub) {
      sub.row([this](OptionsRowBuilder &r) {
        r.combo(typeProxy_, {QStringLiteral("HTTP"), QStringLiteral("SOCKS5")});
        r.label(tr("Proxy server:"));
        r.lineEdit(editHost_);
        r.label(tr("Port:"));
        r.lineEdit(editPort_);
        editPort_->setFixedWidth(60);
      }, QMargins(0, 0, 0, 0), false);
      sub.row([this](OptionsRowBuilder &r) {
        r.label(tr("Username:"));
        r.lineEdit(editUser_);
        r.label(tr("Password:"));
        r.lineEdit(editPassword_);
        editPassword_->setEchoMode(QLineEdit::Password);
      }, QMargins(0, 0, 0, 0), false);
      sub.stretch();
    });
    OptionsPageBuilder::bindEnabled(manualProxyButton_, {manualWidget});
    b.spacing(20);

    b.label(tr("Network options for feed updates (requires application restart):"));
    b.grid(QMargins(15, 0, 5, 0), 1, [this](OptionsGridBuilder &g) {
      g.addSpinRow(0, tr("Request timeout:"), timeoutRequest_, 0, 300, QString(), QString(),
                   QStringLiteral("connection wait seconds"));
      g.addSpinRow(1, tr("Number of requests:"), numberRequests_, 1, 10, QString(), QString(),
                   QStringLiteral("parallel concurrent connections threads"));
      g.addSpinRow(2, tr("Number of retries:"), numberRepeats_, 1, 10, QString(), QString(),
                   QStringLiteral("retry attempts"));
    });
    b.spacing(20);

    b.check(customUserAgent_, tr("Use custom User-Agent"), QString(),
            QStringLiteral("ua header browser fingerprint"));
    b.indented(QMargins(15, 0, 5, 0), [this](OptionsPageBuilder &sub) {
      userAgentEdit_ = new LineEdit();
      Settings userAgentSettings("Settings");
      hadStoredCustomUserAgent_ = userAgentSettings.contains("userAgent");
      const bool useCustomUserAgent = userAgentSettings.contains("useCustomUserAgent")
          ? userAgentSettings.value("useCustomUserAgent", false).toBool()
          : hadStoredCustomUserAgent_;
      userAgentEdit_->setText(hadStoredCustomUserAgent_
          ? userAgentSettings.value("userAgent").toString()
          : QString());
      userAgentEdit_->setPlaceholderText(tr("Enter a custom User-Agent"));
      userAgentEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      userAgentEdit_->setMinimumWidth(0);
      customUserAgent_->setChecked(useCustomUserAgent);
      userAgentEdit_->setEnabled(useCustomUserAgent);
      sub.addWidget(userAgentEdit_);

      connect(customUserAgent_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && !hadStoredCustomUserAgent_ && !userAgentWarningAccepted_) {
          QMessageBox warning(QMessageBox::Warning, tr("Custom User-Agent"),
                              tr("A custom User-Agent can make this application's requests more distinctive "
                                 "and may make them easier to identify or fingerprint. Only change it if "
                                 "you understand the implications."),
                              QMessageBox::NoButton, this);
          QPushButton *continueButton = warning.addButton(tr("Continue"), QMessageBox::AcceptRole);
          warning.addButton(QMessageBox::Cancel);
          warning.setDefaultButton(QMessageBox::Cancel);
          warning.exec();
          if (warning.clickedButton() != continueButton) {
            QSignalBlocker blocker(customUserAgent_);
            customUserAgent_->setChecked(false);
            userAgentEdit_->setEnabled(false);
            return;
          }
          userAgentWarningAccepted_ = true;
        }
        userAgentEdit_->setEnabled(checked);
      });
    });
    b.stretch(1);
  });
  return networkConnectionsWidget_;
}

/** @brief Create widget "Browser"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createBrowserWidget()
{
  browserWidget_ = OptionsPageBuilder::buildTabs(PageBrowser, &searchEntries_, {
    {tr("Articles"), [this](OptionsPageBuilder &b) {
      b.check(autoLoadImages_, tr("Load images"), QString(),
              QStringLiteral("pictures graphics media"));
      b.check(highQualityImageScaling_, tr("Use Lanczos scaling for article images"),
              tr("Scales article images using a 2-pass Lanczos-3 filter on a background thread"),
              QStringLiteral("pictures resize quality"));
      b.spinRow(preloadNewspaperPages_, tr("Preload up to"), 0, 10,
                tr("pages in newspaper mode"), tr("0 disables image preloading"),
                QStringLiteral("cache prefetch images"));
      OptionsPageBuilder::bindEnabled(autoLoadImages_, {highQualityImageScaling_, preloadNewspaperPages_});

      b.row([this](OptionsRowBuilder &r) {
        const QString zoomLabel = tr("Default article zoom:");
        r.label(zoomLabel);
        r.spin(defaultZoomPages_, 30, 300, QStringLiteral(" %"));
        r.registerSearch(defaultZoomPages_, zoomLabel, QString(),
                         QStringLiteral("scale magnify text size"));
      });
      b.spacing(12);

      b.label(tr("Open links in:"));
      QRadioButton *systemBrowser = b.radio(tr("System default web browser"),
                                            QStringLiteral("url link open"));
      b.radio(customExternalBrowser_, tr("Custom web browser"),
              QStringLiteral("url link open"));
      auto *browserGroup = new QButtonGroup(this);
      browserGroup->addButton(systemBrowser);
      browserGroup->addButton(customExternalBrowser_);
      systemBrowser->setChecked(true);

      QPushButton *browseBrowser = b.pathRow(externalBrowser_, QString(), tr("Browse..."));
#ifdef Q_OS_MAC
      externalBrowser_->setPlaceholderText(tr("Application name or application bundle path"));
#else
      externalBrowser_->setPlaceholderText(tr("Browser command, including any arguments"));
      externalBrowser_->setToolTip(tr("Use double quotes around paths containing spaces. The article URL is appended automatically."));
#endif
      OptionsPageBuilder::bindEnabled(customExternalBrowser_, {externalBrowser_, browseBrowser});
      connect(browseBrowser, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Select Browser"), QString(), tr("All files (*)"),
            nullptr, QFileDialog::DontResolveSymlinks);
        if (!file.isEmpty()) externalBrowser_->setText(QDir::toNativeSeparators(file));
      });

      b.check(returnFocusAfterLink_,
              tr("Return focus to %1 after opening a link").arg(ProjectMetadata::name()),
              tr("Attempts to bring %1 back to the foreground after opening an article link in your browser. Your desktop may prevent this, particularly on Wayland.").arg(ProjectMetadata::name()),
              QStringLiteral("foreground window"));
      // clicked, rather than toggled: loading saved settings must not show a dialog.
      connect(returnFocusAfterLink_, &QAbstractButton::clicked, this, [this](bool checked) {
        if (!checked || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")) ||
            AppSettings::returnFocusWaylandWarningShown.get()) return;
        QMessageBox::warning(this, tr("Returning focus"),
                             tr("On Wayland, returning focus to %1 after opening a link may be unreliable or may not work at all.").arg(ProjectMetadata::name()),
                             QMessageBox::Ok);
        // Remember the acknowledgement even if the user cancels Options afterward.
        AppSettings::returnFocusWaylandWarningShown.set(true);
      });
      b.stretch();
    }},
    {tr("Downloads"), [this](OptionsPageBuilder &b) {
      QPushButton *browse = b.pathRow(downloadLocationEdit_, tr("Download location:"), tr("Browse..."),
                                      QStringLiteral("save folder directory path"));
      connect(browse, &QAbstractButton::clicked, this, &OptionsDialog::selectionDownloadLocation);
      b.check(askDownloadLocation_, tr("Ask where to save each file before downloading"),
              QString(), QStringLiteral("prompt save as dialog"));
      b.stretch();
    }},
  });
  return browserWidget_;
}

/** @brief Create widget "Feeds"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createFeedsWidget()
{
  feedsWidget_ = OptionsPageBuilder::buildTabs(PageFeeds, &searchEntries_, {
    {tr("General"), [this](OptionsPageBuilder &b) {
      b.check(updateFeedsStartUp_, tr("Automatically update feeds on startup"),
              tr("Refresh enabled feeds immediately when the application starts. "
                 "Otherwise, overdue scheduled feeds are checked after one minute."),
              QStringLiteral("refresh sync fetch launch"));
      b.row([this](OptionsRowBuilder &r) {
        r.check(updateFeedsEnable_, tr("Automatically update feeds every"),
                tr("Time spent with the application closed counts toward the interval. "
                   "Overdue feeds are refreshed after a one-minute startup grace period."),
                QStringLiteral("refresh sync fetch interval timer schedule"));
        r.spin(updateFeedsInterval_, 1, 9999);
        r.combo(updateIntervalType_, {tr("seconds"), tr("minutes"), tr("hours")});
        OptionsPageBuilder::bindEnabled(updateFeedsEnable_, {updateFeedsInterval_, updateIntervalType_});
      }, QMargins(0, 0, 0, 0));
      b.spacing(10);

      b.label(tr("Action when opening a feed:"));
      b.grid(QMargins(15, 0, 0, 10), 1, [this](OptionsGridBuilder &g) {
        g.radio(0, 0, positionLastNews_, tr("Focus the last opened article"), QStringLiteral("open feed select"));
        g.check(0, 1, openNewsWebViewOn_, tr("Open the article"), QString(), QStringLiteral("open feed"));
        g.radio(1, 0, positionFirstNews_, tr("Focus the first article in the list"), QStringLiteral("open feed select"));
        g.radio(2, 0, positionUnreadNews_, tr("Focus the first unread article"), QStringLiteral("open feed select"));
        g.radio(3, 0, nottoOpenNews_, tr("Do nothing"), QStringLiteral("open feed"));
        connect(nottoOpenNews_, &QAbstractButton::toggled, openNewsWebViewOn_, &QWidget::setDisabled);
      });

      b.check(markIdenticalNewsRead_, tr("Automatically mark identical news as read"), QString(),
              QStringLiteral("duplicate dedup crosspost"));
      b.spacing(15);

      b.group(avoidedOldNewsDateOn_, tr("Do not add articles published before this date to the database:"),
              true, [this](OptionsPageBuilder &grp) {
        avoidedOldNewsDate_ = new QCalendarWidget();
        avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
        avoidedOldNewsDate_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        avoidedOldNewsDate_->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
        grp.addWidget(avoidedOldNewsDate_);
        grp.stretch();
      }, Qt::Horizontal, QMargins(5, 5, 5, 5), QStringLiteral("date cutoff filter ignore old"));
      avoidedOldNewsDateOn_->setChecked(false);
      b.stretch();
    }},
    {tr("Display"), [this](OptionsPageBuilder &b) {
      b.check(alternatingRowColorsNews_, tr("Alternating row background colors"), QString(),
              QStringLiteral("zebra striping"));
      b.spacing(10);
      b.check(simplifiedDateTime_, tr("Simplified date and time format"), QString(),
              QStringLiteral("today yesterday relative"));

      b.row([this](OptionsRowBuilder &r) {
        const QString dateLabel = tr("Date display format:");
        const QString timeLabel = tr("time:");
        r.label(dateLabel);
        r.combo(formatDate_);
        const QPair<QString, const char *> dateFormats[] = {
          {QStringLiteral("31.12.99"), "dd.MM.yy"},
          {QStringLiteral("31.12.1999"), "dd.MM.yyyy"},
          {QStringLiteral("31. %1. 1999").arg(tr("Dec")), "dd. MMM. yyyy"},
          {QStringLiteral("31. %1 1999").arg(tr("December")), "dd. MMMM yyyy"},
          {QStringLiteral("99-12-31"), "yy-MM-dd"},
          {QStringLiteral("1999-12-31"), "yyyy-MM-dd"},
          {QStringLiteral("12/31/1999"), "MM/dd/yyyy"},
        };
        for (const auto &entry : dateFormats) {
          formatDate_->addItem(entry.first, QLatin1String(entry.second));
        }
        r.registerSearch(formatDate_, dateLabel, QString(), QStringLiteral("timestamp"));

        r.spacing(10);
        r.label(timeLabel);
        r.combo(formatTime_);
        const QPair<const char *, const char *> timeFormats[] = {
          {"13:37", "hh:mm"},
          {"13:37:09", "hh:mm:ss"},
          {"01:37 PM", "hh:mm AP"},
          {"01:37:09 PM", "hh:mm:ss AP"},
        };
        for (const auto &entry : timeFormats) {
          formatTime_->addItem(QLatin1String(entry.first), QLatin1String(entry.second));
        }
        r.registerSearch(formatTime_, dateLabel + QLatin1Char(' ') + timeLabel, QString(),
                         QStringLiteral("clock hour timestamp"));
      }, QMargins(0, 0, 0, 0));
      b.spacing(10);

      b.comboRow(mainNewsFilter_, tr("Default article filter:"), {},
                 QStringLiteral("unread starred"));
      const QPair<QString, const char *> filterItems[] = {
        {tr("Show All"), "filterNewsAll_"},
        {tr("Show New"), "filterNewsNew_"},
        {tr("Show Unread"), "filterNewsUnread_"},
        {tr("Show Starred"), "filterNewsStar_"},
        {tr("Show Not Starred"), "filterNewsNotStarred_"},
        {tr("Show Unread or Starred"), "filterNewsUnreadStar_"},
        {tr("Show Last Day"), "filterNewsLastDay_"},
        {tr("Show Last 7 Days"), "filterNewsLastWeek_"},
      };
      for (const auto &entry : filterItems) {
        mainNewsFilter_->addItem(entry.first, QLatin1String(entry.second));
      }
      b.spacing(10);

      const QString styleLabel = tr("Article style sheet:");
      b.label(styleLabel);
      b.grid(QMargins(15, 0, 5, 10), -1, [this](OptionsGridBuilder &g) {
        styleSheetNewsEdit_ = new LineEdit();
        auto *styleSheetNewsButton = new QPushButton(tr("Browse..."));
        connect(styleSheetNewsButton, &QAbstractButton::clicked, this, &OptionsDialog::selectionUserStyleNews);
        g.addWidget(0, 0, styleSheetNewsEdit_);
        g.addWidget(0, 1, styleSheetNewsButton, Qt::AlignRight);
      });
      b.registerSearch(styleSheetNewsEdit_, styleLabel, QString(), QStringLiteral("css theme custom style"));
      b.stretch();
    }},
    {tr("Reading"), [this](OptionsPageBuilder &b) {
      b.group(markNewsReadOn_, tr("Mark articles as read:"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this, &grp](OptionsRowBuilder &r) {
          markCurNewsRead_ = new QRadioButton(tr("when selected, after "), grp.container());
          r.addWidget(markCurNewsRead_);
          r.spin(markNewsReadTime_, 0, 100);
          r.label(tr("seconds"));
          OptionsPageBuilder::bindEnabled(markCurNewsRead_, {markNewsReadTime_});
          r.registerSearch(markCurNewsRead_,
                           tr("Mark articles as read:") + QLatin1Char(' ') + tr("when selected, after "),
                           QString(), QStringLiteral("auto read timer delay"));
        }, QMargins(0, 0, 0, 0));
        grp.radio(markPrevNewsRead_, tr("after switching to another article"),
                  QStringLiteral("auto read"));
      }, Qt::Vertical, std::nullopt, QStringLiteral("auto read"));
      b.check(markReadSwitchingFeed_, tr("Mark displayed news as read when switching feeds"), QString(),
              QStringLiteral("auto read"));
      b.check(markReadClosingTab_, tr("Mark displayed news as read when closing tab"), QString(),
              QStringLiteral("auto read"));
      b.check(markReadMinimize_, tr("Mark displayed news as read on minimize"), QString(),
              QStringLiteral("auto read"));
      b.spacing(10);

      b.label(tr("Prevent accidental deletion of:"));
      b.indented(QMargins(15, 0, 0, 10), [this](OptionsPageBuilder &sub) {
        sub.check(notDeleteStarred_, tr("starred news"), QString(), QStringLiteral("protect keep"));
        sub.check(notDeleteLabeled_, tr("labeled news"), QString(), QStringLiteral("protect keep"));
      });
      b.check(changeBehaviorActionNUN_, tr("Change behavior of the 'Next Unread Article' action"), QString(),
              QStringLiteral("next unread navigation"));
      b.stretch();
    }},
    {tr("Clean Up"), [this](OptionsPageBuilder &b) {
      b.group(cleanupOnShutdownBox_, tr("Enable cleanup on shutdown"), true, [this](OptionsPageBuilder &grp) {
        grp.grid(std::nullopt, 1, [this](OptionsGridBuilder &g) {
          g.check(0, 0, dayCleanUpOn_, tr("Maximum article age to keep (days):"), tr(
              "Removes expired articles on shutdown and ignores expired incoming articles on later updates. "
              "The first successful fetch of a new feed is exempt. Unread, starred and labeled articles "
              "are preserved according to the options below. Articles without a usable publication date "
              "use their received date for cleanup and retain a record to prevent reappearance."),
              QStringLiteral("retention expire prune"));

          auto *maxDayRow = new QHBoxLayout();
          maxDayRow->setContentsMargins(0, 0, 0, 0);
          maxDayCleanUp_ = new QSpinBox();
          maxDayCleanUp_->setRange(0, 9999);
          OptionsPageBuilder::bindEnabled(dayCleanUpOn_, {maxDayCleanUp_});

          const QString ageWarningText = tr(
              "Raising this limit may cause old articles you have already read or deleted "
              "to reappear as new if a feed still lists them.");
          maxDayCleanUpWarning_ = new QToolButton();
          maxDayCleanUpWarning_->setIcon(QIcon(QStringLiteral(":/images/warning")));
          maxDayCleanUpWarning_->setIconSize(QSize(16, 16));
          maxDayCleanUpWarning_->setAutoRaise(true);
          maxDayCleanUpWarning_->setCursor(Qt::PointingHandCursor);
          maxDayCleanUpWarning_->setToolTip(ageWarningText);
          maxDayCleanUpWarning_->setAccessibleName(tr("Article age limit warning"));
          maxDayCleanUpWarning_->hide();
          connect(maxDayCleanUpWarning_, &QAbstractButton::clicked, this, [this, ageWarningText]() {
            QMessageBox::warning(this, tr("Maximum article age to keep"), ageWarningText, QMessageBox::Ok);
          });
          maxDayRow->addWidget(maxDayCleanUp_);
          maxDayRow->addWidget(maxDayCleanUpWarning_);
          maxDayRow->addStretch();
          g.addLayout(0, 1, maxDayRow);

          g.check(1, 0, newsCleanUpOn_, tr("Maximum number of articles to keep:"), QString(),
                  QStringLiteral("limit count cap retention"));
          maxNewsCleanUp_ = new QSpinBox();
          maxNewsCleanUp_->setRange(0, 9999);
          OptionsPageBuilder::bindEnabled(newsCleanUpOn_, {maxNewsCleanUp_});
          g.addWidget(1, 1, maxNewsCleanUp_, Qt::AlignLeft);

          g.check(2, 0, readCleanUp_, tr("Delete read news"), QString(), QStringLiteral("purge prune"));
          g.check(3, 0, neverUnreadCleanUp_, tr("Never delete unread news"), QString(), QStringLiteral("preserve keep"));
          g.check(4, 0, neverStarCleanUp_, tr("Never delete starred news"), QString(), QStringLiteral("preserve keep"));
          g.check(5, 0, neverLabelCleanUp_, tr("Never delete labeled news"), QString(), QStringLiteral("preserve keep"));
        });
        grp.spacing(10);

        grp.check(cleanUpDeleted_, tr("Clean up 'Deleted'"), QString(), QStringLiteral("trash empty purge"));
        grp.check(optimizeDB_, tr("Optimize database on shutdown (slower shutdown)"), QString(),
                  QStringLiteral("vacuum sqlite compact defragment"));
        grp.spacing(10);

        grp.row([this](OptionsRowBuilder &r) {
          cleanUpPreviewLabel_ = new QLabel();
          r.addWidget(cleanUpPreviewLabel_);
          cleanUpPreviewButton_ = r.button(tr("Preview"));
          cleanUpPreviewButton_->setEnabled(false);
          connect(cleanUpPreviewButton_, &QAbstractButton::clicked,
                  this, &OptionsDialog::showCleanUpPreview);
        });
        grp.stretch();
      }, Qt::Vertical, std::nullopt, QStringLiteral("prune purge expire retention exit"));
    }},
  });

  connect(cleanupOnShutdownBox_, &QGroupBox::toggled, this, &OptionsDialog::updateCleanUpPreview);
  connect(cleanupOnShutdownBox_, &QGroupBox::toggled, this, &OptionsDialog::updateMaxDayCleanUpWarning);
  connect(dayCleanUpOn_, &QAbstractButton::toggled, this, &OptionsDialog::updateMaxDayCleanUpWarning);
  connect(maxDayCleanUp_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &OptionsDialog::updateMaxDayCleanUpWarning);
  for (QAbstractButton *btn : {static_cast<QAbstractButton *>(dayCleanUpOn_),
                               static_cast<QAbstractButton *>(newsCleanUpOn_),
                               static_cast<QAbstractButton *>(readCleanUp_),
                               static_cast<QAbstractButton *>(neverUnreadCleanUp_),
                               static_cast<QAbstractButton *>(neverStarCleanUp_),
                               static_cast<QAbstractButton *>(neverLabelCleanUp_),
                               static_cast<QAbstractButton *>(cleanUpDeleted_)}) {
    connect(btn, &QAbstractButton::toggled, this, &OptionsDialog::updateCleanUpPreview);
  }
  for (QSpinBox *spin : {maxDayCleanUp_, maxNewsCleanUp_}) {
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionsDialog::updateCleanUpPreview);
  }
  connect(feedsWidget_, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == 3) updateCleanUpPreview();
  });
  return feedsWidget_;
}

void OptionsDialog::onFeedsPageEntered()
{
  if (feedsWidget_ && feedsWidget_->currentIndex() == 3) {
    updateCleanUpPreview();
  }
}

void OptionsDialog::updateMaxDayCleanUpWarning()
{
  if (!maxDayCleanUpWarning_ || initialMaxDayCleanUp_ < 0) return;
  const bool raised = cleanupOnShutdownBox_->isChecked() &&
      dayCleanUpOn_->isChecked() &&
      maxDayCleanUp_->value() > initialMaxDayCleanUp_;
  maxDayCleanUpWarning_->setVisible(raised);
}

/** @brief Create widget "Labels"
 *----------------------------------------------------------------------------*/
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

/** @brief Create widget "Notifier"
 *----------------------------------------------------------------------------*/
QWidget *OptionsDialog::createNotifierWidget()
{
  feedsNotifierDlg_ = nullptr;

  notifierWidget_ = OptionsPageBuilder::buildTabs(PageNotifications, &searchEntries_, {
    {tr("Notification"), [this](OptionsPageBuilder &b) {
      b.group(showNotifyOn_, tr("Show notifications for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          const QString screenLabel = tr("Screen");
          r.label(screenLabel);
          QStringList screens{QStringLiteral("-1")};
          for (int i = 0; i < QGuiApplication::screens().size(); ++i) {
            screens << QString::number(i);
          }
          r.combo(screenNotify_, screens);
          screenNotify_->setCurrentIndex(1);
          r.registerSearch(screenNotify_, screenLabel, QString(), QStringLiteral("monitor display"));

          r.spacing(10);
          const QString positionLabel = tr("Position");
          r.label(positionLabel);
          r.combo(positionNotify_, {tr("Top Left"), tr("Top Right"), tr("Bottom Left"), tr("Bottom Right")});
          r.registerSearch(positionNotify_, positionLabel, QString(), QStringLiteral("corner location"));

          r.stretch(1);
          QPushButton *showNotifier = r.button(tr("Preview"));
          connect(showNotifier, &QAbstractButton::clicked, this, &OptionsDialog::showNotification);
        }, std::nullopt, false);

#if defined(Q_OS_WIN)
        grp.check(fullscreenModeNotify_, tr("Do not show notification in fullscreen mode"));
#else
        fullscreenModeNotify_ = new QCheckBox(this);
        fullscreenModeNotify_->hide();
#endif
        grp.check(showNotifyInactiveApp_, tr("Show when the main window is inactive"));
        grp.check(closeNotify_, tr("Close notification after opening an article"));
        grp.spacing(10);

        grp.grid(std::nullopt, 2, [this](OptionsGridBuilder &g) {
          g.addSpinRow(0, tr("Transparency"), transparencyNotify_, 0, 100, QStringLiteral("%"),
                       QString(), QStringLiteral("opacity alpha"));
          g.addSpinRow(1, tr("Show at most"), countShowNewsNotify_, 1, 30, tr("items per notification page"),
                       QString(), QStringLiteral("limit alerts"));
          g.addSpinRow(2, tr("List item width"), widthTitleNewsNotify_, 50, 500, tr("pixels"),
                       QString(), QStringLiteral("size"));
          g.addSpinRow(3, tr("Close notification after"), timeShowNewsNotify_, 0, 99999, tr("seconds"),
                       QString(), QStringLiteral("duration timeout delay"));
        });
        grp.spacing(10);

        grp.check(showTitlesFeedsNotify_, tr("Show feed titles"));
        grp.check(showIconFeedNotify_, tr("Show feed icons"));
        grp.check(showButtonMarkAllNotify_, tr("Show button 'Mark All News Read'"));
        grp.check(showButtonMarkReadNotify_, tr("Show button 'Mark Read/Unread'"));
        grp.check(showButtonExBrowserNotify_, tr("Show button 'Open in External Browser'"));
        grp.check(showButtonDeleteNotify_, tr("Show button 'Delete News'"));
        grp.spacing(10);

        grp.row([this](OptionsRowBuilder &r) {
          r.check(onlySelectedFeeds_, tr("Only show selected feeds:"), QString(),
                  QStringLiteral("filter feeds alert"));
          QPushButton *feedsNotifierButton = r.button(tr("Feeds"));
          OptionsPageBuilder::bindEnabled(onlySelectedFeeds_, {feedsNotifierButton});
          connect(feedsNotifierButton, &QAbstractButton::clicked, this, &OptionsDialog::openFeedsNotifierDlg);
          r.stretch(1);
        }, std::nullopt, false);
        grp.stretch(1);
      }, Qt::Vertical, QMargins(10, 10, 10, 10), QStringLiteral("popup toast alert banner"));
      showNotifyOn_->setChecked(false);
    }},
    {tr("Sound"), [this](OptionsPageBuilder &b) {
      b.group(soundNotifyBox_, tr("Play a sound for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          editSoundNotifier_ = new LineEdit();
          r.addWidget(editSoundNotifier_, 1);
          selectionSoundNotifier_ = r.button(tr("Browse..."));
          playSoundNotifier_ = r.button(tr("Play"));
          connect(selectionSoundNotifier_, &QAbstractButton::clicked, this, &OptionsDialog::selectionSoundNotifier);
          connect(playSoundNotifier_, &QAbstractButton::clicked, this, &OptionsDialog::slotPlaySoundNotifier);
          connect(this, &OptionsDialog::signalPlaySound,
                  qobject_cast<MainWindow *>(parent()), &MainWindow::slotPlaySound);
        }, std::nullopt, false);
        grp.stretch(1);
      }, Qt::Vertical, QMargins(10, 10, 10, 10), QStringLiteral("audio wav mp3 flac alert chime"));
      soundNotifyBox_->setChecked(false);
    }},
  });
  return notifierWidget_;
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
//----------------------------------------------------------------------------
void OptionsDialog::slotCategoriesTreeKeyUpDownPressed()
{
  slotCategoriesItemClicked(categoriesTree_->currentItem(), 1);
}
//----------------------------------------------------------------------------
void OptionsDialog::slotCategoriesItemClicked(QTreeWidgetItem* item, int)
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
//----------------------------------------------------------------------------
int OptionsDialog::currentIndex()
{
  if (QTreeWidgetItem *item = categoriesTree_->currentItem()) {
    const int pageIndex = item->data(0, Qt::DisplayRole).toInt();
    if (pageIndex >= 0 && pageIndex < PageCount) return pageIndex;
  }
  return lastPageIndex_;
}
//----------------------------------------------------------------------------
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
  case PageBrowser:       return browserWidget_;
  case PageFeeds:         return feedsWidget_;
  case PageNotifications: return notifierWidget_;
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
//----------------------------------------------------------------------------
QNetworkProxy OptionsDialog::proxy()
{
  return networkProxy_;
}
//----------------------------------------------------------------------------
void OptionsDialog::setProxy(const QNetworkProxy proxy)
{
  networkProxy_ = proxy;
  updateProxy();
}
//----------------------------------------------------------------------------
void OptionsDialog::updateProxy()
{
  switch (networkProxy_.type()) {
  case QNetworkProxy::HttpProxy: case QNetworkProxy::Socks5Proxy:
    manualProxyButton_->setChecked(true);
    if (networkProxy_.type() == QNetworkProxy::Socks5Proxy)
      typeProxy_->setCurrentIndex(1);
    break;
  case QNetworkProxy::NoProxy:
    directConnectionButton_->setChecked(true);
    break;
  case QNetworkProxy::DefaultProxy:
  default:
    systemProxyButton_->setChecked(true);
  }
  editHost_->setText(networkProxy_.hostName());
  editPort_->setText(QVariant(networkProxy_.port()).toString());
  editUser_->setText(networkProxy_.user());
  editPassword_->setText(networkProxy_.password());
}
//----------------------------------------------------------------------------
void OptionsDialog::applyProxy()
{
  if (systemProxyButton_->isChecked()) {
    networkProxy_.setType(QNetworkProxy::DefaultProxy);
  } else if (manualProxyButton_->isChecked()) {
    if (typeProxy_->currentIndex() == 1)
      networkProxy_.setType(QNetworkProxy::Socks5Proxy);
    else
      networkProxy_.setType(QNetworkProxy::HttpProxy);
  } else {
    networkProxy_.setType(QNetworkProxy::NoProxy);
  }
  networkProxy_.setHostName(editHost_->text());
  networkProxy_.setPort(editPort_->text().toInt());
  networkProxy_.setUser(editUser_->text());
  networkProxy_.setPassword(editPassword_->text());
}
//----------------------------------------------------------------------------
QString OptionsDialog::language() const
{
  return languagePage_ ? languagePage_->language() : selectedLanguage_;
}
//----------------------------------------------------------------------------
void OptionsDialog::setLanguage(const QString &langFileName)
{
  selectedLanguage_ = langFileName;
  if (languagePage_) languagePage_->setLanguage(langFileName);
}
//----------------------------------------------------------------------------
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
//----------------------------------------------------------------------------
void OptionsDialog::setBehaviorIconTray(int behavior)
{
  switch (behavior) {
  case CHANGE_ICON_TRAY:       changeIconTray_->setChecked(true);  break;
  case NEW_COUNT_ICON_TRAY:    newCountTray_->setChecked(true);    break;
  case UNREAD_COUNT_ICON_TRAY: unreadCountTray_->setChecked(true); break;
  default: staticIconTray_->setChecked(true);
  }
}
//----------------------------------------------------------------------------
int OptionsDialog::behaviorIconTray()
{
  if (staticIconTray_->isChecked())       return STATIC_ICON_TRAY;
  else if (changeIconTray_->isChecked())  return CHANGE_ICON_TRAY;
  else if (newCountTray_->isChecked())    return NEW_COUNT_ICON_TRAY;
  else if (unreadCountTray_->isChecked()) return UNREAD_COUNT_ICON_TRAY;
  else return STATIC_ICON_TRAY;
}
//----------------------------------------------------------------------------
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
//----------------------------------------------------------------------------
void OptionsDialog::setOpeningFeed(int action)
{
  switch (action) {
  case 1: positionFirstNews_->setChecked(true); break;
  case 2: nottoOpenNews_->setChecked(true); break;
  case 3: positionUnreadNews_->setChecked(true); break;
  default: positionLastNews_->setChecked(true);
  }
}
//----------------------------------------------------------------------------
int OptionsDialog::getOpeningFeed()
{
  if (positionLastNews_->isChecked())     return 0;
  else if (positionFirstNews_->isChecked()) return 1;
  else if (nottoOpenNews_->isChecked()) return 2;
  else if (positionUnreadNews_->isChecked()) return 3;
  else return 0;
}
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void OptionsDialog::selectionUserStyleNews()
{
  QString path(mainApp->resourcesDir() % "/" % ProjectMetadata::styles());
  QString fileName = QFileDialog::getOpenFileName(this,
                                                  tr("Select Style Sheet File"),
                                                  path, "*.css");
  if (!fileName.isEmpty())
    styleSheetNewsEdit_->setText(fileName);
}
//----------------------------------------------------------------------------
void OptionsDialog::selectionSoundNotifier()
{
  QString path;

  QFileInfo file(editSoundNotifier_->text());
  if (file.isFile()) path = editSoundNotifier_->text();
  else path = file.path();

  QString fileName = QFileDialog::getOpenFileName(this,
                                                  tr("Open File..."),
                                                  path,
                                                  tr("Audio Files (*.wav *.flac *.mp3);;All Files (*)")
                                                 );
  if (!fileName.isEmpty())
    editSoundNotifier_->setText(fileName);
}

void OptionsDialog::slotPlaySoundNotifier()
{
  if (!editSoundNotifier_->text().isEmpty())
    emit signalPlaySound(editSoundNotifier_->text());
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

/** @brief Show notifier test window
 *----------------------------------------------------------------------------*/
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

  connect(notificationWidget_, &NotificationWidget::signalClose, this, &OptionsDialog::deleteNotification);

  notificationWidget_->show();
}

/** @brief Destroy notifier test window
 *----------------------------------------------------------------------------*/
void OptionsDialog::deleteNotification()
{
  notificationWidget_->deleteLater();
  notificationWidget_ = nullptr;
}
//----------------------------------------------------------------------------
void OptionsDialog::selectionDownloadLocation()
{
  QString dirStr = QFileDialog::getExistingDirectory(this, tr("Open Directory..."),
                                                     downloadLocationEdit_->text(),
                                                     QFileDialog::ShowDirsOnly
                                                     | QFileDialog::DontResolveSymlinks);
  if (!dirStr.isEmpty())
    downloadLocationEdit_->setText(dirStr);
}

ShutdownCleanUpConfig OptionsDialog::currentCleanUpConfig() const
{
  ShutdownCleanUpConfig config;
  config.cleanupOn = cleanupOnShutdownBox_->isChecked();
  config.dayCleanUpOn = dayCleanUpOn_->isChecked();
  config.maxDayCleanUp = maxDayCleanUp_->value();
  config.newsCleanUpOn = newsCleanUpOn_->isChecked();
  config.maxNewsCleanUp = maxNewsCleanUp_->value();
  config.readCleanUp = readCleanUp_->isChecked();
  config.neverUnreadCleanUp = neverUnreadCleanUp_->isChecked();
  config.neverStarCleanUp = neverStarCleanUp_->isChecked();
  config.neverLabelCleanUp = neverLabelCleanUp_->isChecked();
  config.cleanUpDeleted = cleanUpDeleted_->isChecked();
  return config;
}

void OptionsDialog::updateCleanUpPreview()
{
  if (!categoriesTree_->currentItem() ||
      categoriesTree_->currentItem()->data(0, Qt::DisplayRole).toInt() != PageFeeds ||
      feedsWidget_->currentIndex() != 3) {
    return;
  }
  QElapsedTimer timer;
  timer.start();
  QString error;
  cleanUpPlanValid_ = ShutdownCleanUp::evaluate(db_, currentCleanUpConfig(), cleanUpPlan_, error);
  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::updateCleanUpPreview: %1 ms (cleanupOn=%2, valid=%3, total=%4)")
      .arg(timer.elapsed())
      .arg(cleanupOnShutdownBox_->isChecked())
      .arg(cleanUpPlanValid_)
      .arg(cleanUpPlan_.totalArticles());
  if (!cleanUpPlanValid_) {
    cleanUpPreviewLabel_->setText(tr("Cannot calculate shutdown cleanup preview: %1").arg(error));
    cleanUpPreviewButton_->setEnabled(false);
    return;
  }

  const int total = cleanUpPlan_.totalArticles();
  cleanUpPreviewLabel_->setText(
      tr("On shutdown %n article(s) will be deleted.", "", total));
  cleanUpPreviewButton_->setEnabled(cleanupOnShutdownBox_->isChecked() && total > 0);
}

void OptionsDialog::showCleanUpPreview()
{
  updateCleanUpPreview();
  if (!cleanUpPlanValid_ || cleanUpPlan_.totalArticles() <= 0) return;

  CleanUpPreviewDialog dialog(cleanUpPlan_, this);
  dialog.exec();
}

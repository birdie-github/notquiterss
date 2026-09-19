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
#include "projectmetadata.h"
#include "logfile.h"
#include "optionsdialog.h"
#include "backupsettingspage.h"
#include <QScreen>
#include <QWindow>
#include "articlecontent.h"
#include <QDesktopServices>

#include "mainapplication.h"
#include "labeldialog.h"
#include "settings.h"
#include "globals.h"

OptionsDialog::OptionsDialog(QWidget *parent)
  : Dialog(parent)
  , hadStoredCustomUserAgent_(false)
  , userAgentWarningAccepted_(false)
  , notificationWidget_(NULL)
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Options"));
  setSizeGripEnabled(true);

  db_ = QSqlDatabase::database();

  contentLabel_ = new QLabel();
  contentLabel_->setObjectName("contentLabel_");
  contentLabel_->setAlignment(Qt::AlignCenter);
  contentLabel_->setStyleSheet(
        QString("#contentLabel_ {border-bottom: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  contentLabel_->setMinimumHeight(36);
  contentLabel_->setMargin(4);
  QFont fontContentLabel = contentLabel_->font();
  fontContentLabel.setBold(true);
  fontContentLabel.setPointSize(fontContentLabel.pointSize()+2);
  contentLabel_->setFont(fontContentLabel);
  mainLayout->insertWidget(0, contentLabel_);

  categoriesTree_ = new QTreeWidget();
  categoriesTree_->setObjectName("categoriesTree");
  categoriesTree_->setStyleSheet("QTreeWidget::item { min-height: 20px; }"
                                 "QTreeWidget { padding: 1px; }");
  categoriesTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  categoriesTree_->setHeaderHidden(true);
  categoriesTree_->setColumnCount(3);
  categoriesTree_->setColumnHidden(0, true);
  categoriesTree_->header()->setStretchLastSection(false);
  categoriesTree_->header()->resizeSection(2, 5);
  categoriesTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  categoriesTree_->setMinimumWidth(150);
  QStringList treeItem;
  treeItem << "0" << tr("General");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "1" << tr("System Tray");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "2" << tr("Network");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "3" << tr("Article View");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "4" << tr("Feeds");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "5" << tr("Labels");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "6" << tr("Notifications");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "7" << tr("Passwords");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "8" << tr("Language");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "9" << tr("Fonts & Colors");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "10" << tr("Keyboard Shortcuts");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));

  treeItem.clear();
  treeItem << "11" << tr("Database Backup");
  categoriesTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));

  createGeneralWidget();

  createTraySystemWidget();

  createNetworkConnectionsWidget();

  createBrowserWidget();

  createFeedsWidget();

  createLabelsWidget();

  createNotifierWidget();

  createPasswordsWidget();

  createLanguageWidget();

  createFontsColorsWidget();

  createShortcutWidget();

  contentStack_ = new QStackedWidget();
  contentStack_->setObjectName("contentStack_");
  // Scroll only the current page, not a stack sized for its tallest hidden page.
  const auto addPage = [this](QWidget *page) {
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameStyle(QFrame::NoFrame);
    scrollArea->setWidget(page);
    contentStack_->addWidget(scrollArea);
  };
  addPage(generalWidget_);
  addPage(traySystemWidget_);
  addPage(networkConnectionsWidget_);
  addPage(browserWidget_);
  addPage(feedsWidget_);
  addPage(labelsWidget_);
  addPage(notifierWidget_);
  addPage(passwordsWidget_);
  addPage(languageWidget_);
  addPage(fontsColorsWidget_);
  addPage(shortcutWidget_);
  auto *backupPage = new BackupSettingsPage(this);
  addPage(backupPage);
  connect(this, &QDialog::accepted, backupPage, &BackupSettingsPage::save);

  QSplitter *splitter = new QSplitter();
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(categoriesTree_);
  splitter->addWidget(contentStack_);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  QList<int> sizes;
  sizes << 150 << 600;
  splitter->setSizes(sizes);

  pageLayout->addWidget(splitter, 1);
  pageLayout->setContentsMargins(10, 5, 10, 5);

  buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  connect(buttonBox, SIGNAL(accepted()), this, SLOT(acceptDialog()));
  connect(this, SIGNAL(finished(int)), this, SLOT(closeDialog()));

  connect(categoriesTree_, SIGNAL(itemPressed(QTreeWidgetItem*,int)),
          this, SLOT(slotCategoriesItemClicked(QTreeWidgetItem*,int)));
  connect(this, SIGNAL(signalCategoriesTreeKeyUpDownPressed()),
          SLOT(slotCategoriesTreeKeyUpDownPressed()), Qt::QueuedConnection);

  categoriesTree_->installEventFilter(this);

  setMinimumSize(500, 400);
  // Use native font/style metrics; a tall page can still scroll independently.
  ensurePolished();
  const QSize pageSize = generalWidget_->sizeHint().expandedTo(generalWidget_->minimumSizeHint());
  const QSize surroundingSize = sizeHint() - contentStack_->sizeHint();
  resize(QSize(700, 580).expandedTo(pageSize + surroundingSize));

  Settings settings;
  restoreGeometry(settings.value("options/geometry").toByteArray());
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
}

void OptionsDialog::acceptDialog()
{
  if (customUserAgent_->isChecked() && userAgentEdit_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("Custom User-Agent"),
                         tr("Enter a custom User-Agent or disable the custom User-Agent option."));
    return;
  }

  if (customExternalBrowser_->isChecked() && externalBrowser_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("External Browser"), tr("Enter a custom browser or select the system default browser."));
    return;
  }

#if defined(Q_OS_WIN)
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
#endif

  Settings userAgentSettings("Settings");
  const bool useCustomUserAgent = customUserAgent_->isChecked();
  const QString customUserAgent = userAgentEdit_->text().trimmed();
  userAgentSettings.setValue("useCustomUserAgent", useCustomUserAgent);
  if (useCustomUserAgent || hadStoredCustomUserAgent_ || userAgentWarningAccepted_)
    userAgentSettings.setValue("userAgent", customUserAgent);
  globals.setUserAgent(useCustomUserAgent, customUserAgent);

  applyProxy();
  applyLabels();
  applyNotifier();
  applyPass();
  accept();
}

void OptionsDialog::closeDialog()
{
  Settings settings;
  settings.setValue("options/geometry", saveGeometry());
}

bool OptionsDialog::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
    if (obj != editShortcut_) {
      if ((keyEvent->key() == Qt::Key_Up) ||
          (keyEvent->key() == Qt::Key_Down)) {
        if (obj == categoriesTree_)
          emit signalCategoriesTreeKeyUpDownPressed();
        else if (obj == shortcutTree_)
          emit signalShortcutTreeUpDownPressed();
      }
    } else {
      if ((keyEvent->key() < Qt::Key_Shift) ||
          (keyEvent->key() > Qt::Key_Alt)) {
        QString str;
        if ((keyEvent->modifiers() & Qt::ShiftModifier) ||
            (keyEvent->modifiers() & Qt::ControlModifier) ||
            (keyEvent->modifiers() & Qt::AltModifier))
          str.append(QKeySequence(keyEvent->modifiers()).toString());
        if (keyEvent->key() == Qt::Key_Backtab)
          str.append(QKeySequence(Qt::Key_Tab).toString());
        else
          str.append(QKeySequence(keyEvent->key()).toString());
        editShortcut_->setText(str);

        QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
        int row = index.row();
        QString shortcutStr = shortcutModel_->item(row, 2)->text();
        QList<QStandardItem *> treeItems;
        treeItems = shortcutModel_->findItems(shortcutStr, Qt::MatchFixedString, 2);
          if (!shortcutStr.isEmpty()) {
            for (int i = 0; i < treeItems.count(); i++) {
              if ((treeItems.count() == 2) || (treeItems.at(i)->row() == row)) {
                treeItems.at(i)->setData(shortcutModel_->item(0, 1)->data(Qt::ForegroundRole),
                                         Qt::ForegroundRole);
              }
            }
          }

        shortcutModel_->item(row, 2)->setText(str);
        shortcutModel_->item(row, 2)->setData(str);

        if (!str.isEmpty()) {
          treeItems = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
          if (treeItems.count() > 1) {
            for (int i = 0; i < treeItems.count(); i++) {
              if (treeItems.at(i)->row() != row) {
                warningShortcut_->setText(tr("Warning: key is already assigned to") +
                                          " '" +
                                          shortcutModel_->item(treeItems.at(i)->row(), 0)->text()
                                          + "'");
              }
              treeItems.at(i)->setData(QColor(Qt::red), Qt::ForegroundRole);
            }
          } else {
            warningShortcut_->clear();
          }
        }
      }
      return true;
    }
    return false;
  } else {
    return QDialog::eventFilter(obj, event);
  }
}

/** @brief Create windget "General"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createGeneralWidget()
{
  showSplashScreen_ = new QCheckBox(tr("Show splash screen on startup"));
  reopenFeedStartup_ = new QCheckBox(tr("Reopen last opened feeds on startup"));
  openNewTabNextToActive_ = new QCheckBox(tr("Open new tab next to active"));
  hideFeedsOpenTab_ = new QCheckBox(tr("Hide feeds tree when opening tabs"));
  showToggleFeedsTree_ = new QCheckBox(tr("Show feeds tree toggle"));
  defaultIconFeeds_ = new QCheckBox(tr("Show default RSS icon instead of each feed's icon"));
  autocollapseFolder_ = new QCheckBox(tr("Automatically collapse folders"));
  showCloseButtonTab_ = new QCheckBox(tr("Show a close button on each tab"));

  updateCheckEnabled_ = new QCheckBox(tr("Automatically check for updates"));
  storeDBMemory_ = new QCheckBox(tr("Store the database in memory (requires application restart)"));
  storeDBMemory_->setChecked(false);
  saveDBMemFileInterval_ = new QSpinBox();
  saveDBMemFileInterval_->setRange(1, 999);

  QHBoxLayout *saveDBMemFileLayout = new QHBoxLayout();
  saveDBMemFileLayout->setContentsMargins(15, 0, 0, 0);
  saveDBMemFileLayout->addWidget(new QLabel(tr("Save the in-memory database to disk every")));
  saveDBMemFileLayout->addWidget(saveDBMemFileInterval_);
  saveDBMemFileLayout->addWidget(new QLabel(tr("minutes")));
  saveDBMemFileLayout->addStretch();

  QWidget *saveDBMemFileWidget = new QWidget();
  saveDBMemFileWidget->setEnabled(false);
  saveDBMemFileWidget->setLayout(saveDBMemFileLayout);

  connect(storeDBMemory_, SIGNAL(toggled(bool)),
          saveDBMemFileWidget, SLOT(setEnabled(bool)));

  QVBoxLayout *generalLayout = new QVBoxLayout();
  generalLayout->addWidget(showSplashScreen_);
  generalLayout->addWidget(reopenFeedStartup_);
  generalLayout->addWidget(openNewTabNextToActive_);
  generalLayout->addWidget(hideFeedsOpenTab_);
  generalLayout->addWidget(showToggleFeedsTree_);
  generalLayout->addWidget(defaultIconFeeds_);
  generalLayout->addWidget(autocollapseFolder_);
  generalLayout->addWidget(showCloseButtonTab_);
  generalLayout->addSpacing(20);

#if defined(Q_OS_WIN)
  autoRunEnabled_ = new QCheckBox(tr("Run %1 at Windows startup").arg(QGuiApplication::applicationDisplayName()));
  autoRunSettings_ = new QSettings("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                                   QSettings::NativeFormat);
  bool isAutoRun;
  if (mainApp->isPortableAppsCom())
    isAutoRun = autoRunSettings_->value((QCoreApplication::applicationName() + "Portable"), false).toBool();
  else
    isAutoRun = autoRunSettings_->value(QCoreApplication::applicationName(), false).toBool();
  autoRunEnabled_->setChecked(isAutoRun);

  generalLayout->addWidget(autoRunEnabled_);
#endif

  generalLayout->addWidget(updateCheckEnabled_);
  generalLayout->addWidget(storeDBMemory_);
  generalLayout->addWidget(saveDBMemFileWidget);
  QGroupBox *debugGroup = new QGroupBox(tr("Debug"));
  QHBoxLayout *debugLayout = new QHBoxLayout(debugGroup);
  QCheckBox *fileLogging = new QCheckBox(tr("Enable file logging"));
  fileLogging->setChecked(LogFile::fileLoggingEnabled());
  fileLogging->setToolTip(tr("Takes effect immediately. Existing logs are kept when disabled."));
  QPushButton *showLog = new QPushButton(tr("Show log location"));
  debugLayout->addWidget(fileLogging);
  debugLayout->addWidget(showLog);
  debugLayout->addStretch();
  connect(fileLogging, &QCheckBox::toggled, this, [](bool enabled) {
    Settings().setValue("Settings/logFileOutput", enabled);
    LogFile::setFileLoggingEnabled(enabled);
  });
  connect(showLog, &QPushButton::clicked, this, []() { LogFile::showLocation(); });
  generalLayout->addWidget(debugGroup);
  generalLayout->addStretch(1);

  generalWidget_ = new QFrame();
  generalWidget_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  generalWidget_->setLayout(generalLayout);
}

/** @brief Create widget "System tray"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createTraySystemWidget()
{
  showTrayIconBox_ = new QGroupBox(tr("Show system tray icon"));
  showTrayIconBox_->setCheckable(true);

  startingTray_ = new QCheckBox(tr("starting %1").arg(QGuiApplication::applicationDisplayName()));
  minimizingTray_ = new QCheckBox(tr("minimizing %1").arg(QGuiApplication::applicationDisplayName()));
  closingTray_ = new QCheckBox(tr("closing %1").arg(QGuiApplication::applicationDisplayName()));
  QVBoxLayout *moveTrayLayout = new QVBoxLayout();
  moveTrayLayout->setContentsMargins(15, 0, 5, 10);
  moveTrayLayout->addWidget(startingTray_);
  moveTrayLayout->addWidget(minimizingTray_);
  moveTrayLayout->addWidget(closingTray_);

  staticIconTray_ = new QRadioButton(tr("Show a static icon"));
  changeIconTray_ = new QRadioButton(tr("Change icon when new articles arrive"));
  newCountTray_ = new QRadioButton(tr("Show number of new articles"));
  unreadCountTray_ = new QRadioButton(tr("Show number of unread articles"));
  QVBoxLayout *behaviorLayout = new QVBoxLayout();
  behaviorLayout->setContentsMargins(15, 0, 5, 10);
  behaviorLayout->addWidget(staticIconTray_);
  behaviorLayout->addWidget(changeIconTray_);
  behaviorLayout->addWidget(newCountTray_);
  behaviorLayout->addWidget(unreadCountTray_);

  singleClickTray_ = new QCheckBox(
        tr("Use a single click instead of a double click to show the window"));
  clearStatusNew_ = new QCheckBox(tr("Clear new status when minimizing to tray"));
  emptyWorking_ = new QCheckBox(tr("Empty working set when minimizing to tray"));

  QVBoxLayout *trayLayout = new QVBoxLayout(showTrayIconBox_);
#ifndef Q_OS_MAC
  trayLayout->addWidget(new QLabel(tr("Move to the system tray when:")));
  trayLayout->addLayout(moveTrayLayout);
#endif
  trayLayout->addWidget(new QLabel(tr("Tray icon behavior:")));
  trayLayout->addLayout(behaviorLayout);
#ifndef Q_OS_MAC
  trayLayout->addWidget(singleClickTray_);
  trayLayout->addWidget(clearStatusNew_);
#endif
#if defined(Q_OS_WIN)
  trayLayout->addWidget(emptyWorking_);
#endif
  trayLayout->addStretch(1);

  QVBoxLayout *boxTrayLayout = new QVBoxLayout();
  boxTrayLayout->addWidget(showTrayIconBox_);

  traySystemWidget_ = new QFrame();
  traySystemWidget_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  traySystemWidget_->setLayout(boxTrayLayout);
}

/** @brief Create widget "Network connections"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createNetworkConnectionsWidget()
{
  directConnectionButton_ = new QRadioButton(
        tr("Direct connection to the Internet"));
  systemProxyButton_ = new QRadioButton(
        tr("System proxy configuration (if available)"));
  manualProxyButton_ = new QRadioButton(tr("Manual proxy configuration:"));

  QVBoxLayout *networkConnectionsLayout = new QVBoxLayout();
  networkConnectionsLayout->addWidget(directConnectionButton_);
  networkConnectionsLayout->addWidget(systemProxyButton_);
  networkConnectionsLayout->addWidget(manualProxyButton_);

  typeProxy_ = new QComboBox();
  typeProxy_->addItems(QStringList() << "HTTP" << "SOCKS5");
  editHost_ = new LineEdit();
  editPort_ = new LineEdit();
  editPort_->setFixedWidth(60);
  editUser_ = new LineEdit();
  editPassword_ = new LineEdit();
  editPassword_->setEchoMode(QLineEdit::Password);

  QHBoxLayout *addrPortLayout = new QHBoxLayout();
  addrPortLayout->setContentsMargins(0, 0, 0, 0);
  addrPortLayout->addWidget(typeProxy_);
  addrPortLayout->addWidget(new QLabel(tr("Proxy server:")));
  addrPortLayout->addWidget(editHost_);
  addrPortLayout->addWidget(new QLabel(tr("Port:")));
  addrPortLayout->addWidget(editPort_);

  QWidget *addrPortWidget = new QWidget();
  addrPortWidget->setLayout(addrPortLayout);

  QHBoxLayout *userPasswordLayout = new QHBoxLayout();
  userPasswordLayout->setContentsMargins(0, 0, 0, 0);
  userPasswordLayout->addWidget(new QLabel(tr("Username:")));
  userPasswordLayout->addWidget(editUser_);
  userPasswordLayout->addWidget(new QLabel(tr("Password:")));
  userPasswordLayout->addWidget(editPassword_);

  QWidget *userPasswordWidget = new QWidget();
  userPasswordWidget->setLayout(userPasswordLayout);

  QVBoxLayout *manualLayout = new QVBoxLayout();
  manualLayout->setContentsMargins(0, 0, 0, 0);
  manualLayout->addWidget(addrPortWidget);
  manualLayout->addWidget(userPasswordWidget);
  manualLayout->addStretch();

  manualWidget_ = new QWidget();
  manualWidget_->setEnabled(false);  // cause proper radio-button isn't set on creation
  manualWidget_->setLayout(manualLayout);

  networkConnectionsLayout->addWidget(manualWidget_);
  networkConnectionsLayout->addSpacing(20);

  customUserAgent_ = new QCheckBox(tr("Use custom User-Agent"));
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

  QVBoxLayout *userAgentLayout = new QVBoxLayout();
  userAgentLayout->setContentsMargins(15, 0, 5, 0);
  userAgentLayout->addWidget(userAgentEdit_);

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

  timeoutRequest_ = new QSpinBox();
  timeoutRequest_->setRange(0, 300);
  numberRequests_ = new QSpinBox();
  numberRequests_->setRange(1, 10);
  numberRepeats_ = new QSpinBox();
  numberRepeats_->setRange(1, 10);

  QGridLayout *requestLayout = new QGridLayout();
  requestLayout->setColumnStretch(1, 1);
  requestLayout->setContentsMargins(15, 0, 5, 0);
  requestLayout->addWidget(new QLabel(tr("Request timeout:")), 0, 0);
  requestLayout->addWidget(timeoutRequest_, 0, 1, 1, 1, Qt::AlignLeft);
  requestLayout->addWidget(new QLabel(tr("Number of requests:")), 1, 0);
  requestLayout->addWidget(numberRequests_, 1, 1, 1, 1, Qt::AlignLeft);
  requestLayout->addWidget(new QLabel(tr("Number of retries:")), 2, 0);
  requestLayout->addWidget(numberRepeats_, 2, 1, 1, 1, Qt::AlignLeft);

  networkConnectionsLayout->addWidget(new QLabel(tr("Network options for feed updates (requires application restart):")));
  networkConnectionsLayout->addLayout(requestLayout);
  networkConnectionsLayout->addSpacing(20);
  networkConnectionsLayout->addWidget(customUserAgent_);
  networkConnectionsLayout->addLayout(userAgentLayout);
  networkConnectionsLayout->addStretch(1);

  networkConnectionsWidget_ = new QFrame();
  networkConnectionsWidget_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  networkConnectionsWidget_->setLayout(networkConnectionsLayout);

  connect(manualProxyButton_, SIGNAL(toggled(bool)),
          this, SLOT(manualProxyToggle(bool)));
}

/** @brief Create widget "Browser"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createBrowserWidget()
{
  QRadioButton *systemBrowser = new QRadioButton(tr("System default web browser"));
  customExternalBrowser_ = new QRadioButton(tr("Custom web browser"));
  QButtonGroup *browserGroup = new QButtonGroup(this);
  browserGroup->addButton(systemBrowser);
  browserGroup->addButton(customExternalBrowser_);
  systemBrowser->setChecked(true);
  externalBrowser_ = new LineEdit();
#ifdef Q_OS_MAC
  externalBrowser_->setPlaceholderText(tr("Application name or application bundle path"));
#else
  externalBrowser_->setPlaceholderText(tr("Browser command, including any arguments"));
  externalBrowser_->setToolTip(tr("Use double quotes around paths containing spaces. The article URL is appended automatically."));
#endif
  QPushButton *browseBrowser = new QPushButton(tr("Browse..."));
  externalBrowser_->setEnabled(false);
  browseBrowser->setEnabled(false);
  connect(customExternalBrowser_, &QRadioButton::toggled,
          externalBrowser_, &QWidget::setEnabled);
  connect(customExternalBrowser_, &QRadioButton::toggled,
          browseBrowser, &QWidget::setEnabled);
  connect(browseBrowser, &QPushButton::clicked, this, [this]() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Browser"), QString(), tr("All files (*)"),
        nullptr, QFileDialog::DontResolveSymlinks);
    if (!file.isEmpty()) externalBrowser_->setText(QDir::toNativeSeparators(file));
  });
  QHBoxLayout *browserCommandLayout = new QHBoxLayout();
  browserCommandLayout->addWidget(externalBrowser_, 1);
  browserCommandLayout->addWidget(browseBrowser);

  autoLoadImages_ = new QCheckBox(tr("Load images"));
  defaultZoomPages_ = new QSpinBox();
  defaultZoomPages_->setRange(30, 300);
  defaultZoomPages_->setSuffix(" %");
  QHBoxLayout *zoomLayout = new QHBoxLayout();
  zoomLayout->addWidget(new QLabel(tr("Default article zoom:")));
  zoomLayout->addWidget(defaultZoomPages_);
  zoomLayout->addStretch();
  QVBoxLayout *articleLayout = new QVBoxLayout();
  articleLayout->addWidget(autoLoadImages_);
  articleLayout->addLayout(zoomLayout);
  articleLayout->addSpacing(12);
  articleLayout->addWidget(new QLabel(tr("Open links in:")));
  articleLayout->addWidget(systemBrowser);
  articleLayout->addWidget(customExternalBrowser_);
  articleLayout->addLayout(browserCommandLayout);
  articleLayout->addStretch();
  QWidget *articleWidget = new QWidget();
  articleWidget->setLayout(articleLayout);

  downloadLocationEdit_ = new LineEdit();
  QPushButton *browse = new QPushButton(tr("Browse..."));
  connect(browse, SIGNAL(clicked()), this, SLOT(selectionDownloadLocation()));
  askDownloadLocation_ = new QCheckBox(tr("Ask where to save each file before downloading"));
  QHBoxLayout *location = new QHBoxLayout();
  location->addWidget(downloadLocationEdit_);
  location->addWidget(browse);
  QVBoxLayout *downloadsLayout = new QVBoxLayout();
  downloadsLayout->addWidget(new QLabel(tr("Download location:")));
  downloadsLayout->addLayout(location);
  downloadsLayout->addWidget(askDownloadLocation_);
  downloadsLayout->addStretch();
  QWidget *downloads = new QWidget();
  downloads->setLayout(downloadsLayout);
  browserWidget_ = new QTabWidget();
  browserWidget_->addTab(articleWidget, tr("Articles"));
  browserWidget_->addTab(downloads, tr("Downloads"));
}

/** @brief Create windet "Feeds"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createFeedsWidget()
{
//! tab "General"
  updateFeedsStartUp_ = new QCheckBox(
        tr("Automatically update the feeds on startup"));
  updateFeedsEnable_ = new QCheckBox(tr("Automatically update the feeds every"));
  updateFeedsInterval_ = new QSpinBox();
  updateFeedsInterval_->setEnabled(false);
  updateFeedsInterval_->setRange(1, 9999);
  connect(updateFeedsEnable_, SIGNAL(toggled(bool)),
          updateFeedsInterval_, SLOT(setEnabled(bool)));

  updateIntervalType_ = new QComboBox(this);
  updateIntervalType_->setEnabled(false);
  QStringList intervalList;
  intervalList << tr("seconds") << tr("minutes")  << tr("hours");
  updateIntervalType_->addItems(intervalList);
  connect(updateFeedsEnable_, SIGNAL(toggled(bool)),
          updateIntervalType_, SLOT(setEnabled(bool)));

  QHBoxLayout *updateFeedsLayout = new QHBoxLayout();
  updateFeedsLayout->setContentsMargins(0, 0, 0, 0);
  updateFeedsLayout->addWidget(updateFeedsEnable_);
  updateFeedsLayout->addWidget(updateFeedsInterval_);
  updateFeedsLayout->addWidget(updateIntervalType_);
  updateFeedsLayout->addStretch();

  positionLastNews_ = new QRadioButton(tr("Focus the last opened article"));
  positionFirstNews_ = new QRadioButton(tr("Focus the first article in the list"));
  positionUnreadNews_ = new QRadioButton(tr("Focus the first unread article"));
  openNewsWebViewOn_ = new QCheckBox(tr("Open the article"));
  nottoOpenNews_ = new QRadioButton(tr("Do nothing"));
  connect(nottoOpenNews_, SIGNAL(toggled(bool)),
          openNewsWebViewOn_, SLOT(setDisabled(bool)));

  QGridLayout *openingFeedsLayout = new QGridLayout();
  openingFeedsLayout->setContentsMargins(15, 0, 0, 10);
  openingFeedsLayout->setColumnStretch(1, 1);
  openingFeedsLayout->addWidget(positionLastNews_, 0, 0, 1, 1);
  openingFeedsLayout->addWidget(openNewsWebViewOn_, 0, 1, 1, 1);
  openingFeedsLayout->addWidget(positionFirstNews_, 1, 0, 1, 1);
  openingFeedsLayout->addWidget(positionUnreadNews_, 2, 0, 1, 1);
  openingFeedsLayout->addWidget(nottoOpenNews_, 3, 0, 1, 1);

  markIdenticalNewsRead_ = new QCheckBox(tr("Automatically mark identical news as read"));

  avoidedOldNewsDate_ = new QCalendarWidget();
  avoidedOldNewsDate_->setSelectedDate(QDate::currentDate());
  avoidedOldNewsDate_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  avoidedOldNewsDate_->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
  QHBoxLayout *avoidedOldNewsDateLayout = new QHBoxLayout();
  avoidedOldNewsDateLayout->setContentsMargins(5, 5, 5, 5);
  avoidedOldNewsDateLayout->addWidget(avoidedOldNewsDate_);
  avoidedOldNewsDateLayout->addStretch();

  avoidedOldNewsDateOn_ = new QGroupBox(tr("Do not add articles published before this date to the database:"));
  avoidedOldNewsDateOn_->setCheckable(true);
  avoidedOldNewsDateOn_->setChecked(false);
  avoidedOldNewsDateOn_->setLayout(avoidedOldNewsDateLayout);

  QVBoxLayout *generalFeedsLayout = new QVBoxLayout();
  generalFeedsLayout->addWidget(updateFeedsStartUp_);
  generalFeedsLayout->addLayout(updateFeedsLayout);
  generalFeedsLayout->addSpacing(10);
  generalFeedsLayout->addWidget(new QLabel(tr("Action on feed opening:")));
  generalFeedsLayout->addLayout(openingFeedsLayout);
  generalFeedsLayout->addWidget(markIdenticalNewsRead_);
  generalFeedsLayout->addSpacing(15);
  generalFeedsLayout->addWidget(avoidedOldNewsDateOn_);
  generalFeedsLayout->addStretch();

  QWidget *generalFeedsWidget = new QWidget();
  generalFeedsWidget->setLayout(generalFeedsLayout);

  //! tab "Display"
  QStringList itemList;
  itemList << "31.12.99" << "31.12.1999"
           << QString("31. %1. 1999").arg(tr("Dec"))
           << QString("31. %1 1999").arg(tr("December"))
           << "99-12-31" << "1999-12-31" << "12/31/1999";
  formatDate_ = new QComboBox(this);
  formatDate_->addItems(itemList);
  formatDate_->setItemData(0, "dd.MM.yy");
  formatDate_->setItemData(1, "dd.MM.yyyy");
  formatDate_->setItemData(2, "dd. MMM. yyyy");
  formatDate_->setItemData(3, "dd. MMMM yyyy");
  formatDate_->setItemData(4, "yy-MM-dd");
  formatDate_->setItemData(5, "yyyy-MM-dd");
  formatDate_->setItemData(6, "MM/dd/yyyy");

  itemList.clear();
  itemList << "13:37"  << "13:37:09" << "01:37 PM"<< "01:37:09 PM";
  formatTime_ = new QComboBox(this);
  formatTime_->addItems(itemList);
  formatTime_->setItemData(0, "hh:mm");
  formatTime_->setItemData(1, "hh:mm:ss");
  formatTime_->setItemData(2, "hh:mm AP");
  formatTime_->setItemData(3, "hh:mm:ss AP");

  QHBoxLayout *formatDateLayout = new QHBoxLayout();
  formatDateLayout->setContentsMargins(0, 0, 0, 0);
  formatDateLayout->addWidget(new QLabel(tr("Date display format:")));
  formatDateLayout->addWidget(formatDate_);
  formatDateLayout->addSpacing(10);
  formatDateLayout->addWidget(new QLabel(tr("time:")));
  formatDateLayout->addWidget(formatTime_);
  formatDateLayout->addStretch();

  alternatingRowColorsNews_ = new QCheckBox(tr("Alternating row background colors"));
  simplifiedDateTime_ = new QCheckBox(tr("Simplified representation of date and time"));

  itemList.clear();
  itemList << tr("Show All")  << tr("Show New") << tr("Show Unread")
           << tr("Show Starred") << tr("Show Not Starred")
           << tr("Show Unread or Starred") << tr("Show Last Day")
           << tr("Show Last 7 Days");
  mainNewsFilter_ = new QComboBox(this);
  mainNewsFilter_->addItems(itemList);
  mainNewsFilter_->setItemData(0, "filterNewsAll_");
  mainNewsFilter_->setItemData(1, "filterNewsNew_");
  mainNewsFilter_->setItemData(2, "filterNewsUnread_");
  mainNewsFilter_->setItemData(3, "filterNewsStar_");
  mainNewsFilter_->setItemData(4, "filterNewsNotStarred_");
  mainNewsFilter_->setItemData(5, "filterNewsUnreadStar_");
  mainNewsFilter_->setItemData(6, "filterNewsLastDay_");
  mainNewsFilter_->setItemData(7, "filterNewsLastWeek_");

  QHBoxLayout *mainNewsFilterLayout = new QHBoxLayout();
  mainNewsFilterLayout->setContentsMargins(0, 0, 0, 0);
  mainNewsFilterLayout->addWidget(new QLabel(tr("Default article filter:")));
  mainNewsFilterLayout->addWidget(mainNewsFilter_);
  mainNewsFilterLayout->addStretch();

  styleSheetNewsEdit_ = new LineEdit();
  QPushButton *styleSheetNewsButton = new QPushButton(tr("Browse..."));
  connect(styleSheetNewsButton, SIGNAL(clicked()),
          this, SLOT(selectionUserStyleNews()));

  QGridLayout *styleSheetNewsLayout = new QGridLayout();
  styleSheetNewsLayout->setContentsMargins(15, 0, 5, 10);
  styleSheetNewsLayout->addWidget(styleSheetNewsEdit_, 0, 0);
  styleSheetNewsLayout->addWidget(styleSheetNewsButton, 0, 1, Qt::AlignRight);

  QVBoxLayout *displayFeedsLayout = new QVBoxLayout();
  displayFeedsLayout->addWidget(alternatingRowColorsNews_);
  displayFeedsLayout->addSpacing(10);
  displayFeedsLayout->addWidget(simplifiedDateTime_);
  displayFeedsLayout->addLayout(formatDateLayout);
  displayFeedsLayout->addSpacing(10);
  displayFeedsLayout->addLayout(mainNewsFilterLayout);
  displayFeedsLayout->addSpacing(10);
  displayFeedsLayout->addWidget(new QLabel(tr("Article style sheet:")));
  displayFeedsLayout->addLayout(styleSheetNewsLayout);
  displayFeedsLayout->addStretch();

  QWidget *displayFeedsWidget = new QWidget();
  displayFeedsWidget->setLayout(displayFeedsLayout);

  //! tab "Reading"
  QVBoxLayout* readingMainLayout = new QVBoxLayout();

  {
    markNewsReadOn_ = new QGroupBox(tr("Mark articles as read:"));

    markNewsReadOn_->setCheckable(true);

    {
      QVBoxLayout* radioLayout = new QVBoxLayout();

      {
        QHBoxLayout* curLayout = new QHBoxLayout();

        markCurNewsRead_ = new QRadioButton(tr("when selected, after "));

        markNewsReadTime_ = new QSpinBox();
        markNewsReadTime_->setEnabled(false);
        markNewsReadTime_->setRange(0, 100);
        connect(markCurNewsRead_, SIGNAL(toggled(bool)),
          markNewsReadTime_, SLOT(setEnabled(bool)));

        curLayout->setContentsMargins(0, 0, 0, 0);
        curLayout->addWidget(markCurNewsRead_);
        curLayout->addWidget(markNewsReadTime_);
        curLayout->addWidget(new QLabel(tr("seconds")));
        curLayout->addStretch();

        radioLayout->addLayout(curLayout);
      }

      markPrevNewsRead_ = new QRadioButton(tr("after switching to another article"));

      radioLayout->addWidget(markPrevNewsRead_);
      markNewsReadOn_->setLayout(radioLayout);
    }

    markReadSwitchingFeed_ = new QCheckBox(tr("Mark displayed news as read when switching feeds"));
    markReadClosingTab_ = new QCheckBox(tr("Mark displayed news as read when closing tab"));
    markReadMinimize_ = new QCheckBox(tr("Mark displayed news as read on minimize"));

    QGridLayout* curLayout = new QGridLayout();

    notDeleteStarred_ = new QCheckBox(tr("starred news"));
    notDeleteLabeled_ = new QCheckBox(tr("labeled news"));

    curLayout->setContentsMargins(15, 0, 0, 10);
    curLayout->addWidget(notDeleteStarred_, 0, 0, 1, 1);
    curLayout->addWidget(notDeleteLabeled_, 1, 0, 1, 1);

    changeBehaviorActionNUN_ = new QCheckBox(tr("Change behavior of the 'Next Unread Article' action"));

    readingMainLayout->addWidget(markNewsReadOn_);
    readingMainLayout->addWidget(markReadSwitchingFeed_);
    readingMainLayout->addWidget(markReadClosingTab_);
    readingMainLayout->addWidget(markReadMinimize_);
    readingMainLayout->addSpacing(10);
    readingMainLayout->addWidget(new QLabel(tr("Prevent accidental deletion of:")));
    readingMainLayout->addLayout(curLayout);
    readingMainLayout->addWidget(changeBehaviorActionNUN_);
    readingMainLayout->addStretch();
  }

  QWidget* readingFeedsWidget = new QWidget();
  readingFeedsWidget->setLayout(readingMainLayout);

//! tab "Clean Up"
  QWidget *cleanUpFeedsWidget = new QWidget();

  cleanupOnShutdownBox_ = new QGroupBox(tr("Enable cleanup on shutdown"));
  cleanupOnShutdownBox_->setCheckable(true);

  dayCleanUpOn_ = new QCheckBox(tr("Maximum article age to keep (days):"));
  dayCleanUpOn_->setToolTip(tr(
      "Removes expired articles on shutdown and ignores expired incoming articles on later updates. "
      "The first successful fetch of a new feed is exempt. Unread, starred and labeled articles "
      "are preserved according to the options below. Articles without a usable publication date "
      "use their received date for cleanup and retain a record to prevent reappearance."));
  maxDayCleanUp_ = new QSpinBox();
  maxDayCleanUp_->setEnabled(false);
  maxDayCleanUp_->setRange(0, 9999);
  connect(dayCleanUpOn_, SIGNAL(toggled(bool)),
          maxDayCleanUp_, SLOT(setEnabled(bool)));

  newsCleanUpOn_ = new QCheckBox(tr("Maximum number of articles to keep:"));
  maxNewsCleanUp_ = new QSpinBox();
  maxNewsCleanUp_->setEnabled(false);
  maxNewsCleanUp_->setRange(0, 9999);
  connect(newsCleanUpOn_, SIGNAL(toggled(bool)),
          maxNewsCleanUp_, SLOT(setEnabled(bool)));

  readCleanUp_ = new QCheckBox(tr("Delete read news"));
  neverUnreadCleanUp_ = new QCheckBox(tr("Never delete unread news"));
  neverStarCleanUp_ = new QCheckBox(tr("Never delete starred news"));
  neverLabelCleanUp_ = new QCheckBox(tr("Never delete labeled news"));

  cleanUpDeleted_ = new QCheckBox(tr("Clean up 'Deleted'"));
  optimizeDB_ = new QCheckBox(tr("Optimize database on shutdown (slower shutdown)"));

  QGridLayout *cleanUpFeedsLayout = new QGridLayout();
  cleanUpFeedsLayout->setColumnStretch(1, 1);
  cleanUpFeedsLayout->addWidget(dayCleanUpOn_, 0, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(maxDayCleanUp_, 0, 1, 1, 1, Qt::AlignLeft);
  cleanUpFeedsLayout->addWidget(newsCleanUpOn_, 1, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(maxNewsCleanUp_, 1, 1, 1, 1, Qt::AlignLeft);
  cleanUpFeedsLayout->addWidget(readCleanUp_, 2, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverUnreadCleanUp_, 3, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverStarCleanUp_, 4, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverLabelCleanUp_, 5, 0, 1, 1);

  QVBoxLayout *cleanUpFeedsLayout2 = new QVBoxLayout();
  cleanUpFeedsLayout2->addWidget(cleanUpDeleted_);
  cleanUpFeedsLayout2->addWidget(optimizeDB_);

  QVBoxLayout *cleanUpFeedsLayout3 = new QVBoxLayout(cleanupOnShutdownBox_);
  cleanUpFeedsLayout3->addLayout(cleanUpFeedsLayout);
  cleanUpFeedsLayout3->addSpacing(10);
  cleanUpFeedsLayout3->addLayout(cleanUpFeedsLayout2);
  cleanUpFeedsLayout3->addStretch();

  QVBoxLayout *boxCleanUpFeedsLayout = new QVBoxLayout(cleanUpFeedsWidget);
  boxCleanUpFeedsLayout->addWidget(cleanupOnShutdownBox_);

  feedsWidget_ = new QTabWidget();
  feedsWidget_->addTab(generalFeedsWidget, tr("General"));
  feedsWidget_->addTab(displayFeedsWidget, tr("Display"));
  feedsWidget_->addTab(readingFeedsWidget, tr("Reading"));
  feedsWidget_->addTab(cleanUpFeedsWidget, tr("Clean Up"));
}

/** @brief Create widget "Labels"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createLabelsWidget()
{
  labelsTree_ = new QTreeWidget(this);
  labelsTree_->setObjectName("labelsTree_");
  labelsTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  labelsTree_->setColumnCount(5);
  labelsTree_->setColumnHidden(0, true);
  labelsTree_->setColumnHidden(2, true);
  labelsTree_->setColumnHidden(3, true);
  labelsTree_->setColumnHidden(4, true);
  labelsTree_->header()->hide();

  newLabelButton_ = new QPushButton(tr("Create..."), this);
  connect(newLabelButton_, SIGNAL(clicked()), this, SLOT(newLabel()));
  editLabelButton_ = new QPushButton(tr("Edit..."), this);
  editLabelButton_->setEnabled(false);
  connect(editLabelButton_, SIGNAL(clicked()), this, SLOT(editLabel()));
  deleteLabelButton_ = new QPushButton(tr("Delete..."), this);
  deleteLabelButton_->setEnabled(false);
  connect(deleteLabelButton_, SIGNAL(clicked()), this, SLOT(deleteLabel()));

  moveUpLabelButton_ = new QPushButton(tr("Move up"), this);
  moveUpLabelButton_->setEnabled(false);
  connect(moveUpLabelButton_, SIGNAL(clicked()), this, SLOT(moveUpLabel()));
  moveDownLabelButton_ = new QPushButton(tr("Move down"), this);
  moveDownLabelButton_->setEnabled(false);
  connect(moveDownLabelButton_, SIGNAL(clicked()), this, SLOT(moveDownLabel()));

  QVBoxLayout *buttonsLayout = new QVBoxLayout();
  buttonsLayout->addWidget(newLabelButton_);
  buttonsLayout->addWidget(editLabelButton_);
  buttonsLayout->addWidget(deleteLabelButton_);
  buttonsLayout->addSpacing(10);
  buttonsLayout->addWidget(moveUpLabelButton_);
  buttonsLayout->addWidget(moveDownLabelButton_);
  buttonsLayout->addStretch();

  QHBoxLayout *labelsLayout = new QHBoxLayout();
  labelsLayout->setContentsMargins(0, 0, 0, 0);
  labelsLayout->addWidget(labelsTree_);
  labelsLayout->addLayout(buttonsLayout);

  labelsWidget_ = new QWidget(this);
  labelsWidget_->setLayout(labelsLayout);

  connect(labelsTree_, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
          this, SLOT(slotCurrentLabelChanged(QTreeWidgetItem*,QTreeWidgetItem*)));
  connect(labelsTree_, SIGNAL(doubleClicked(QModelIndex)),
          this, SLOT(editLabel()));

  loadLabelsOk_ = false;
}

/** @brief Create widget "Notifier"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createNotifierWidget()
{
  showNotifyOn_ = new QGroupBox(tr("Show notifications for incoming articles"));
  showNotifyOn_->setCheckable(true);
  showNotifyOn_->setChecked(false);

  screenNotify_ = new QComboBox();
  screenNotify_->addItem("-1");
  for (int i = 0; i < QGuiApplication::screens().size(); ++i) {
    screenNotify_->addItem(QString::number(i));
  }
  screenNotify_->setCurrentIndex(1);

  positionNotify_ = new QComboBox();
  QStringList positionList;
  positionList << tr("Top Left") << tr("Top Right")
               << tr("Bottom Left") << tr("Bottom Right");
  positionNotify_->addItems(positionList);

  fullscreenModeNotify_ = new QCheckBox(tr("Do not show notification in fullscreen mode"));
#if !defined(Q_OS_WIN)
  fullscreenModeNotify_->hide();
#endif
  showNotifyInactiveApp_ = new QCheckBox(tr("Show when the main window is inactive"));
  closeNotify_ = new QCheckBox(tr("Close notification after opening an article"));

  transparencyNotify_ = new QSpinBox();
  transparencyNotify_->setRange(0, 100);
  countShowNewsNotify_ = new QSpinBox();
  countShowNewsNotify_->setRange(1, 30);
  widthTitleNewsNotify_ = new QSpinBox();
  widthTitleNewsNotify_->setRange(50, 500);
  timeShowNewsNotify_ = new QSpinBox();
  timeShowNewsNotify_->setRange(0, 99999);

  QPushButton *showNotifer = new QPushButton(tr("Review"));
  connect(showNotifer, SIGNAL(clicked()), this, SLOT(showNotification()));

  QHBoxLayout *notifierLayout1 = new QHBoxLayout();
  notifierLayout1->addWidget(new QLabel(tr("Screen")));
  notifierLayout1->addWidget(screenNotify_);
  notifierLayout1->addSpacing(10);
  notifierLayout1->addWidget(new QLabel(tr("Position")));
  notifierLayout1->addWidget(positionNotify_);
  notifierLayout1->addStretch(1);
  notifierLayout1->addWidget(showNotifer);

  QGridLayout *notifierLayout2 = new QGridLayout();
  notifierLayout2->setColumnStretch(2, 1);
  notifierLayout2->addWidget(new QLabel(tr("Transparency")), 0, 0);
  notifierLayout2->addWidget(transparencyNotify_, 0, 1);
  notifierLayout2->addWidget(new QLabel("%"), 0, 2);
  notifierLayout2->addWidget(new QLabel(tr("Show at most")), 1, 0);
  notifierLayout2->addWidget(countShowNewsNotify_, 1, 1);
  notifierLayout2->addWidget(new QLabel(tr("items per notification page")), 1, 2);
  notifierLayout2->addWidget(new QLabel(tr("List item width")), 2, 0);
  notifierLayout2->addWidget(widthTitleNewsNotify_, 2, 1);
  notifierLayout2->addWidget(new QLabel(tr("pixels")), 2, 2);
  notifierLayout2->addWidget(new QLabel(tr("Close notification after")), 3, 0);
  notifierLayout2->addWidget(timeShowNewsNotify_, 3, 1);
  notifierLayout2->addWidget(new QLabel(tr("seconds")), 3, 2);

  showTitlesFeedsNotify_ = new QCheckBox(tr("Show feed titles"));
  showIconFeedNotify_ = new QCheckBox(tr("Show feed icons"));
  showButtonMarkAllNotify_ = new QCheckBox(tr("Show button 'Mark All News Read'"));
  showButtonMarkReadNotify_ = new QCheckBox(tr("Show button 'Mark Read/Unread'"));
  showButtonExBrowserNotify_ = new QCheckBox(tr("Show button 'Open in External Browser'"));
  showButtonDeleteNotify_ = new QCheckBox(tr("Show button 'Delete News'"));

  QVBoxLayout *notifierLayout3 = new QVBoxLayout();
  notifierLayout3->addWidget(showTitlesFeedsNotify_);
  notifierLayout3->addWidget(showIconFeedNotify_);
  notifierLayout3->addWidget(showButtonMarkAllNotify_);
  notifierLayout3->addWidget(showButtonMarkReadNotify_);
  notifierLayout3->addWidget(showButtonExBrowserNotify_);
  notifierLayout3->addWidget(showButtonDeleteNotify_);

  onlySelectedFeeds_ = new QCheckBox(tr("Only show selected feeds:"));
  QPushButton *feedsNotiferButton = new QPushButton(tr("Feeds"));
  feedsNotiferButton->setEnabled(false);

  QHBoxLayout *onlySelectedFeedsLayout = new QHBoxLayout();
  onlySelectedFeedsLayout->addWidget(onlySelectedFeeds_);
  onlySelectedFeedsLayout->addWidget(feedsNotiferButton);
  onlySelectedFeedsLayout->addStretch(1);

  createFeedsNotifierDlg();

  connect(onlySelectedFeeds_, SIGNAL(toggled(bool)),
          feedsNotiferButton, SLOT(setEnabled(bool)));
  connect(feedsNotiferButton, SIGNAL(clicked()),
          feedsNotifierDlg_, SLOT(exec()));

  QVBoxLayout *notificationLayoutV = new QVBoxLayout();
  notificationLayoutV->setContentsMargins(10, 10, 10, 10);
  notificationLayoutV->addLayout(notifierLayout1);
  notificationLayoutV->addWidget(fullscreenModeNotify_);
  notificationLayoutV->addWidget(showNotifyInactiveApp_);
  notificationLayoutV->addWidget(closeNotify_);
  notificationLayoutV->addSpacing(10);
  notificationLayoutV->addLayout(notifierLayout2);
  notificationLayoutV->addSpacing(10);
  notificationLayoutV->addLayout(notifierLayout3);
  notificationLayoutV->addSpacing(10);
  notificationLayoutV->addLayout(onlySelectedFeedsLayout);
  notificationLayoutV->addStretch(1);

  showNotifyOn_->setLayout(notificationLayoutV);

  QWidget *notificationWidget = new QWidget();
  QVBoxLayout *notificationLayout = new QVBoxLayout(notificationWidget);
  notificationLayout->addWidget(showNotifyOn_);

  soundNotifyBox_ = new QGroupBox(tr("Play a sound for incoming articles"));
  soundNotifyBox_->setCheckable(true);
  soundNotifyBox_->setChecked(false);

  editSoundNotifer_ = new LineEdit();
  selectionSoundNotifer_ = new QPushButton(tr("Browse..."));
  playSoundNotifer_ = new QPushButton(tr("Play"));

  connect(selectionSoundNotifer_, SIGNAL(clicked()),
          this, SLOT(selectionSoundNotifer()));
  connect(playSoundNotifer_, SIGNAL(clicked()), this, SLOT(slotPlaySoundNotifer()));
  connect(this, SIGNAL(signalPlaySound(QString)), parent(), SLOT(slotPlaySound(QString)));

  QHBoxLayout *soundNotifyLayoutH = new QHBoxLayout();
  soundNotifyLayoutH->addWidget(editSoundNotifer_, 1);
  soundNotifyLayoutH->addWidget(selectionSoundNotifer_);
  soundNotifyLayoutH->addWidget(playSoundNotifer_);

  QVBoxLayout *soundNotifyLayout = new QVBoxLayout(soundNotifyBox_);
  soundNotifyLayout->setContentsMargins(10, 10, 10, 10);
  soundNotifyLayout->addLayout(soundNotifyLayoutH);
  soundNotifyLayout->addStretch(1);

  QWidget *soundNotifyWidget = new QWidget();
  QVBoxLayout *boxCleanUpFeedsLayout = new QVBoxLayout(soundNotifyWidget);
  boxCleanUpFeedsLayout->addWidget(soundNotifyBox_);

  notifierWidget_ = new QTabWidget();
  notifierWidget_->addTab(notificationWidget, tr("Notification"));
  notifierWidget_->addTab(soundNotifyWidget, tr("Sound"));

  loadNotifierOk_ = false;
}

/** @brief Create dialog "Feeds" for Notifier
 *----------------------------------------------------------------------------*/
void OptionsDialog::createFeedsNotifierDlg()
{
  feedsNotifierDlg_ = new Dialog(this);
  feedsNotifierDlg_->setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
  feedsNotifierDlg_->setWindowTitle(tr("Selection of feeds"));
  feedsNotifierDlg_->setMinimumSize(300, 300);

  feedsTreeNotify_ = new QTreeWidget(this);
  feedsTreeNotify_->setObjectName("feedsTreeNotify_");
  feedsTreeNotify_->setColumnCount(2);
  feedsTreeNotify_->setColumnHidden(1, true);
  feedsTreeNotify_->header()->hide();

  itemNotChecked_ = false;

  QStringList treeItem;
  treeItem << "Feeds" << "Id";
  feedsTreeNotify_->setHeaderLabels(treeItem);

  treeItem.clear();
  treeItem << tr("All Feeds") << "0";
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setCheckState(0, Qt::Checked);
  feedsTreeNotify_->addTopLevelItem(treeWidgetItem);

  feedsNotifierDlg_->pageLayout->addWidget(feedsTreeNotify_);
  feedsNotifierDlg_->buttonBox->addButton(QDialogButtonBox::Close);

  connect(feedsTreeNotify_, SIGNAL(itemChanged(QTreeWidgetItem*,int)),
          this, SLOT(feedsTreeNotifyItemChanged(QTreeWidgetItem*,int)));
}

/** @brief Create widget "Passwords"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createPasswordsWidget()
{
  passTree_ = new QTreeWidget(this);
  passTree_->setObjectName("labelsTree_");
  passTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  passTree_->setColumnCount(4);
  passTree_->setColumnHidden(0, true);
  passTree_->setColumnHidden(3, true);
  passTree_->setColumnWidth(1, 250);
  passTree_->header()->setMinimumSectionSize(22);

  QStringList strTreeItem;
  strTreeItem.clear();
  strTreeItem << "Id" << tr("Site") << tr("User") << tr("Password");
  passTree_->setHeaderLabels(strTreeItem);

  QSqlQuery q;
  q.exec("SELECT id, server, username, password FROM passwords");
  while (q.next()) {
    QString id = q.value(0).toString();
    QString server = q.value(1).toString();
    QString user = q.value(2).toString();
    QString pass = QString::fromUtf8(QByteArray::fromBase64(q.value(3).toByteArray()));

    strTreeItem.clear();
    strTreeItem << id << server << user << pass;
    QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(strTreeItem);
    passTree_->addTopLevelItem(treeWidgetItem);
  }

  QPushButton *deletePass = new QPushButton(tr("Delete"));
  connect(deletePass, SIGNAL(clicked()), this, SLOT(slotDeletePass()));
  QPushButton *deleteAllPass = new QPushButton(tr("Delete All"));
  connect(deleteAllPass, SIGNAL(clicked()), this, SLOT(slotDeleteAllPass()));
  QPushButton *showPass = new QPushButton(tr("Show Passwords"));
  connect(showPass, SIGNAL(clicked()), this, SLOT(slotShowPass()));
  QVBoxLayout *passButtonLayout = new QVBoxLayout();
  passButtonLayout->addWidget(deletePass);
  passButtonLayout->addWidget(deleteAllPass);
  passButtonLayout->addSpacing(10);
  passButtonLayout->addWidget(showPass);
  passButtonLayout->addStretch(1);

  QHBoxLayout *passLayout = new QHBoxLayout();
  passLayout->setContentsMargins(0, 0, 0, 0);
  passLayout->addWidget(passTree_);
  passLayout->addLayout(passButtonLayout);

  passwordsWidget_ = new QWidget();
  passwordsWidget_->setLayout(passLayout);
}

/** @brief Create widget "Language"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createLanguageWidget()
{
  languageFileList_ = new QTreeWidget();
  languageFileList_->setObjectName("languageFileList_");
  languageFileList_->setColumnCount(4);
  languageFileList_->setColumnHidden(0, true);
  languageFileList_->setColumnWidth(1, 240);
  languageFileList_->setColumnWidth(2, 160);
  languageFileList_->setHeaderLabels({QStringLiteral("Id"), tr("Language"), tr("Author"), tr("Contact")});

  const LanguageCatalog catalog = mainApp->languageCatalog();
  for (const LanguageCatalog::Entry &entry : catalog.entries()) {
    QString label = entry.name + QStringLiteral(" [%1]").arg(entry.id);
    if (!entry.emoji.isEmpty()) label.prepend(entry.emoji + QLatin1Char(' '));
    auto *item = new QTreeWidgetItem(languageFileList_, {entry.id, label, entry.author, entry.contact});
    if (!entry.fileName.isEmpty()) item->setToolTip(1, entry.fileName);
  }
  setLanguage(mainApp->language());

  QString linkWikiStr =
      QString("<a href=\"%1\">%2</a>").arg(ProjectMetadata::translationsUrl().toHtmlEscaped(), tr("Translation website"));
  QLabel *linkTranslators = new QLabel(linkWikiStr);
  linkTranslators->setOpenExternalLinks(false);
  connect(linkTranslators, &QLabel::linkActivated, this, [](const QString &link) {
    const QUrl url(link);
    if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
  });

  QVBoxLayout *languageLayout = new QVBoxLayout();
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(new QLabel(tr("Choose language:")));
  languageLayout->addWidget(languageFileList_);
  languageLayout->addWidget(linkTranslators);

  languageWidget_ = new QWidget();
  languageWidget_->setLayout(languageLayout);
}

/** @brief Create widget "Fonts and Colors"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createFontsColorsWidget()
{
  //! tab "Fonts"

  fontsTree_ = new QTreeWidget();
  fontsTree_->setObjectName("fontTree");
  fontsTree_->setColumnCount(3);
  fontsTree_->setColumnHidden(0, true);
  fontsTree_->setColumnWidth(1, 260);

  QStringList treeItem;
  treeItem << "Id" << tr("Type") << tr("Font");
  fontsTree_->setHeaderLabels(treeItem);

  treeItem.clear();
  treeItem << "0" << tr("Feed list font");
  fontsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "1" << tr("Article list font");
  fontsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "2" << tr("Article title font");
  fontsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "3" << tr("Article text font");
  fontsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << "4" << tr("Notification font");
  fontsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));

  fontsTree_->setCurrentItem(fontsTree_->topLevelItem(0));
  connect(fontsTree_, SIGNAL(doubleClicked(QModelIndex)),
          this, SLOT(slotFontChange()));

  QPushButton *fontChange = new QPushButton(tr("Change..."));
  connect(fontChange, SIGNAL(clicked()), this, SLOT(slotFontChange()));
  QPushButton *fontReset = new QPushButton(tr("Reset"));
  connect(fontReset, SIGNAL(clicked()), this, SLOT(slotFontReset()));
  QVBoxLayout *fontsButtonLayout = new QVBoxLayout();
  fontsButtonLayout->addWidget(fontChange);
  fontsButtonLayout->addWidget(fontReset);
  fontsButtonLayout->addStretch(1);

  QHBoxLayout *mainFontsLayout = new QHBoxLayout();
  mainFontsLayout->addWidget(fontsTree_, 1);
  mainFontsLayout->addLayout(fontsButtonLayout);

  QWidget *fontsWidget = new QWidget();
  fontsWidget->setLayout(mainFontsLayout);

  //! tab "Colors"

  colorsTree_ = new QTreeWidget(this);
  colorsTree_->setObjectName("colorsTree_");
  colorsTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  colorsTree_->setRootIsDecorated(false);
  colorsTree_->setColumnCount(2);
  colorsTree_->setColumnHidden(1, true);
  colorsTree_->setHeaderHidden(true);

  treeItem.clear();
  treeItem << tr("Feed list text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Feeds list background");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Article list text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Article list background");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Focused article text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Focused article background");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Link color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Title color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Date color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Author color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Article text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Article title background");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Article background");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Feed with new articles");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Unread article count in feed tree");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("New article text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Unread article text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Focused feed color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Focused feed background color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Disabled feed");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Alternating row colors");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Notification text color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));
  treeItem.clear();
  treeItem << tr("Notification background color");
  colorsTree_->addTopLevelItem(new QTreeWidgetItem(treeItem));

  colorsTree_->setCurrentItem(colorsTree_->topLevelItem(0));

  connect(colorsTree_, SIGNAL(doubleClicked(QModelIndex)),
          this, SLOT(slotColorChange()));

  QPushButton *colorChange = new QPushButton(tr("Change..."));
  connect(colorChange, SIGNAL(clicked()), this, SLOT(slotColorChange()));
  QPushButton *colorReset = new QPushButton(tr("Reset"));
  connect(colorReset, SIGNAL(clicked()), this, SLOT(slotColorReset()));
  QVBoxLayout *colorsButtonLayout = new QVBoxLayout();
  colorsButtonLayout->addWidget(colorChange);
  colorsButtonLayout->addWidget(colorReset);
  colorsButtonLayout->addStretch(1);

  QHBoxLayout *colorsLayout = new QHBoxLayout();
  colorsLayout->addWidget(colorsTree_);
  colorsLayout->addLayout(colorsButtonLayout);

  QWidget *colorsWidget_ = new QWidget(this);
  colorsWidget_->setLayout(colorsLayout);

  fontsColorsWidget_ = new QTabWidget();
  fontsColorsWidget_->addTab(fontsWidget, tr("Fonts"));
  fontsColorsWidget_->addTab(colorsWidget_, tr("Colors"));
}

/** @brief Create widget "Shortcuts"
 *----------------------------------------------------------------------------*/
void OptionsDialog::createShortcutWidget()
{
  filterShortcut_ = new LineEdit(this, tr("Filter"));

  shortcutTree_ = new QTreeView(this);
  shortcutTree_->setObjectName("shortcutTree");
  shortcutTree_->setSortingEnabled(false);
  shortcutTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  shortcutTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  shortcutTree_->setRootIsDecorated(false);

  shortcutModel_ = new QStandardItemModel();
  shortcutModel_->setColumnCount(5);
  shortcutProxyModel_ = new QSortFilterProxyModel();
  shortcutProxyModel_->setSourceModel(shortcutModel_);
  shortcutProxyModel_->setFilterKeyColumn(-1);
  shortcutProxyModel_->setFilterRole(Qt::UserRole + 1);
  shortcutProxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  shortcutTree_->setModel(shortcutProxyModel_);

  shortcutTree_->hideColumn(3);
  shortcutTree_->hideColumn(4);
  shortcutTree_->header()->setStretchLastSection(true);
  shortcutTree_->setColumnWidth(0, 215);
  shortcutTree_->setColumnWidth(1, 165);
  shortcutTree_->setColumnWidth(2, 120);

  QStringList treeItem;
  treeItem << tr("Action") << tr("Description") << tr("Shortcut")
           << "ObjectName" << "Data";
  shortcutModel_->setHorizontalHeaderLabels(treeItem);

  editShortcut_ = new LineEdit();
  QPushButton *resetShortcutButton = new QPushButton(tr("Reset"));

  QHBoxLayout *editShortcutLayout = new QHBoxLayout();
  editShortcutLayout->addWidget(new QLabel(tr("Shortcut:")));
  editShortcutLayout->addWidget(editShortcut_, 1);
  editShortcutLayout->addWidget(resetShortcutButton);

  editShortcutBox = new QGroupBox();
  editShortcutBox->setEnabled(false);
  editShortcutBox->setLayout(editShortcutLayout);

  warningShortcut_ = new QLabel();

  QVBoxLayout *shortcutLayout = new QVBoxLayout();
  shortcutLayout->setContentsMargins(0, 0, 0, 0);
  shortcutLayout->addWidget(filterShortcut_);
  shortcutLayout->addWidget(shortcutTree_, 1);
  shortcutLayout->addWidget(warningShortcut_);
  shortcutLayout->addWidget(editShortcutBox);

  shortcutWidget_ = new QWidget();
  shortcutWidget_->setLayout(shortcutLayout);

  connect(shortcutTree_, SIGNAL(clicked(QModelIndex)),
          this, SLOT(shortcutTreeClicked(QModelIndex)));
  connect(this, SIGNAL(signalShortcutTreeUpDownPressed()),
          SLOT(slotShortcutTreeUpDownPressed()), Qt::QueuedConnection);
  connect(editShortcut_, SIGNAL(signalClear()),
          this, SLOT(slotClearShortcut()));
  connect(resetShortcutButton, SIGNAL(clicked()),
          this, SLOT(slotResetShortcut()));
  connect(filterShortcut_, SIGNAL(textChanged(QString)),
          this, SLOT(filterShortcutChanged(QString)));

  editShortcut_->installEventFilter(this);
}
//----------------------------------------------------------------------------
void OptionsDialog::slotCategoriesTreeKeyUpDownPressed()
{
  slotCategoriesItemClicked(categoriesTree_->currentItem(), 1);
}
//----------------------------------------------------------------------------
void OptionsDialog::slotCategoriesItemClicked(QTreeWidgetItem* item, int)
{
  contentLabel_->setText(item->data(1, Qt::DisplayRole).toString());
  contentStack_->setCurrentIndex(item->data(0, Qt::DisplayRole).toInt());

  if (item->data(1, Qt::DisplayRole).toString() == tr("Labels")) {
    loadLabels();
  } else if (item->data(1, Qt::DisplayRole).toString() == tr("Notifications")) {
    loadNotifier();
  }
}
//----------------------------------------------------------------------------
int OptionsDialog::currentIndex()
{
  return categoriesTree_->currentItem()->text(0).toInt();
}
//----------------------------------------------------------------------------
void OptionsDialog::setCurrentItem(int index)
{
  categoriesTree_->setCurrentItem(categoriesTree_->topLevelItem(index), 1);
  slotCategoriesTreeKeyUpDownPressed();
}
//----------------------------------------------------------------------------
void OptionsDialog::manualProxyToggle(bool checked)
{
  manualWidget_->setEnabled(checked);
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
QString OptionsDialog::language()
{
  const QTreeWidgetItem *item = languageFileList_->currentItem();
  return item ? item->text(0) : QStringLiteral("en");
}
//----------------------------------------------------------------------------
void OptionsDialog::setLanguage(const QString &langFileName)
{
  // Set focus on selected language-file
  QList<QTreeWidgetItem*> list =
      languageFileList_->findItems(langFileName, Qt::MatchFixedString, 0);
  if (list.count()) {
    languageFileList_->setCurrentItem(list.at(0));
  } else {
    // can't find file, choose english
    QList<QTreeWidgetItem*> list =
        languageFileList_->findItems("en", Qt::MatchFixedString, 0);
    languageFileList_->setCurrentItem(list.at(0));
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::slotFontChange()
{
  bool bOk;
  QFont curFont;
  curFont.setFamily(fontsTree_->currentItem()->text(2).section(", ", 0, 0));
  curFont.setPointSize(fontsTree_->currentItem()->text(2).section(", ", 1).toInt());

  QFont font = QFontDialog::getFont(&bOk, curFont);
  if (bOk) {
    QString strFont = QString("%1, %2").
        arg(font.family()).
        arg(font.pointSize());
    fontsTree_->currentItem()->setText(2, strFont);
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::slotFontReset()
{
  switch (fontsTree_->currentItem()->text(0).toInt()) {
  case 2: case 3: fontsTree_->currentItem()->setText(
          2, QString("%1, %2").arg(qApp->font().family()).arg(qApp->font().pointSize()+2));
    break;
  default: fontsTree_->currentItem()->setText(
          2, QString("%1, %2").arg(qApp->font().family()).arg(qApp->font().pointSize()));
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::slotColorChange()
{
  QString colorStr = colorsTree_->currentItem()->text(1);
  QColorDialog *colorDialog = new QColorDialog(QColor(colorStr), this);
  if (colorDialog->exec() == QDialog::Rejected) {
    delete colorDialog;
    return;
  }
  QColor color = colorDialog->selectedColor();
  delete colorDialog;

  QPixmap pixmapColor(14, 14);
  pixmapColor.fill(color.name());
  colorsTree_->currentItem()->setIcon(0, pixmapColor);
  colorsTree_->currentItem()->setText(1, color.name());
}
//----------------------------------------------------------------------------
void OptionsDialog::slotColorReset()
{
  const int row = colorsTree_->currentIndex().row();
  const QString colorName = mainApp->mainWindow()->defaultColorForOption(row);
  QPixmap pixmapColor(14, 14);
  if (colorName.isEmpty())
    pixmapColor.fill(QColor(0, 0, 0, 0));
  else
    pixmapColor.fill(colorName);
  colorsTree_->currentItem()->setIcon(0, pixmapColor);
  colorsTree_->currentItem()->setText(1, colorName);
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
  QListIterator<QAction *> iter(actions);
  while (iter.hasNext()) {
    QAction *pAction = iter.next();

    QStringList treeItem;
    treeItem << pAction->text().remove("&")
             << pAction->toolTip() << pAction->shortcut().toString()
             << pAction->objectName() << pAction->data().toString();

    QList<QStandardItem *> treeItems;
    for (int i = 0; i < treeItem.count(); i++) {
      QStandardItem *item = new QStandardItem(treeItem.at(i));
      if (i == 0) {
        if (pAction->icon().isNull())
          item->setIcon(QIcon(":/images/images/noicon.png"));
        else {
          if (pAction->objectName() == "autoLoadImagesToggle") {
            item->setIcon(QIcon(":/images/imagesOn"));
            item->setText(tr("Load images"));
          } else item->setIcon(pAction->icon());
        }
      } else if (i == 1) {
        if (pAction->objectName() == "autoLoadImagesToggle") {
          item->setText(tr("Automatically load images in article view"));
        }
      }
      if (i >= 0 && i <= 2) {
        item->setData(treeItem.at(i));
      }
      treeItems.append(item);
    }
    shortcutModel_->appendRow(treeItems);

    QString str = pAction->shortcut().toString();
    treeItems = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
    if ((treeItems.count() > 1) && !str.isEmpty()) {
      for (int i = 0; i < treeItems.count(); i++) {
        treeItems.at(i)->setData(QColor(Qt::red), Qt::ForegroundRole);
      }
    }
  }

  listDefaultShortcut_ = list;
}
//----------------------------------------------------------------------------
void OptionsDialog::saveActionShortcut(QList<QAction *> actions, QActionGroup *labelGroup)
{
  for (int i = 0; i < shortcutModel_->rowCount(); i++) {
    QString objectName = shortcutModel_->item(i, 3)->text();
    if (objectName.contains("labelAction_")) {
      QAction *action = new QAction(parent());
      action->setIcon(shortcutModel_->item(i, 0)->icon());
      action->setText(shortcutModel_->item(i, 0)->text());
      action->setShortcut(QKeySequence(shortcutModel_->item(i, 2)->text()));
      action->setObjectName(shortcutModel_->item(i, 3)->text());
      action->setCheckable(true);
      action->setData(shortcutModel_->item(i, 4)->text());
      labelGroup->addAction(action);
      actions.append(action);
    } else {
      actions.at(i)->setShortcut(
            QKeySequence(shortcutModel_->item(i, 2)->text()));
    }
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::slotShortcutTreeUpDownPressed()
{
  shortcutTreeClicked(shortcutTree_->currentIndex());
}
//----------------------------------------------------------------------------
void OptionsDialog::shortcutTreeClicked(const QModelIndex &index)
{
  QModelIndex indexCur = shortcutProxyModel_->mapToSource(index);
  editShortcut_->setText(shortcutModel_->item(indexCur.row(), 2)->text());
  editShortcutBox->setEnabled(true);
  editShortcut_->setFocus();
  warningShortcut_->clear();
}
//----------------------------------------------------------------------------
void OptionsDialog::slotClearShortcut()
{
  QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
  int row = index.row();
  QString str = shortcutModel_->item(row, 2)->text();
  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
  if ((treeItems.count() > 1) && !str.isEmpty()) {
    for (int i = 0; i < treeItems.count(); i++) {
      if ((treeItems.count() == 2) || (treeItems.at(i)->row() == row)) {
        treeItems.at(i)->setData(shortcutModel_->item(0, 1)->data(Qt::ForegroundRole),
                                 Qt::ForegroundRole);
      }
    }
  }

  editShortcut_->clear();
  shortcutModel_->item(row, 2)->setText("");
  shortcutModel_->item(row, 2)->setData("");
  warningShortcut_->clear();
}
//----------------------------------------------------------------------------
void OptionsDialog::slotResetShortcut()
{
  QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
  int row = index.row();
  QString objectName = shortcutModel_->item(row, 3)->text();
  if (objectName.contains("labelAction_"))
    return;

  QString str = shortcutModel_->item(row, 2)->text();
  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
  if (!str.isEmpty()) {
    for (int i = 0; i < treeItems.count(); i++) {
      if ((treeItems.count() == 2) || (treeItems.at(i)->row() == row)) {
        treeItems.at(i)->setData(shortcutModel_->item(0, 1)->data(Qt::ForegroundRole),
                                 Qt::ForegroundRole);
      }
    }
  }

  str = listDefaultShortcut_->at(row);
  editShortcut_->setText(str);
  shortcutModel_->item(row, 2)->setText(str);
  shortcutModel_->item(row, 2)->setData(str);
  warningShortcut_->clear();

  if (!str.isEmpty()) {
    treeItems = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
    for (int i = 0; i < treeItems.count(); i++) {
      if (treeItems.at(i)->row() != row) {
        warningShortcut_->setText(tr("Warning: key is already assigned to ") +
                                  " '" +
                                  shortcutModel_->item(treeItems.at(i)->row(), 0)->text() +
                                  "'");
      }
      if (treeItems.count() > 1) {
        treeItems.at(i)->setData(QColor(Qt::red), Qt::ForegroundRole);
      }
    }
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::filterShortcutChanged(const QString & text)
{
  shortcutProxyModel_->setFilterFixedString(text);

  if (shortcutTree_->currentIndex().isValid()) {
    QModelIndex indexCur = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
    editShortcut_->setText(shortcutModel_->item(indexCur.row(), 2)->text());
    editShortcutBox->setEnabled(true);
  } else {
    editShortcut_->setText("");
    editShortcutBox->setEnabled(false);
  }
  warningShortcut_->clear();
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
void OptionsDialog::selectionSoundNotifer()
{
  QString path;

  QFileInfo file(editSoundNotifer_->text());
  if (file.isFile()) path = editSoundNotifer_->text();
  else path = file.path();

  QString fileName = QFileDialog::getOpenFileName(this,
                                                  tr("Open File..."),
                                                  path,
                                                  tr("Audio Files (*.wav *.flac *.mp3);;All Files (*)")
                                                 );
  if (!fileName.isEmpty())
    editSoundNotifer_->setText(fileName);
}

void OptionsDialog::slotPlaySoundNotifer()
{
  if (!editSoundNotifer_->text().isEmpty())
    emit signalPlaySound(editSoundNotifer_->text());
}

//----------------------------------------------------------------------------
void OptionsDialog::feedsTreeNotifyItemChanged(QTreeWidgetItem *item, int column)
{
  if ((column != 0) || itemNotChecked_) return;

  itemNotChecked_ = true;
  if (item->checkState(0) == Qt::Unchecked) {
    setCheckStateItem(item, Qt::Unchecked);

    QTreeWidgetItem *parentItem = item->parent();
    while (parentItem) {
      parentItem->setCheckState(0, Qt::Unchecked);
      parentItem = parentItem->parent();
    }
  } else {
    setCheckStateItem(item, Qt::Checked);
  }
  itemNotChecked_ = false;
}
//----------------------------------------------------------------------------
void OptionsDialog::setCheckStateItem(QTreeWidgetItem *item, Qt::CheckState state)
{
  for (int i = 0; i < item->childCount(); ++i) {
    QTreeWidgetItem *childItem = item->child(i);
    childItem->setCheckState(0, state);
    setCheckStateItem(childItem, state);
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::loadLabels()
{
  if (loadLabelsOk_) return;
  loadLabelsOk_ = true;

  idLabels_.clear();

  QSqlQuery q;
  q.exec("SELECT id, name, image, color_text, color_bg, num FROM labels ORDER BY num");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    if ((idLabel <= 6) && (MainWindow::nameLabels().at(idLabel-1) == nameLabel)) {
      nameLabel = MainWindow::trNameLabels().at(idLabel-1);
    }
    QByteArray byteArray = q.value(2).toByteArray();
    QString colorText = q.value(3).toString();
    QString colorBg = q.value(4).toString();
    int numLabel = q.value(5).toInt();
    QPixmap imageLabel;
    if (!byteArray.isNull())
      imageLabel.loadFromData(byteArray);
    QStringList strTreeItem;
    strTreeItem << QString::number(idLabel) << nameLabel
                << colorText << colorBg << QString::number(numLabel);
    QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(strTreeItem);
    treeWidgetItem->setIcon(1, QIcon(imageLabel));
    if (!colorText.isEmpty())
      treeWidgetItem->setForeground(1, QColor(colorText));
    if (!colorBg.isEmpty())
      treeWidgetItem->setBackground(1, QColor(colorBg));
    labelsTree_->addTopLevelItem(treeWidgetItem);
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::newLabel()
{
  LabelDialog *labelDialog = new LabelDialog(this);

  if (labelDialog->exec() == QDialog::Rejected) {
    delete labelDialog;
    return;
  }

  QString nameLabel = labelDialog->nameEdit_->text();
  QString colorText = labelDialog->colorTextStr_;
  QString colorBg = labelDialog->colorBgStr_;

  int idLabel = 0;
  for (int i = 0; i < labelsTree_->topLevelItemCount(); i++) {
    QString str = labelsTree_->topLevelItem(i)->text(0);
    if (idLabel < str.toInt())
      idLabel = str.toInt();
  }
  idLabel = idLabel + 1;

  QStringList itemStr;
  itemStr << QString::number(idLabel) << nameLabel
              << colorText << colorBg
              << QString::number(idLabel);
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(itemStr);
  treeWidgetItem->setIcon(1, labelDialog->icon_);
  if (!colorText.isEmpty())
    treeWidgetItem->setForeground(1, QColor(colorText));
  if (!colorBg.isEmpty())
    treeWidgetItem->setBackground(1, QColor(colorBg));
  labelsTree_->addTopLevelItem(treeWidgetItem);
  addIdLabelList(treeWidgetItem->text(0));

  itemStr.clear();
  itemStr << nameLabel << nameLabel << ""
          << QString("labelAction_%1").arg(idLabel) << QString::number(idLabel);
  QList<QStandardItem *> treeItems;
  for (int i = 0; i < itemStr.count(); i++) {
    QStandardItem *item = new QStandardItem(itemStr.at(i));
    if (i == 0) {
      item->setIcon(labelDialog->icon_);
    }
    if (i >= 0 && i <= 2) {
      item->setData(itemStr.at(i));
    }
    treeItems.append(item);
  }
  shortcutModel_->appendRow(treeItems);

  delete labelDialog;
}
//----------------------------------------------------------------------------
void OptionsDialog::editLabel()
{
  QTreeWidgetItem *treeWidgetItem = labelsTree_->currentItem();

  QString idLabelStr = treeWidgetItem->text(0);
  QString nameLabel = treeWidgetItem->text(1);
  QString colorText = treeWidgetItem->text(2);
  QString colorBg = treeWidgetItem->text(3);

  LabelDialog *labelDialog = new LabelDialog(this);

  labelDialog->nameEdit_->setText(nameLabel);
  labelDialog->icon_ = treeWidgetItem->icon(1);
  labelDialog->colorTextStr_ = colorText;
  labelDialog->colorBgStr_ = colorBg;
  labelDialog->loadData();

  if (labelDialog->exec() == QDialog::Rejected) {
    delete labelDialog;
    return;
  }

  nameLabel = labelDialog->nameEdit_->text();
  colorText = labelDialog->colorTextStr_;
  colorBg = labelDialog->colorBgStr_;

  treeWidgetItem->setText(1, nameLabel);
  treeWidgetItem->setText(2, colorText);
  treeWidgetItem->setText(3, colorBg);
  treeWidgetItem->setIcon(1, labelDialog->icon_);
  if (!colorText.isEmpty())
    treeWidgetItem->setForeground(1, QColor(colorText));
  if (!colorBg.isEmpty())
    treeWidgetItem->setBackground(1, QColor(colorBg));
  addIdLabelList(idLabelStr);

  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(idLabelStr, Qt::MatchFixedString, 4);
  QStandardItem *item = treeItems.first();
  shortcutModel_->item(item->row(), 0)->setIcon(labelDialog->icon_);
  shortcutModel_->item(item->row(), 0)->setText(nameLabel);
  shortcutModel_->item(item->row(), 0)->setData(nameLabel);

  delete labelDialog;
}
//----------------------------------------------------------------------------
void OptionsDialog::deleteLabel()
{
  int labelRow = labelsTree_->currentIndex().row();
  addIdLabelList(labelsTree_->topLevelItem(labelRow)->text(0));

  QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(idLabelStr, Qt::MatchFixedString, 4);
  QStandardItem *item = treeItems.first();
  shortcutModel_->removeRow(item->row());

  QTreeWidgetItem *treeItem = labelsTree_->takeTopLevelItem(labelRow);
  delete treeItem;
}
//----------------------------------------------------------------------------
void OptionsDialog::moveUpLabel()
{
  int labelRow = labelsTree_->currentIndex().row();

  addIdLabelList(labelsTree_->topLevelItem(labelRow)->text(0));
  addIdLabelList(labelsTree_->topLevelItem(labelRow-1)->text(0));

  QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(idLabelStr, Qt::MatchFixedString, 4);
  int row = treeItems.first()->row();
  treeItems = shortcutModel_->takeRow(row-1);
  shortcutModel_->insertRow(row, treeItems);

  int num1 = labelsTree_->topLevelItem(labelRow)->text(4).toInt();
  int num2 = labelsTree_->topLevelItem(labelRow-1)->text(4).toInt();
  labelsTree_->topLevelItem(labelRow-1)->setText(4, QString::number(num1));
  labelsTree_->topLevelItem(labelRow)->setText(4, QString::number(num2));

  QTreeWidgetItem *treeItem = labelsTree_->takeTopLevelItem(labelRow-1);
  labelsTree_->insertTopLevelItem(labelRow, treeItem);

  if (labelsTree_->currentIndex().row() == 0)
    moveUpLabelButton_->setEnabled(false);
  if (labelsTree_->currentIndex().row() != (labelsTree_->topLevelItemCount()-1))
    moveDownLabelButton_->setEnabled(true);
}
//----------------------------------------------------------------------------
void OptionsDialog::moveDownLabel()
{
  int labelRow = labelsTree_->currentIndex().row();

  addIdLabelList(labelsTree_->topLevelItem(labelRow)->text(0));
  addIdLabelList(labelsTree_->topLevelItem(labelRow+1)->text(0));

  QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  QList<QStandardItem *> treeItems;
  treeItems = shortcutModel_->findItems(idLabelStr, Qt::MatchFixedString, 4);
  int row = treeItems.first()->row();
  treeItems = shortcutModel_->takeRow(row+1);
  shortcutModel_->insertRow(row, treeItems);

  int num1 = labelsTree_->topLevelItem(labelRow)->text(4).toInt();
  int num2 = labelsTree_->topLevelItem(labelRow+1)->text(4).toInt();
  labelsTree_->topLevelItem(labelRow+1)->setText(4, QString::number(num1));
  labelsTree_->topLevelItem(labelRow)->setText(4, QString::number(num2));

  QTreeWidgetItem *treeItem = labelsTree_->takeTopLevelItem(labelRow+1);
  labelsTree_->insertTopLevelItem(labelRow, treeItem);

  if (labelsTree_->currentIndex().row() == (labelsTree_->topLevelItemCount()-1))
    moveDownLabelButton_->setEnabled(false);
  if (labelsTree_->currentIndex().row() != 0)
    moveUpLabelButton_->setEnabled(true);
}
//----------------------------------------------------------------------------
void OptionsDialog::slotCurrentLabelChanged(QTreeWidgetItem *current,
                                           QTreeWidgetItem *)
{
  if (labelsTree_->indexOfTopLevelItem(current) == 0)
    moveUpLabelButton_->setEnabled(false);
  else moveUpLabelButton_->setEnabled(true);

  if (labelsTree_->indexOfTopLevelItem(current) == (labelsTree_->topLevelItemCount()-1))
    moveDownLabelButton_->setEnabled(false);
  else moveDownLabelButton_->setEnabled(true);

  if (labelsTree_->indexOfTopLevelItem(current) < 0) {
    editLabelButton_->setEnabled(false);
    deleteLabelButton_->setEnabled(false);
    moveUpLabelButton_->setEnabled(false);
    moveDownLabelButton_->setEnabled(false);
  } else {
    editLabelButton_->setEnabled(true);
    deleteLabelButton_->setEnabled(true);
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::applyLabels()
{
  db_.transaction();
  QSqlQuery q;

  foreach (QString idLabel, idLabels_) {
    QList<QTreeWidgetItem *> treeItems =
        labelsTree_->findItems(idLabel, Qt::MatchFixedString, 0);
    if (treeItems.count() == 0) {
      q.exec(QString("DELETE FROM labels WHERE id=='%1'").arg(idLabel));
      q.exec(QString("SELECT id, label FROM news WHERE label LIKE '%,%1,%'").arg(idLabel));
      while (q.next()) {
        QString strIdLabels = q.value(1).toString();
        strIdLabels.replace(QString(",%1,").arg(idLabel), ",");
        QSqlQuery q1;
        q1.exec(QString("UPDATE news SET label='%1' WHERE id=='%2'").
               arg(strIdLabels).arg(q.value(0).toInt()));
      }
    } else {
      QString nameLabel = treeItems.at(0)->text(1);
      if ((idLabel.toInt() <= 6) && (MainWindow::trNameLabels().at(idLabel.toInt()-1) == nameLabel)) {
        nameLabel = MainWindow::nameLabels().at(idLabel.toInt()-1);
      }
      QPixmap icon = treeItems.at(0)->icon(1).pixmap(16, 16);
      QByteArray iconData;
      QBuffer    buffer(&iconData);
      buffer.open(QIODevice::WriteOnly);
      icon.save(&buffer, "PNG");
      buffer.close();
      QString colorText = treeItems.at(0)->text(2);
      QString colorBg = treeItems.at(0)->text(3);
      int numLabel = treeItems.at(0)->text(4).toInt();

      q.exec(QString("SELECT * FROM labels WHERE id=='%1'").arg(idLabel));
      if (q.next()) {
        q.prepare("UPDATE labels SET name=?, image=?, color_text=?, color_bg=?, num=? "
                  "WHERE id=?");
        q.addBindValue(nameLabel);
        q.addBindValue(iconData);
        q.addBindValue(colorText);
        q.addBindValue(colorBg);
        q.addBindValue(numLabel);
        q.addBindValue(idLabel);
        q.exec();
      } else {
        q.prepare("INSERT INTO labels(name, image, color_text, color_bg, num) "
                  "VALUES (:name, :image, :color_text, :color_bg, :num)");
        q.bindValue(":name", nameLabel);
        q.bindValue(":image", iconData);
        q.bindValue(":color_text", colorText);
        q.bindValue(":color_bg", colorBg);
        q.bindValue(":num", numLabel);
        q.exec();
      }
    }
  }

  db_.commit();
}

/** @brief Add id for editing label
 * @param idLabel id for label
 *----------------------------------------------------------------------------*/
void OptionsDialog::addIdLabelList(const QString &idLabel)
{
  if (!idLabels_.contains(idLabel)) idLabels_.append(idLabel);
}
//----------------------------------------------------------------------------
void OptionsDialog::loadNotifier()
{
  if (loadNotifierOk_) return;
  loadNotifierOk_ = true;

  itemNotChecked_ = true;

  QSqlQuery q;
  QQueue<int> parentIds;
  parentIds.enqueue(0);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    QString qStr = QString("SELECT text, id, image, xmlUrl FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
        arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      QString feedText = q.value(0).toString();
      QString feedId = q.value(1).toString();
      QByteArray byteArray = q.value(2).toByteArray();
      QString xmlUrl = q.value(3).toString();

      QStringList treeItem;
      treeItem << feedText << feedId;
      QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);

      treeWidgetItem->setCheckState(0, Qt::Unchecked);
      QSqlQuery q1;
      qStr = QString("SELECT value FROM feeds_ex WHERE feedId='%1' AND name='showNotification'").
          arg(feedId);
      q1.exec(qStr);
      if (q1.next()) {
        if (q1.value(0).toInt() == 1)
          treeWidgetItem->setCheckState(0, Qt::Checked);
      } else {
        qStr = QString("INSERT INTO feeds_ex(feedId, name, value) VALUES ('%1', 'showNotification', '0')").
            arg(feedId);
        q1.exec(qStr);
      }
      if (treeWidgetItem->checkState(0) == Qt::Unchecked)
        feedsTreeNotify_->topLevelItem(0)->setCheckState(0, Qt::Unchecked);

      QPixmap iconItem;
      if (xmlUrl.isEmpty()) {
        iconItem.load(":/images/folder");
      } else {
        if (byteArray.isNull() || mainApp->mainWindow()->defaultIconFeeds_) {
          iconItem.load(":/images/feed");
        } else {
          iconItem.loadFromData(QByteArray::fromBase64(byteArray));
        }
      }
      treeWidgetItem->setIcon(0, iconItem);

      QList<QTreeWidgetItem *> treeItems =
            feedsTreeNotify_->findItems(QString::number(parentId),
                                                       Qt::MatchFixedString | Qt::MatchRecursive,
                                                       1);
      treeItems.at(0)->addChild(treeWidgetItem);
      if (xmlUrl.isEmpty())
        parentIds.enqueue(feedId.toInt());
    }
  }
  feedsTreeNotify_->expandAll();
  itemNotChecked_ = false;
}
//----------------------------------------------------------------------------
void OptionsDialog::applyNotifier()
{
  mainApp->mainWindow()->idFeedsNotifyList_.clear();

  feedsTreeNotify_->expandAll();
  QTreeWidgetItem *treeWidgetItem =
      feedsTreeNotify_->itemBelow(feedsTreeNotify_->topLevelItem(0));
  db_.transaction();
  while (treeWidgetItem) {
    int check = 0;
    if (treeWidgetItem->checkState(0) == Qt::Checked)
      check = 1;
    QSqlQuery q;
    QString qStr = QString("UPDATE feeds_ex SET value='%1' WHERE feedId='%2' AND name='showNotification'").
        arg(check).arg(treeWidgetItem->text(1).toInt());
    q.exec(qStr);

    if (check && onlySelectedFeeds_->isChecked())
      mainApp->mainWindow()->idFeedsNotifyList_.append(treeWidgetItem->text(1).toInt());

    treeWidgetItem = feedsTreeNotify_->itemBelow(treeWidgetItem);
  }
  db_.commit();
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

  connect(notificationWidget_, SIGNAL(signalClose()),
          this, SLOT(deleteNotification()));

  notificationWidget_->show();
}

/** @brief Destroy notifier test window
 *----------------------------------------------------------------------------*/
void OptionsDialog::deleteNotification()
{
  notificationWidget_->deleteLater();
  notificationWidget_ = NULL;
}
//----------------------------------------------------------------------------
void OptionsDialog::slotDeletePass()
{
  if (passTree_->topLevelItemCount())
    passTree_->currentItem()->setHidden(true);
}
//----------------------------------------------------------------------------
void OptionsDialog::slotDeleteAllPass()
{
  for (int i = 0; i < passTree_->topLevelItemCount(); i++) {
    passTree_->topLevelItem(i)->setHidden(true);
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::slotShowPass()
{
  if (passTree_->isColumnHidden(3)) {
    passTree_->showColumn(3);
    passTree_->setColumnWidth(1, passTree_->columnWidth(1) - passTree_->columnWidth(3));
  }
}
//----------------------------------------------------------------------------
void OptionsDialog::applyPass()
{
  db_.transaction();
  QSqlQuery q;
  for (int i = 0; i < passTree_->topLevelItemCount(); i++) {
    if (passTree_->topLevelItem(i)->isHidden()) {
      QString id = passTree_->topLevelItem(i)->text(0);
      q.exec(QString("DELETE FROM passwords WHERE id=='%1'").arg(id));
    }
  }
  db_.commit();
}
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

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

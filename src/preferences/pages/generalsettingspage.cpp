// SPDX-License-Identifier: GPL-3.0-or-later
#include "generalsettingspage.h"

#include "app/mainapplication.h"
#include "common/logfile.h"
#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

GeneralSettingsPage::GeneralSettingsPage(QWidget *parent,
                                         QVector<OptionSearchEntry> *searchEntries,
                                         int pageIndex)
  : QFrame(parent)
{
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *layout = new QVBoxLayout(this);
  OptionsPageBuilder b(this, layout, pageIndex, -1, searchEntries);

  b.check(showSplashScreen_, OptionsDialog::tr("Show splash screen on startup"), QString(),
          QStringLiteral("launch logo boot"));
  b.check(reopenFeedStartup_, OptionsDialog::tr("Reopen last opened feeds on startup"), QString(),
          QStringLiteral("restore session tabs"));
  b.check(openNewTabNextToActive_, OptionsDialog::tr("Open new tab next to active"), QString(),
          QStringLiteral("tab position order"));
  b.check(hideFeedsOpenTab_, OptionsDialog::tr("Hide feeds tree when opening tabs"), QString(),
          QStringLiteral("sidebar panel collapse"));
  b.check(showToggleFeedsTree_, OptionsDialog::tr("Show feeds tree toggle"), QString(),
          QStringLiteral("sidebar panel button"));
  b.check(defaultIconFeeds_, OptionsDialog::tr("Show default RSS icon instead of each feed's icon"), QString(),
          QStringLiteral("favicon"));
  b.check(autocollapseFolder_, OptionsDialog::tr("Automatically collapse folders"), QString(),
          QStringLiteral("tree expand"));
  b.check(showCloseButtonTab_, OptionsDialog::tr("Show a close button on each tab"), QString(),
          QStringLiteral("tabs close"));
#ifdef NQRSS_ENABLE_DONATE_BUTTON
  b.check(hideDonateButton_, OptionsDialog::tr("Hide the donate button"), QString(),
          QStringLiteral("support"));
#endif
  b.spacing(20);

#if defined(Q_OS_WIN)
  b.check(autoRunEnabled_, OptionsDialog::tr("Run %1 at Windows startup").arg(QGuiApplication::applicationDisplayName()),
          QString(), QStringLiteral("autostart boot login"));
  autoRunSettings_ = new QSettings(
      QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"),
      QSettings::NativeFormat);
  const QString autoRunKey = mainApp->isPortableAppsCom()
      ? (QCoreApplication::applicationName() + QStringLiteral("Portable"))
      : QCoreApplication::applicationName();
  autoRunEnabled_->setChecked(autoRunSettings_->value(autoRunKey, false).toBool());
#endif

  b.check(updateCheckEnabled_, OptionsDialog::tr("Automatically check for updates"), QString(),
          QStringLiteral("version upgrade release"));
  b.check(storeDBMemory_, OptionsDialog::tr("Store the database in memory (requires application restart)"), QString(),
          QStringLiteral("ram sqlite performance cache"));
  QWidget *saveDBMemFileWidget = b.indented(QMargins(15, 0, 0, 0), [this](OptionsPageBuilder &sub) {
    sub.spinRow(saveDBMemFileInterval_,
                OptionsDialog::tr("Save the in-memory database to disk every"), 1, 999, OptionsDialog::tr("minutes"),
                QString(), QStringLiteral("ram sqlite flush interval"));
  });
  OptionsPageBuilder::bindEnabled(storeDBMemory_, {saveDBMemFileWidget});

  b.group(OptionsDialog::tr("Debug"), [this](OptionsPageBuilder &grp) {
    grp.row([this](OptionsRowBuilder &r) {
      QCheckBox *fileLogging = r.check(OptionsDialog::tr("Enable file logging"),
          OptionsDialog::tr("Takes effect immediately. Existing logs are kept when disabled."),
          QStringLiteral("debug log diagnostics"));
      fileLogging->setChecked(LogFile::fileLoggingEnabled());
      connect(fileLogging, &QCheckBox::toggled, this, [](bool enabled) {
        Settings().setValue("Settings/logFileOutput", enabled);
        LogFile::setFileLoggingEnabled(enabled);
      });
      QPushButton *showLog = r.button(OptionsDialog::tr("Show log location"));
      connect(showLog, &QPushButton::clicked, this, []() { LogFile::showLocation(); });
    });
  }, Qt::Horizontal);
  b.stretch(1);
}

void GeneralSettingsPage::save()
{
#if defined(Q_OS_WIN)
  if (mainApp->isPortableAppsCom()) {
    const QString key = QCoreApplication::applicationName() + QStringLiteral("Portable");
    if (autoRunEnabled_->isChecked()) {
      const QFileInfo file(QCoreApplication::applicationDirPath() + QStringLiteral("/../../")
                           + QCoreApplication::applicationName() + QStringLiteral("Portable.exe"));
      autoRunSettings_->setValue(key, QDir::toNativeSeparators(file.absoluteFilePath()));
    } else {
      autoRunSettings_->remove(key);
    }
  } else {
    if (autoRunEnabled_->isChecked()) {
      autoRunSettings_->setValue(QCoreApplication::applicationName(),
                                 QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
      autoRunSettings_->remove(QCoreApplication::applicationName());
    }
  }
#endif
}

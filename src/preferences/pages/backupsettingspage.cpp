// SPDX-License-Identifier: GPL-3.0-or-later
#include "backupsettingspage.h"
#include "backup/databasebackup.h"
#include "platform/filemanager.h"
#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QVBoxLayout>

BackupSettingsPage::BackupSettingsPage(QWidget *parent,
                                       QVector<OptionSearchEntry> *searchEntries,
                                       int pageIndex)
  : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  OptionsPageBuilder b(this, layout, pageIndex, -1, searchEntries);

  QLabel *location = b.label(tr("Backup directory: %1").arg(DatabaseBackup::directory()));
  location->setWordWrap(true);
  location->setTextInteractionFlags(Qt::TextSelectableByMouse);

  b.row([this](OptionsRowBuilder &r) {
    QPushButton *now = r.button(tr("Back Up Now"));
    QPushButton *show = r.button(tr("Show Backups"));
    r.registerSearch(now, now->text(), QString(), QStringLiteral("export archive snapshot"));
    r.registerSearch(show, show->text(), QString(), QStringLiteral("folder directory"));
    connect(show, &QPushButton::clicked, this, [this] {
      if (!QDir().mkpath(DatabaseBackup::directory())) {
        QMessageBox::warning(this, tr("Database Backup"),
                             tr("Cannot create the backup directory: %1").arg(DatabaseBackup::directory()));
        return;
      }
      FileManager::openDirectory(DatabaseBackup::directory());
    });
    connect(now, &QPushButton::clicked, this, [this] {
      DatabaseBackup::report(DatabaseBackup::create(QSqlDatabase::database(),
          DatabaseBackup::Trigger::Manual, clean_->isChecked() ? 1 : 0), true, this);
    });
  }, QMargins(0, 0, 0, 0));

  b.check(enabled_, tr("Enable automatic backups"), QString(),
          QStringLiteral("export restore archive snapshot"));
  enabled_->setChecked(AppSettings::backupEnabled.get());

  QWidget *triggers = b.indented(QMargins(18, 4, 0, 4), [this](OptionsPageBuilder &sub) {
    sub.check(onExit_, tr("On application exit"), QString(), QStringLiteral("quit close"))
        ->setChecked(AppSettings::backupExit.get());
    sub.check(subscriptions_, tr("After feed subscriptions are changed"))
        ->setChecked(AppSettings::backupSubscriptions.get());
    sub.check(scheduled_, tr("On a schedule"), QString(), QStringLiteral("interval timer"))
        ->setChecked(AppSettings::backupScheduled.get());

    QWidget *frequencies = sub.indented(QMargins(18, 4, 0, 4), [this](OptionsPageBuilder &freq) {
      frequency_ = new QButtonGroup(this);
      const QList<int> days{1, 2, 7, 30};
      const QStringList names{tr("Every day"), tr("Every 2 days"), tr("Every week"), tr("Every month")};
      const int saved = days.contains(AppSettings::backupFrequency.get())
          ? AppSettings::backupFrequency.get() : 1;
      for (int i = 0; i < days.size(); ++i) {
        QRadioButton *radio = freq.radio(names.at(i), scheduled_->text());
        frequency_->addButton(radio, days.at(i));
        radio->setChecked(days.at(i) == saved);
      }
    });
    OptionsPageBuilder::bindEnabled(scheduled_, {frequencies});
  });
  OptionsPageBuilder::bindEnabled(enabled_, {triggers});

  b.spinRow(keep_, tr("Keep the most recent backup sets:"), 1, 10000,
            QString(), QString(), QStringLiteral("retention limit count"))
      ->setValue(AppSettings::backupKeep.get());

  b.check(clean_, tr("Keep only starred or labelled articles in backups"),
          QString(), QStringLiteral("starred labeled filter"))
      ->setChecked(AppSettings::backupClean.get());
  connect(clean_, &QCheckBox::toggled, this, [this](bool checked) {
    if (!checked) return;
    const auto answer = QMessageBox::warning(this, tr("Backups will exclude other articles"),
        tr("These backups will contain your subscriptions and settings, but only starred or labelled articles. "
           "All other articles—including unread articles—will be excluded.\n\n"
           "Restoring such a backup cannot recover excluded articles, and feeds may no longer provide them.\n\n"
           "Your current database will not be changed. Pre-upgrade safety backups always remain complete."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) clean_->setChecked(false);
  });

  QLabel *note = b.label(tr("Each backup set includes the database and application settings. "
      "Pre-upgrade safety backups are always enabled and always complete. "
      "Schedules run while the application is running, with a catch-up check at startup. "
      "Back Up Now uses the article selection above; other changes take effect when you accept Settings."));
  note->setWordWrap(true);
  b.stretch();
}
void BackupSettingsPage::save()
{
  AppSettings::backupEnabled.set(enabled_->isChecked());
  AppSettings::backupExit.set(onExit_->isChecked());
  AppSettings::backupSubscriptions.set(subscriptions_->isChecked());
  AppSettings::backupScheduled.set(scheduled_->isChecked());
  AppSettings::backupFrequency.set(frequency_->checkedId());
  AppSettings::backupClean.set(clean_->isChecked());
  AppSettings::backupKeep.set(keep_->value());
  DatabaseBackup::instance()->start();
}

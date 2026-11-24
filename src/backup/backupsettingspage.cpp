// SPDX-License-Identifier: GPL-3.0-or-later
#include "backupsettingspage.h"
#include "databasebackup.h"
#include "platform/filemanager.h"
#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"
#include <QtWidgets>

BackupSettingsPage::BackupSettingsPage(QWidget *parent,
                                       QVector<OptionSearchEntry> *searchEntries,
                                       int pageIndex)
  : QWidget(parent)
{
  const auto addSearch = [searchEntries, pageIndex](QWidget *target, const QString &title,
                                                    const QString &keywords = {}) {
    if (searchEntries && target && !title.isEmpty()) {
      searchEntries->append({pageIndex, -1, title, QString(), keywords, target});
    }
  };

  auto *layout = new QVBoxLayout(this);
  auto *location = new QLabel(tr("Backup directory: %1").arg(DatabaseBackup::directory()), this);
  location->setWordWrap(true);
  location->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(location);
  auto *buttons = new QHBoxLayout;
  auto *now = new QPushButton(tr("Back Up Now"), this);
  auto *show = new QPushButton(tr("Show Backups"), this);
  buttons->addWidget(now);
  buttons->addWidget(show);
  buttons->addStretch();
  layout->addLayout(buttons);
  addSearch(now, tr("Back Up Now"));
  addSearch(show, tr("Show Backups"));
  connect(show, &QPushButton::clicked, this, [this] {
    if (!QDir().mkpath(DatabaseBackup::directory())) {
      QMessageBox::warning(this, tr("Database Backup"), tr("Cannot create the backup directory: %1").arg(DatabaseBackup::directory()));
      return;
    }
    FileManager::openDirectory(DatabaseBackup::directory());
  });
  enabled_ = new QCheckBox(tr("Enable automatic backups"), this);
  enabled_->setChecked(AppSettings::backupEnabled.get());
  layout->addWidget(enabled_);
  addSearch(enabled_, enabled_->text());
  auto *triggers = new QWidget(this);
  auto *triggerLayout = new QVBoxLayout(triggers);
  onExit_ = new QCheckBox(tr("On application exit"), triggers);
  subscriptions_ = new QCheckBox(tr("After feed subscriptions are changed"), triggers);
  scheduled_ = new QCheckBox(tr("On a schedule"), triggers);
  onExit_->setChecked(AppSettings::backupExit.get());
  subscriptions_->setChecked(AppSettings::backupSubscriptions.get());
  scheduled_->setChecked(AppSettings::backupScheduled.get());
  triggerLayout->addWidget(onExit_);
  triggerLayout->addWidget(subscriptions_);
  triggerLayout->addWidget(scheduled_);
  addSearch(onExit_, onExit_->text());
  addSearch(subscriptions_, subscriptions_->text());
  addSearch(scheduled_, scheduled_->text());
  auto *frequencies = new QWidget(triggers);
  auto *frequencyLayout = new QVBoxLayout(frequencies);
  frequency_ = new QButtonGroup(this);
  const QList<int> days{1, 2, 7, 30};
  const QStringList names{tr("Every day"), tr("Every 2 days"), tr("Every week"), tr("Every month")};
  const int saved = days.contains(AppSettings::backupFrequency.get()) ? AppSettings::backupFrequency.get() : 1;
  for (int i = 0; i < days.size(); ++i) {
    auto *radio = new QRadioButton(names.at(i), frequencies);
    frequency_->addButton(radio, days.at(i));
    radio->setChecked(days.at(i) == saved);
    frequencyLayout->addWidget(radio);
    addSearch(radio, names.at(i), scheduled_->text());
  }
  triggerLayout->addWidget(frequencies);
  frequencies->setEnabled(scheduled_->isChecked());
  connect(scheduled_, &QCheckBox::toggled, frequencies, &QWidget::setEnabled);
  triggers->setEnabled(enabled_->isChecked());
  connect(enabled_, &QCheckBox::toggled, triggers, &QWidget::setEnabled);
  layout->addWidget(triggers);
  auto *retention = new QHBoxLayout;
  const QString keepLabel = tr("Keep the most recent backup sets:");
  retention->addWidget(new QLabel(keepLabel, this));
  keep_ = new QSpinBox(this);
  keep_->setRange(1, 10000);
  keep_->setValue(AppSettings::backupKeep.get());
  retention->addWidget(keep_);
  retention->addStretch();
  layout->addLayout(retention);
  addSearch(keep_, keepLabel);
  clean_ = new QCheckBox(tr("Keep only starred or labelled articles in backups"), this);
  clean_->setChecked(AppSettings::backupClean.get());
  layout->addWidget(clean_);
  addSearch(clean_, clean_->text());
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
  connect(now, &QPushButton::clicked, this, [this] {
    DatabaseBackup::report(DatabaseBackup::create(QSqlDatabase::database(),
        DatabaseBackup::Trigger::Manual, clean_->isChecked() ? 1 : 0), true, this);
  });
  auto *note = new QLabel(tr("Each backup set includes the database and application settings. "
      "Pre-upgrade safety backups are always enabled and always complete. "
      "Schedules run while the application is running, with a catch-up check at startup. "
      "Back Up Now uses the article selection above; other changes take effect when you accept Settings."), this);
  note->setWordWrap(true);
  layout->addWidget(note);
  layout->addStretch();
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

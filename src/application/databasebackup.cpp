// SPDX-License-Identifier: GPL-3.0-or-later
#include "databasebackup.h"
#include "filemanager.h"
#include "mainapplication.h"
#include "settings.h"
#include "projectmetadata.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSqlDriver>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <sqlite3.h>
#include <memory>
#include <atomic>

namespace {
using Handle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
QMutex backupMutex;
bool upgradeBackupHandled = false;
std::atomic<bool> subscriptionPending{false};
const char *triggerName(DatabaseBackup::Trigger trigger)
{
  switch (trigger) {
  case DatabaseBackup::Trigger::Manual: return "manual";
  case DatabaseBackup::Trigger::Subscription: return "subscription";
  case DatabaseBackup::Trigger::Schedule: return "schedule";
  case DatabaseBackup::Trigger::Exit: return "exit";
  case DatabaseBackup::Trigger::Upgrade: return "upgrade";
  case DatabaseBackup::Trigger::Prerelease: return "prerelease";
  }
  return "unknown";
}
QString sqlError(sqlite3 *db) { return QString::fromUtf8(sqlite3_errmsg(db)); }
bool execute(sqlite3 *db, const char *sql, QString &error)
{
  if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) return true;
  error = sqlError(db);
  return false;
}

bool copySnapshot(sqlite3 *source, sqlite3 *raw, QString &error)
{
  sqlite3_backup *copy = sqlite3_backup_init(raw, "main", source, "main");
  if (!copy) { error = sqlError(raw); return false; }
  QElapsedTimer timeout;
  timeout.start();
  int rc;
  do {
    rc = sqlite3_backup_step(copy, 256);
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) sqlite3_sleep(20);
  } while ((rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) && timeout.elapsed() < 10000);
  const int finishRc = sqlite3_backup_finish(copy);
  if (rc != SQLITE_DONE || finishRc != SQLITE_OK) {
    error = DatabaseBackup::tr("SQLite snapshot failed (code %1): %2").arg(rc).arg(sqlError(raw));
    return false;
  }

  return true;
}

// Hash logical rows, not SQLite page counters/free space. This also observes
// changes made through other connections and survives application restarts.
QString fingerprint(sqlite3 *db, QString &error)
{
  QCryptographicHash hash(QCryptographicHash::Sha256);
  auto field = [&hash](const QByteArray &value) {
    hash.addData(QByteArray::number(value.size()) + ':' + value);
  };
  sqlite3_stmt *tables = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT name, sql FROM sqlite_master WHERE type='table' "
                           "AND name NOT LIKE 'sqlite_%' ORDER BY name", -1, &tables, nullptr) != SQLITE_OK) {
    error = sqlError(db); return {};
  }
  int rc;
  while ((rc = sqlite3_step(tables)) == SQLITE_ROW) {
    const QByteArray name(reinterpret_cast<const char *>(sqlite3_column_text(tables, 0)));
    field(name);
    field(QByteArray(reinterpret_cast<const char *>(sqlite3_column_text(tables, 1))));
    QByteArray quoted = name;
    quoted.replace("\"", "\"\"");
    const QByteArray sql = "SELECT * FROM \"" + quoted + "\" ORDER BY rowid";
    sqlite3_stmt *rows = nullptr;
    if (sqlite3_prepare_v2(db, sql.constData(), -1, &rows, nullptr) != SQLITE_OK) {
      error = sqlError(db); break;
    }
    int rowRc;
    while ((rowRc = sqlite3_step(rows)) == SQLITE_ROW) {
      field("row");
      for (int i = 0; i < sqlite3_column_count(rows); ++i) {
        field(QByteArray::number(sqlite3_column_type(rows, i)));
        field(QByteArray(static_cast<const char *>(sqlite3_column_blob(rows, i)),
                         sqlite3_column_bytes(rows, i)));
      }
    }
    if (rowRc != SQLITE_DONE) error = sqlError(db);
    sqlite3_finalize(rows);
    if (!error.isEmpty()) break;
  }
  if (rc != SQLITE_DONE && error.isEmpty()) error = sqlError(db);
  sqlite3_finalize(tables);
  return error.isEmpty() ? QString::fromLatin1(hash.result().toHex()) : QString();
}

bool filterCopy(sqlite3 *db, QString &error)
{
  return execute(db,
      "PRAGMA secure_delete=ON; BEGIN;"
      "DELETE FROM news WHERE COALESCE(starred,0)!=1 AND "
      "(label IS NULL OR trim(label, ', ')= '');"
      "DELETE FROM news_ex WHERE newsId IS NULL OR newsId NOT IN (SELECT id FROM news);"
      "UPDATE feeds SET currentNews=0 WHERE currentNews NOT IN (SELECT id FROM news);"
      "UPDATE labels SET currentNews=0 WHERE currentNews NOT IN (SELECT id FROM news);"
      "UPDATE feeds SET "
      "unread=(SELECT count(*) FROM news WHERE feedId=feeds.id AND deleted=0 AND read=0),"
      "newCount=(SELECT count(*) FROM news WHERE feedId=feeds.id AND deleted=0 AND new=1),"
      "undeleteCount=(SELECT count(*) FROM news WHERE feedId=feeds.id AND deleted=0);"
      "WITH RECURSIVE descendants(root,id) AS ("
      "SELECT id,id FROM feeds UNION "
      "SELECT d.root,f.id FROM descendants d JOIN feeds f ON f.parentId=d.id) "
      "UPDATE feeds SET "
      "unread=(SELECT count(*) FROM news WHERE deleted=0 AND read=0 AND feedId IN "
      "(SELECT id FROM descendants WHERE root=feeds.id)),"
      "newCount=(SELECT count(*) FROM news WHERE deleted=0 AND new=1 AND feedId IN "
      "(SELECT id FROM descendants WHERE root=feeds.id)),"
      "undeleteCount=(SELECT count(*) FROM news WHERE deleted=0 AND feedId IN "
      "(SELECT id FROM descendants WHERE root=feeds.id)) "
      "WHERE xmlUrl IS NULL OR xmlUrl='';"
      "COMMIT; VACUUM;", error);
}
}

QString DatabaseBackup::directory()
{
  return QDir(mainApp->dataDir()).filePath(ProjectMetadata::backup());
}

void DatabaseBackup::skipUpgradeBackup()
{
  QMutexLocker lock(&backupMutex);
  upgradeBackupHandled = true;
}

DatabaseBackup::Result DatabaseBackup::create(QSqlDatabase db, Trigger trigger, int cleanOverride)
{
  // Finish a coalesced subscription request even if the user exits during its
  // one-second delay and has disabled the independent exit trigger.
  if (trigger == Trigger::Exit && subscriptionPending.exchange(false)) {
    qInfo() << "[backup] Processing pending subscription backup before exit";
    const Result pending = create(db, Trigger::Subscription);
    if (!pending.error.isEmpty()) return pending;
  }
  QMutexLocker lock(&backupMutex);
  Result result;
  const char *triggerLabel = triggerName(trigger);
  Settings settings;
  const bool manual = trigger == Trigger::Manual;
  const bool prerelease = trigger == Trigger::Prerelease;
  const bool upgrade = trigger == Trigger::Upgrade || prerelease;
  // The startup gate already backed up this session, or the user declined.
  if (trigger == Trigger::Upgrade && upgradeBackupHandled) {
    qInfo() << "[backup] Skipped: trigger=" << triggerLabel << "startup backup already handled";
    result.skipped = true; return result;
  }
  const bool clean = !upgrade && (cleanOverride < 0 ? AppSettings::backupClean.get() : cleanOverride != 0);
  if (!manual && !upgrade && (!AppSettings::backupEnabled.get() ||
      (trigger == Trigger::Exit && !AppSettings::backupExit.get()) ||
      (trigger == Trigger::Subscription && !AppSettings::backupSubscriptions.get()) ||
      (trigger == Trigger::Schedule && !AppSettings::backupScheduled.get()))) {
    qInfo() << "[backup] Skipped: trigger=" << triggerLabel << "disabled in settings";
    result.skipped = true; return result;
  }
  qInfo() << "[backup] Starting: trigger=" << triggerLabel << "filtered=" << clean;
  const QString root = directory();
  auto fail = [&](const QString &error) {
    qWarning() << "[backup] Failed: trigger=" << triggerLabel << error;
    result.error = tr("Could not create a backup in %1:\n%2").arg(root, error);
    return result;
  };
  if (!QDir().mkpath(root)) return fail(tr("Cannot create the backup directory."));
  QTemporaryDir staging(QDir(root).filePath(".incomplete-XXXXXX"));
  if (!staging.isValid()) return fail(staging.errorString());
  const QString dbName = ProjectMetadata::database() + ".backup";
  const QString iniName = QCoreApplication::applicationName() + ".ini.backup";
  QString digest;
  if (prerelease) {
    // Protect both the current file and any pending replacement before
    // connectDatabase() renames it. Read-only SQLite snapshots include WAL data.
    for (const QString &path : QStringList{mainApp->dbFileName(), mainApp->dbFileName() + ".bak"}) {
      if (!QFileInfo::exists(path)) continue;
      sqlite3 *source = nullptr;
      const int sourceRc = sqlite3_open_v2(path.toUtf8().constData(), &source,
                                          SQLITE_OPEN_READONLY, nullptr);
      Handle sourceHandle(source, sqlite3_close);
      if (sourceRc != SQLITE_OK)
        return fail(source ? sqlError(source) : tr("Cannot open the database for backup."));
      sqlite3 *target = nullptr;
      const QString destination = staging.filePath(QFileInfo(path).fileName() + ".backup");
      const int targetRc = sqlite3_open(destination.toUtf8().constData(), &target);
      Handle targetHandle(target, sqlite3_close);
      if (targetRc != SQLITE_OK)
        return fail(target ? sqlError(target) : tr("Cannot open the backup database."));
      QString error;
      if (!copySnapshot(source, target, error)) return fail(error);
    }
  } else {
    sqlite3 *source = nullptr;
    Handle upgradeSource(nullptr, sqlite3_close);
    if (upgrade) {
      // Initialization uses Qt's QSQLITE plugin, which may bundle a different
      // SQLite library. Never pass its native handle to our linked SQLite API.
      // No migrations have run yet; read the committed database independently.
      const int rc = sqlite3_open_v2(mainApp->dbFileName().toUtf8().constData(),
                                    &source, SQLITE_OPEN_READONLY, nullptr);
      upgradeSource.reset(source);
      if (rc != SQLITE_OK)
        return fail(source ? sqlError(source) : tr("Cannot open the database before upgrade."));
    } else {
      QVariant value = db.driver() ? db.driver()->handle() : QVariant();
      if (!value.isValid() || qstrcmp(value.typeName(), "sqlite3*") != 0)
        return fail(tr("The live SQLite connection is unavailable."));
      source = *static_cast<sqlite3 **>(value.data());
    }
    if (!source) return fail(tr("The live SQLite connection is closed."));
    sqlite3 *raw = nullptr;
    const int openRc = sqlite3_open(staging.filePath(dbName).toUtf8().constData(), &raw);
    Handle target(raw, sqlite3_close);
    if (openRc != SQLITE_OK) return fail(raw ? sqlError(raw) : tr("Cannot open the backup database."));
    QString error;
    if (!copySnapshot(source, raw, error)) return fail(error);

    if (clean && !filterCopy(raw, error)) return fail(error);
    digest = fingerprint(raw, error) + (clean ? ":clean" : ":full");
    if (!error.isEmpty()) return fail(error);
    if (!manual && !upgrade && settings.value("Backup/lastDigest").toString() == digest &&
        QFileInfo::exists(settings.value("Backup/lastDirectory").toString() + '/' + dbName) &&
        QFileInfo::exists(settings.value("Backup/lastDirectory").toString() + '/' + iniName)) {
      qInfo() << "[backup] Skipped: trigger=" << triggerLabel << "database unchanged";
      result.skipped = true; return result;
    }
    target.reset(); // Close every handle before publishing/renaming on Windows.
  }
  // Preserve the original settings before startup changes them. A profile may
  // have only settings, only a database, or neither on its first run.
  if (!prerelease && !Settings::syncSettings())
    return fail(tr("Cannot write the current application settings."));
  QFile ini(settings.fileName());
  if ((!prerelease || ini.exists()) && !ini.copy(staging.filePath(iniName)))
    return fail(ini.errorString());
  if (prerelease && QDir(staging.path()).entryList(QDir::Files).isEmpty()) {
    qInfo() << "[backup] Skipped: trigger=" << triggerLabel << "no existing profile files";
    result.skipped = true; return result;
  }
  const QString stem = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") +
      "_v" + QCoreApplication::applicationVersion();
  QString name = stem;
  for (int suffix = 2; QDir(root).exists(name); ++suffix) name = stem + '_' + QString::number(suffix);
  if (!QDir(root).rename(staging.path(), QDir(root).filePath(name)))
    return fail(tr("Cannot publish the completed backup."));
  staging.setAutoRemove(false);
  result.directory = QDir(root).filePath(name);
  if (prerelease) upgradeBackupHandled = true;
  if (!upgrade) {
    settings.setValue("Backup/lastDigest", digest);
    settings.setValue("Backup/lastDirectory", result.directory);
    settings.setValue("Backup/lastSuccess", QDateTime::currentDateTimeUtc());
    if (!Settings::syncSettings())
      result.error = tr("Backup created in %1, but its scheduling state could not be saved.\n").arg(result.directory);
  }

  // Only touch complete sets created under our naming convention. Legacy and
  // unrelated files are deliberately outside this retention policy.
  const QRegularExpression pattern("^\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}_v[0-9]+(?:\\.[0-9]+)*(?:_[0-9]+)?$");
  QStringList sets;
  const auto entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Time);
  for (const auto &entry : entries) {
    const QDir set(entry.absoluteFilePath());
    if (pattern.match(entry.fileName()).hasMatch() &&
        set.entryList(QDir::Files | QDir::Hidden | QDir::System).size() == 2 &&
        set.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty() &&
        QFileInfo(set.filePath(dbName)).isFile() && QFileInfo(set.filePath(iniName)).isFile())
      sets.append(entry.absoluteFilePath());
  }
  sets.removeAll(result.directory);
  sets.prepend(result.directory);
  const int keep = qMax(1, AppSettings::backupKeep.get());
  for (int i = keep; i < sets.size(); ++i) {
    if (!QDir(sets.at(i)).removeRecursively())
      result.error += tr("Backup created, but an old backup could not be removed: %1\n").arg(sets.at(i));
  }
  qInfo() << "Database backup created:" << result.directory << "filtered:" << clean
          << "trigger:" << triggerLabel;
  return result;
}

void DatabaseBackup::report(const Result &result, bool manual, QWidget *parent)
{
  // Repeated automatic failures remain in the log without stacking dialogs.
  static QString lastAutomaticError;
  const QString created = result.directory.isEmpty() ? QString() :
      tr("Backup created:\n%1\n%2").arg(
          QDir(result.directory).filePath(ProjectMetadata::database() + ".backup"),
          QDir(result.directory).filePath(QCoreApplication::applicationName() + ".ini.backup"));
  if (!result.error.isEmpty()) {
    qWarning().noquote() << result.error;
    if (!manual && result.error == lastAutomaticError) return;
    if (!manual) lastAutomaticError = result.error;
    QMessageBox::warning(parent ? parent : QApplication::activeWindow(), tr("Database Backup"),
        created.isEmpty() ? result.error : created + "\n\n" + result.error);
  } else if (manual && !result.directory.isEmpty()) {
    QMessageBox dialog(QMessageBox::Information, tr("Database Backup"),
                       tr("Backup successfully created"), QMessageBox::Ok,
                       parent ? parent : QApplication::activeWindow());
    QPushButton *show = dialog.addButton(tr("Show"), QMessageBox::ActionRole);
    dialog.setDefaultButton(QMessageBox::Ok);
    dialog.exec();
    if (dialog.clickedButton() == show)
      FileManager::openDirectory(result.directory);
  }
  if (result.error.isEmpty() && !result.skipped) lastAutomaticError.clear();
}

DatabaseBackup *DatabaseBackup::instance()
{
  Q_ASSERT(QThread::currentThread() == qApp->thread());
  static DatabaseBackup *service = new DatabaseBackup(qApp);
  return service;
}
DatabaseBackup::DatabaseBackup(QObject *parent) : QObject(parent)
{
  scheduleTimer_.setInterval(60000);
  subscriptionTimer_.setSingleShot(true);
  subscriptionTimer_.setInterval(1000);
  connect(&scheduleTimer_, &QTimer::timeout, this, &DatabaseBackup::scheduled);
  connect(&subscriptionTimer_, &QTimer::timeout, this, [this] {
    if (!stopped_ && subscriptionPending.exchange(false)) {
      const Result result = create(QSqlDatabase::database(), Trigger::Subscription);
      if (!result.error.isEmpty()) subscriptionPending = true;
      report(result, false);
    }
  });
}
void DatabaseBackup::start()
{
  stopped_ = false;
  nextScheduleCheck_ = QDateTime();
  scheduleTimer_.start();
  QTimer::singleShot(0, this, [this] { scheduled(); });
}
void DatabaseBackup::stop()
{
  stopped_ = true;
  scheduleTimer_.stop();
  subscriptionTimer_.stop();
}
void DatabaseBackup::manual(QWidget *parent)
{
  report(create(QSqlDatabase::database(), Trigger::Manual), true, parent);
}
void DatabaseBackup::subscriptionsChanged(const char *reason)
{
  if (!AppSettings::backupEnabled.get() || !AppSettings::backupSubscriptions.get()) return;
  qInfo() << "[backup] Subscription backup requested by:" << reason;
  subscriptionPending = true;
  QMetaObject::invokeMethod(qApp, [] {
    auto *service = instance();
    if (!service->stopped_) service->subscriptionTimer_.start();
  }, Qt::QueuedConnection);
}
void DatabaseBackup::scheduled()
{
  if (stopped_ || !AppSettings::backupEnabled.get() || !AppSettings::backupScheduled.get()) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (nextScheduleCheck_.isValid() && now < nextScheduleCheck_) return;
  const QDateTime last = Settings().value("Backup/lastSuccess").toDateTime();
  const int days = AppSettings::backupFrequency.get();
  const QDateTime due = days == 30 ? last.addMonths(1) : last.addDays(days == 2 || days == 7 ? days : 1);
  if (!last.isValid() || due <= now) {
    // A due but unchanged database (or a failing destination) should not cause
    // a full snapshot attempt every minute. Retry at most hourly.
    nextScheduleCheck_ = now.addSecs(3600);
    report(create(QSqlDatabase::database(), Trigger::Schedule), false);
  }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pendingdatabase.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <sqlite3.h>

namespace {
using Handle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

QString tr(const char *text)
{
  return QCoreApplication::translate("PendingDatabase", text);
}

bool validate(sqlite3 *source, QString &error)
{
  sqlite3_stmt *raw = nullptr;
  int rc = sqlite3_prepare_v2(source, "PRAGMA quick_check", -1, &raw, nullptr);
  Statement check(raw, sqlite3_finalize);
  if (rc != SQLITE_OK) {
    error = QString::fromUtf8(sqlite3_errmsg(source));
    return false;
  }
  rc = sqlite3_step(check.get());
  if (rc != SQLITE_ROW ||
      qstrcmp(reinterpret_cast<const char *>(sqlite3_column_text(check.get(), 0)), "ok") != 0 ||
      sqlite3_step(check.get()) != SQLITE_DONE) {
    error = tr("The pending database failed its integrity check.");
    return false;
  }
  check.reset();

  // Reject empty/unrelated SQLite files without imposing the current schema
  // version: normal initialization still handles supported older databases.
  for (const char *sql : {"SELECT id FROM feeds LIMIT 0", "SELECT id FROM news LIMIT 0",
                          "SELECT name, value FROM info LIMIT 0"}) {
    raw = nullptr;
    rc = sqlite3_prepare_v2(source, sql, -1, &raw, nullptr);
    Statement statement(raw, sqlite3_finalize);
    if (rc != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_DONE) {
      error = tr("The pending file is not a supported feed database: %1")
                  .arg(QString::fromUtf8(sqlite3_errmsg(source)));
      return false;
    }
  }
  return true;
}
}

bool PendingDatabase::install(const QString &databasePath, QString &error)
{
  error.clear();
  const QString pendingPath = databasePath + QStringLiteral(".bak");
  const QFileInfo pending(pendingPath);
  if (!pending.exists() && !pending.isSymLink()) return true;
  if (!pending.isFile() || pending.size() == 0) {
    error = tr("The pending database is missing, empty or not a regular file: %1").arg(pendingPath);
    return false;
  }
  // Consuming just the main file would strand a source WAL at this filename.
  // Require a standalone replacement; never discard or manually unlink its WAL.
  if (QFileInfo(pendingPath + QStringLiteral("-wal")).size() > 0) {
    error = tr("The pending database has a WAL file. Checkpoint and close it before restarting: %1")
                .arg(pendingPath);
    return false;
  }
  if (pending.canonicalFilePath() == QFileInfo(databasePath).canonicalFilePath()) {
    error = tr("The pending database and the current database refer to the same file.");
    return false;
  }

  sqlite3 *raw = nullptr;
  int rc = sqlite3_open_v2(pendingPath.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
  Handle source(raw, sqlite3_close);
  auto fail = [&](sqlite3 *db, int code) {
    error = QString::fromUtf8(db ? sqlite3_errmsg(db) : sqlite3_errstr(code));
    return false;
  };
  if (rc != SQLITE_OK) return fail(source.get(), rc);
  // Hold the validated snapshot through the copy.
  rc = sqlite3_exec(source.get(), "BEGIN", nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK) return fail(source.get(), rc);
  if (!validate(source.get(), error)) return false;

  raw = nullptr;
  rc = sqlite3_open_v2(databasePath.toUtf8().constData(), &raw,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  Handle target(raw, sqlite3_close);
  if (rc != SQLITE_OK) return fail(target.get(), rc);
  // Use SQLite's transaction and journals, rather than unlinking/replacing an
  // openable database file. This also preserves a database symlink's target.
  rc = sqlite3_exec(target.get(), "PRAGMA synchronous=FULL", nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK) return fail(target.get(), rc);
  sqlite3_backup *copy = sqlite3_backup_init(target.get(), "main", source.get(), "main");
  if (!copy) return fail(target.get(), sqlite3_errcode(target.get()));
  QElapsedTimer timeout;
  timeout.start();
  do {
    rc = sqlite3_backup_step(copy, 256);
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
      if (timeout.elapsed() >= 10000) break;
      sqlite3_sleep(20);
    }
  } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
  // An incomplete backup is rolled back by finish(); it must be called once
  // even after a failed step. Finish succeeding alone does not imply completion.
  const int finishRc = sqlite3_backup_finish(copy);
  if (rc != SQLITE_DONE || finishRc != SQLITE_OK) {
    error = tr("Cannot install the pending database (copy: %1, finish: %2): %3")
                .arg(rc).arg(finishRc).arg(QString::fromUtf8(sqlite3_errmsg(target.get())));
    return false;
  }
  // Close both connections before removing the consumed file on Windows.
  target.reset();
  source.reset();
  QFile consumed(pendingPath);
  if (!consumed.remove()) {
    error = tr("The replacement was installed, but its pending file could not be removed: %1\n%2")
                .arg(pendingPath, consumed.errorString());
    return false;
  }
  return true;
}

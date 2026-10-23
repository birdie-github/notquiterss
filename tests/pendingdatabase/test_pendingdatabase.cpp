// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <memory>
#include <sqlite3.h>
#include "pendingdatabase.h"

namespace {
using Handle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
bool create(const QString &path, int id)
{
  sqlite3 *raw = nullptr;
  const int rc = sqlite3_open(path.toUtf8().constData(), &raw);
  Handle db(raw, sqlite3_close);
  const QByteArray sql = "CREATE TABLE feeds(id); CREATE TABLE news(id); "
                         "CREATE TABLE info(name,value); INSERT INTO feeds VALUES(" +
                         QByteArray::number(id) + ");";
  return rc == SQLITE_OK && sqlite3_exec(db.get(), sql.constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

int value(const QString &path)
{
  sqlite3 *raw = nullptr;
  const int rc = sqlite3_open_v2(path.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
  Handle db(raw, sqlite3_close);
  if (rc != SQLITE_OK) return -1;
  sqlite3_stmt *statement = nullptr;
  const int prepared = sqlite3_prepare_v2(db.get(), "SELECT id FROM feeds", -1, &statement, nullptr);
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement, sqlite3_finalize);
  return prepared == SQLITE_OK && sqlite3_step(query.get()) == SQLITE_ROW
      ? sqlite3_column_int(query.get(), 0) : -1;
}
}

class PendingDatabaseTest : public QObject
{
  Q_OBJECT
private slots:
  void noReplacement()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    QString error;
    QVERIFY(PendingDatabase::install(path, error));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFileInfo::exists(path));
  }

  void successfulReplacement_data()
  {
    QTest::addColumn<bool>("existing");
    QTest::newRow("existing database") << true;
    QTest::newRow("missing database") << false;
  }
  void successfulReplacement()
  {
    QFETCH(bool, existing);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    if (existing) QVERIFY(create(path, 1));
    QVERIFY(create(path + ".bak", 2));
    QString error;
    QVERIFY2(PendingDatabase::install(path, error), qPrintable(error));
    QCOMPARE(value(path), 2);
    QVERIFY(!QFileInfo::exists(path + ".bak"));
  }

  void invalidReplacement_data()
  {
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("corrupt") << QByteArray("not a database");
    QTest::newRow("empty") << QByteArray();
  }
  void invalidReplacement()
  {
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    QVERIFY(create(path, 1));
    QFile pending(path + ".bak");
    QVERIFY(pending.open(QIODevice::WriteOnly));
    QCOMPARE(pending.write(bytes), qint64(bytes.size()));
    pending.close();
    QString error;
    QVERIFY(!PendingDatabase::install(path, error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(value(path), 1);
    QVERIFY(pending.exists());
  }

  void unrelatedSqliteFile()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    QVERIFY(create(path, 1));
    sqlite3 *raw = nullptr;
    QCOMPARE(sqlite3_open((path + ".bak").toUtf8().constData(), &raw), SQLITE_OK);
    Handle pending(raw, sqlite3_close);
    QCOMPARE(sqlite3_exec(pending.get(), "CREATE TABLE unrelated(id)", nullptr, nullptr, nullptr), SQLITE_OK);
    pending.reset();
    QString error;
    QVERIFY(!PendingDatabase::install(path, error));
    QCOMPARE(value(path), 1);
    QVERIFY(QFileInfo::exists(path + ".bak"));
  }

  void lockedTarget()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    QVERIFY(create(path, 1));
    QVERIFY(create(path + ".bak", 2));
    sqlite3 *raw = nullptr;
    QCOMPARE(sqlite3_open(path.toUtf8().constData(), &raw), SQLITE_OK);
    Handle lock(raw, sqlite3_close);
    QCOMPARE(sqlite3_exec(lock.get(), "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr), SQLITE_OK);
    QString error;
    QVERIFY(!PendingDatabase::install(path, error));
    QVERIFY(!error.isEmpty());
    lock.reset();
    QCOMPARE(value(path), 1);
    QCOMPARE(value(path + ".bak"), 2);
  }

  void pendingWal()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("feeds.db");
    QVERIFY(create(path, 1));
    QVERIFY(create(path + ".bak", 2));
    sqlite3 *raw = nullptr;
    QCOMPARE(sqlite3_open((path + ".bak").toUtf8().constData(), &raw), SQLITE_OK);
    Handle pending(raw, sqlite3_close);
    QCOMPARE(sqlite3_exec(pending.get(), "PRAGMA journal_mode=WAL; INSERT INTO feeds VALUES(3)",
                         nullptr, nullptr, nullptr), SQLITE_OK);
    QVERIFY(QFileInfo(path + ".bak-wal").size() > 0);
    QString error;
    QVERIFY(!PendingDatabase::install(path, error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(value(path), 1);
    QVERIFY(QFileInfo::exists(path + ".bak"));
  }

  void symlinkTarget()
  {
#ifdef Q_OS_UNIX
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString real = dir.filePath("real.db");
    const QString path = dir.filePath("feeds.db");
    QVERIFY(create(real, 1));
    QVERIFY(QFile::link(real, path));
    QVERIFY(create(path + ".bak", 2));
    QString error;
    QVERIFY2(PendingDatabase::install(path, error), qPrintable(error));
    QVERIFY(QFileInfo(path).isSymLink());
    QCOMPARE(value(real), 2);
#else
    QSKIP("Unix database symlink test");
#endif
  }
};
QTEST_GUILESS_MAIN(PendingDatabaseTest)
#include "test_pendingdatabase.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlTableModel>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <memory>
#include <sqlite3.h>
#include "sqlitedriver.h"

namespace {
QString uniqueName()
{
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

struct Connection {
  QSqlDatabase db;
  Connection(const QString &target, bool memory)
    : db(QSqlDatabase::addDatabase(new SQLiteDriver(), uniqueName()))
  {
    db.setDatabaseName(target);
    if (memory) db.setConnectOptions("QSQLITE_OPEN_URI;QSQLITE_IMMEDIATE_TRANSACTIONS");
    db.open();
  }
  ~Connection()
  {
    const QString name = db.connectionName();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
  sqlite3 *handle() const
  {
    QVariant v = db.driver()->handle();
    return *static_cast<sqlite3 **>(v.data());
  }
};
}

class SqlOwnershipTest : public QObject
{
  Q_OBJECT
private slots:
  void connections_data()
  {
    QTest::addColumn<bool>("memory");
    QTest::newRow("disk") << false;
    QTest::newRow("memory") << true;
  }

  void connections()
  {
    QFETCH(bool, memory);
    const QStringList before = QSqlDatabase::connectionNames();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = memory ? "file:/sqlownership-" + uniqueName() + "?vfs=memdb"
                                  : directory.filePath("feeds.db");
    {
      Connection gui(target, memory);
      QVERIFY2(gui.db.isOpen(), qPrintable(gui.db.lastError().text()));
      QVERIFY(gui.db.driver()->thread() == QThread::currentThread());
      {
        QSqlQuery q(gui.db);
        QVERIFY(q.exec("CREATE TABLE news(id INTEGER PRIMARY KEY, title TEXT)"));
        QVERIFY(gui.db.transaction());
        for (int i = 0; i < 600; ++i) {
          q.prepare("INSERT INTO news VALUES(?, ?)");
          q.addBindValue(i);
          q.addBindValue(QString::number(i));
          QVERIFY(q.exec());
        }
        QVERIFY(gui.db.commit());
      }
      // More rows than Qt's initial model batch. Drain it as NewsModel does,
      // retaining cached data without holding a reader lock while the GUI waits.
      QSqlTableModel model(nullptr, gui.db);
      model.setTable("news");
      QVERIFY(model.select());
      while (model.canFetchMore()) model.fetchMore();
      QCOMPARE(model.rowCount(), 600);

      const auto guiHandle = gui.handle();
      QString error;
      auto worker = std::unique_ptr<QThread>(QThread::create([&] {
        Connection update(target, memory);
        if (!update.db.isOpen() || update.db.driver()->thread() != QThread::currentThread()
            || update.handle() == guiHandle) {
          error = "Worker did not open its own driver/native connection";
          return;
        }
        QSqlQuery q(update.db);
        if (!q.exec("UPDATE news SET title='worker' WHERE id=1")) {
          error = q.lastError().text(); return;
        }
        if (!update.db.transaction() ||
            !q.exec("UPDATE news SET title='discard' WHERE id=2") ||
            !update.db.rollback()) {
          error = "Worker transaction/rollback failed"; return;
        }
        // Query registration/destruction remains entirely in this worker.
        for (int i = 0; i < 1000; ++i) {
          QSqlQuery transient(update.db);
          if (!transient.exec("SELECT count(*) FROM news") || !transient.next()
              || transient.value(0).toInt() != 600) {
            error = "Worker result lifecycle failed"; return;
          }
        }
      }));
      worker->start();
      // Exercise GUI query lifetimes concurrently with the worker's result list.
      for (int i = 0; i < 1000; ++i) {
        QSqlQuery transient(gui.db);
        if (!transient.exec("SELECT count(*) FROM news") || !transient.next()) {
          // Always join before assertions can return and destroy a running thread.
          const QString detail = transient.lastError().text();
          transient.finish();
          worker->wait();
          QFAIL(qPrintable(detail));
        }
      }
      worker->wait();
      QVERIFY2(error.isEmpty(), qPrintable(error));
      QVERIFY(model.select());
      while (model.canFetchMore()) model.fetchMore();
      QSqlQuery q(gui.db);
      QVERIFY(q.exec("SELECT title FROM news WHERE id=1"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toString(), QString("worker"));
      QVERIFY(q.exec("SELECT title FROM news WHERE id=2"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toString(), QString("2"));
      q.finish();
      if (memory) {
        QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
        sqlite3 *file = nullptr;
        const QString saved = directory.filePath("saved.db");
        QCOMPARE(sqlite3_open(saved.toUtf8().constData(), &file), SQLITE_OK);
        sqlite3_backup *copy = sqlite3_backup_init(file, "main", gui.handle(), "main");
        const int step = copy ? sqlite3_backup_step(copy, -1) : SQLITE_ERROR;
        const int finish = copy ? sqlite3_backup_finish(copy) : SQLITE_ERROR;
        sqlite3_close(file);
        QCOMPARE(step, SQLITE_DONE);
        QCOMPARE(finish, SQLITE_OK);
        Connection persisted(saved, false);
        QSqlQuery savedQuery(persisted.db);
        QVERIFY(savedQuery.exec("SELECT title FROM news WHERE id=1"));
        QVERIFY(savedQuery.next());
        QCOMPARE(savedQuery.value(0).toString(), QString("worker"));
      }
    }
    QCOMPARE(QSqlDatabase::connectionNames().size(), before.size());
    if (memory) {
      Connection reopened(target, true);
      QSqlQuery q(reopened.db);
      QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE name='news'"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toInt(), 0); // Last close releases the shared RAM store.
    }
  }
};

QTEST_GUILESS_MAIN(SqlOwnershipTest)
#include "test_sqlownership.moc"

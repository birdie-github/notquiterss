// SPDX-License-Identifier: GPL-3.0-or-later
#include "feeddeletion.h"
#include <QtTest>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

class FeedDeletionTest : public QObject
{
  Q_OBJECT
  QSqlDatabase db;

  QVariant value(const QString &sql)
  {
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return {};
    return q.value(0);
  }

  QStringList snapshot()
  {
    QStringList result;
    QSqlQuery q(db);
    for (const char *table : {"feeds", "news", "feeds_ex", "news_ex"}) {
      if (!q.exec(QString("SELECT * FROM %1 ORDER BY id").arg(QString::fromLatin1(table))))
        return {"snapshot failed"};
      while (q.next()) {
        QString row = QString::fromLatin1(table);
        for (int i = 0; i < q.record().count(); ++i) row += '|' + q.value(i).toString();
        result.append(row);
      }
    }
    return result;
  }

private slots:
  void init()
  {
    db = QSqlDatabase::addDatabase("QSQLITE", "feed-deletion-test");
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE feeds(id INTEGER PRIMARY KEY, parentId, rowToParent, "
                   "unread, undeleteCount, newCount)"));
    // Folder 1 contains feed 2, folder 3/feed 4, and surviving feed 5.
    // A separate root feed 6 must remain untouched except for root ordering.
    QVERIFY(q.exec("INSERT INTO feeds VALUES(1,0,0,6,9,3),(2,1,0,1,2,1),"
                   "(3,1,1,2,3,1),(4,3,0,2,3,1),(5,1,2,3,4,1),(6,0,1,4,5,2)"));
    QVERIFY(q.exec("CREATE TABLE news(id INTEGER PRIMARY KEY, feedId)"));
    QVERIFY(q.exec("CREATE TABLE feeds_ex(id INTEGER PRIMARY KEY, feedId)"));
    QVERIFY(q.exec("CREATE TABLE news_ex(id INTEGER PRIMARY KEY, feedId, newsId)"));
    for (int id : {2, 4, 5, 6}) {
      QVERIFY(q.exec(QString("INSERT INTO news VALUES(%1,%1)").arg(id)));
      QVERIFY(q.exec(QString("INSERT INTO feeds_ex VALUES(%1,%1)").arg(id)));
      QVERIFY(q.exec(QString("INSERT INTO news_ex VALUES(%1,%1,%1)").arg(id)));
    }
  }

  void cleanup()
  {
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase("feed-deletion-test");
  }

  void overlappingSelection()
  {
    QString error;
    QVERIFY2(FeedDeletion::apply(db, {3, 4, 3}, error), qPrintable(error));
    QCOMPARE(value("SELECT count(*) FROM feeds WHERE id IN(3,4)").toInt(), 0);
    QCOMPARE(value("SELECT count(*) FROM news").toInt(), 3);
    QCOMPARE(value("SELECT count(*) FROM feeds_ex").toInt(), 3);
    QCOMPARE(value("SELECT count(*) FROM news_ex").toInt(), 3);
    QCOMPARE(value("SELECT rowToParent FROM feeds WHERE id=5").toInt(), 1);
    QCOMPARE(value("SELECT unread FROM feeds WHERE id=1").toInt(), 4);
    QCOMPARE(value("SELECT undeleteCount FROM feeds WHERE id=1").toInt(), 6);
    QCOMPARE(value("SELECT newCount FROM feeds WHERE id=1").toInt(), 2);
    QCOMPARE(value("SELECT unread FROM feeds WHERE id=6").toInt(), 4);
  }

  void wholeFolder()
  {
    QString error;
    QVERIFY2(FeedDeletion::apply(db, {1, 4}, error), qPrintable(error));
    QCOMPARE(value("SELECT count(*) FROM feeds").toInt(), 1);
    QCOMPARE(value("SELECT id FROM feeds").toInt(), 6);
    QCOMPARE(value("SELECT rowToParent FROM feeds WHERE id=6").toInt(), 0);
    QCOMPARE(value("SELECT count(*) FROM news").toInt(), 1);
    QCOMPARE(value("SELECT count(*) FROM news_ex").toInt(), 1);
  }

  void rollback_data()
  {
    QTest::addColumn<QString>("trigger");
    QTest::newRow("dependent delete") << QString("BEFORE DELETE ON news WHEN OLD.feedId=4");
    QTest::newRow("sibling ordering") << QString("BEFORE UPDATE OF rowToParent ON feeds WHEN OLD.id=5");
    QTest::newRow("folder recount") << QString("BEFORE UPDATE OF unread ON feeds WHEN OLD.id=1");
  }
  void rollback()
  {
    QFETCH(QString, trigger);
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TRIGGER injected_failure " + trigger +
                   " BEGIN SELECT RAISE(ABORT,'injected failure'); END"));
    const auto before = snapshot();
    QString error;
    QVERIFY(!FeedDeletion::apply(db, {3}, error));
    QVERIFY(error.contains("injected failure"));
    QCOMPARE(snapshot(), before);
    // The failed operation must have closed its transaction.
    QVERIFY(db.transaction());
    QVERIFY(db.rollback());
  }

  void missingSelection()
  {
    const auto before = snapshot();
    QString error;
    QVERIFY(!FeedDeletion::apply(db, {2, 999}, error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(snapshot(), before);
  }

  void beginFailureDoesNotRollbackCaller()
  {
    QVERIFY(db.transaction());
    QSqlQuery q(db);
    QVERIFY(q.exec("UPDATE feeds SET unread=99 WHERE id=6"));
    QString error;
    QVERIFY(!FeedDeletion::apply(db, {2}, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(db.rollback());
    QCOMPARE(value("SELECT unread FROM feeds WHERE id=6").toInt(), 4);
  }
};
QTEST_GUILESS_MAIN(FeedDeletionTest)
#include "test_feeddeletion.moc"

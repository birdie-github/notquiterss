// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedbulksettings.h"
#include <QtTest>
#include <QSqlQuery>
#include <QSqlError>

class FeedBulkSettingsTest : public QObject
{
  Q_OBJECT
  QSqlDatabase db;
  QVariant value(int id, const QString &column) {
    QSqlQuery q(db);
    if (!q.exec(QString("SELECT %1 FROM feeds WHERE id=%2").arg(column).arg(id)) || !q.next())
      return {};
    return q.value(0);
  }
private slots:
  void init() {
    db = QSqlDatabase::addDatabase("QSQLITE", "bulk-test");
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE feeds(id INTEGER PRIMARY KEY, parentId INTEGER, xmlUrl TEXT, "
                   "disableUpdate INTEGER DEFAULT 0, updateIntervalEnable INTEGER DEFAULT -1, "
                   "updateInterval INTEGER DEFAULT 10, updateIntervalType INTEGER DEFAULT 0, "
                   "displayEmbeddedImages INTEGER DEFAULT 1, layoutDirection INTEGER DEFAULT 0, "
                   "columns TEXT DEFAULT '', sort INTEGER DEFAULT 0, sortType INTEGER DEFAULT 0)"));
    QVERIFY(q.exec("INSERT INTO feeds(id,parentId,xmlUrl) VALUES "
                   "(1,0,''),(2,1,'https://a.test'),(3,1,''),(4,3,'https://b.test'),"
                   "(5,0,'https://outside.test'),(6,0,'')"));
  }
  void cleanup() {
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase("bulk-test");
  }
  void explicitTargetsAndScope() {
    const QList<int> ids{2, 4};
    QString error;
    FeedBulkSettings::Changes changes;
    changes.disabled = true;
    QVERIFY(FeedBulkSettings::apply(db, ids, changes, error));
    for (int id : {2, 4}) {
      QCOMPARE(value(id, "disableUpdate").toInt(), 1);
      QCOMPARE(value(id, "updateIntervalEnable").toInt(), -1);
      QCOMPARE(value(id, "displayEmbeddedImages").toInt(), 1);
    }
    for (int id : {1, 3, 5, 6}) QCOMPARE(value(id, "disableUpdate").toInt(), 0);
    changes.disabled = false;
    QVERIFY(FeedBulkSettings::apply(db, {2}, changes, error));
    QCOMPARE(value(2, "disableUpdate").toInt(), 0);
    QCOMPARE(value(4, "disableUpdate").toInt(), 1);
  }
  void explicitModesAndFalseValues() {
    QString error;
    FeedBulkSettings::Changes changes;
    changes.disabled = true;
    changes.schedule = FeedBulkSettings::Schedule{1, 10, 0};
    changes.images = 2;
    changes.rightToLeft = true;
    changes.columns = FeedBulkSettings::Columns{",1,2,", 2, 1};
    QVERIFY(FeedBulkSettings::apply(db, {2, 4}, changes, error));
    QCOMPARE(value(2, "updateIntervalEnable").toInt(), 1);
    QCOMPARE(value(4, "columns").toString(), QString(",1,2,"));
    changes = {};
    changes.disabled = false;
    changes.rightToLeft = false;
    changes.images = 0;
    QVERIFY(FeedBulkSettings::apply(db, {2}, changes, error));
    QCOMPARE(value(2, "disableUpdate").toInt(), 0);
    QCOMPARE(value(2, "layoutDirection").toInt(), 0);
    QCOMPARE(value(2, "displayEmbeddedImages").toInt(), 0);
    QCOMPARE(value(2, "updateIntervalEnable").toInt(), 1);
    changes = {};
    changes.schedule = FeedBulkSettings::Schedule{-1, 10, 0};
    QVERIFY(FeedBulkSettings::apply(db, {2}, changes, error));
    QCOMPARE(value(2, "updateIntervalEnable").toInt(), -1);
    changes.schedule->mode = 0;
    QVERIFY(FeedBulkSettings::apply(db, {2}, changes, error));
    QCOMPARE(value(2, "updateIntervalEnable").toInt(), 0);
  }
  void failureRollsBackAllFeeds() {
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TRIGGER reject_second BEFORE UPDATE ON feeds WHEN OLD.id=4 "
                   "BEGIN SELECT RAISE(ABORT,'injected failure'); END"));
    QString error;
    FeedBulkSettings::Changes changes;
    changes.disabled = true;
    QVERIFY(!FeedBulkSettings::apply(db, {2, 4}, changes, error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(value(2, "disableUpdate").toInt(), 0);
    QCOMPARE(value(4, "disableUpdate").toInt(), 0);
    QVERIFY(q.exec("DROP TRIGGER reject_second"));
    QVERIFY(FeedBulkSettings::apply(db, {2, 4}, changes, error));
  }
  void missingFeedOrFolderRollsBack() {
    QString error;
    FeedBulkSettings::Changes changes;
    changes.images = 2;
    QVERIFY(!FeedBulkSettings::apply(db, {2, 999}, changes, error));
    QCOMPARE(value(2, "displayEmbeddedImages").toInt(), 1);
    QVERIFY(!FeedBulkSettings::apply(db, {2, 3}, changes, error));
    QCOMPARE(value(2, "displayEmbeddedImages").toInt(), 1);
    QCOMPARE(value(3, "displayEmbeddedImages").toInt(), 1);
  }
};
QTEST_GUILESS_MAIN(FeedBulkSettingsTest)
#include "test_feedbulksettings.moc"

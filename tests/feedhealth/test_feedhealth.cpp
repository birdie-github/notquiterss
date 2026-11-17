#include <QtTest>
#include <QtSql>
#include "feeds/feedhealth.h"
class FeedHealthTest : public QObject
{
  Q_OBJECT
private slots:
  void automaticThresholdAndRecovery()
  {
    const auto now = QDateTime::fromString("2026-09-12T12:00:00Z", Qt::ISODate);
    QString status = FeedHealth::finish("0", "0", false, now);
    status = FeedHealth::finish(status, "-1 HTTP 502: Bad Gateway", false, now.addSecs(60));
    auto state = FeedHealth::read(status);
    QCOMPARE(state.failures, 1);
    QVERIFY(!state.warning);
    QCOMPARE(state.succeededAt, now);
    QVERIFY(!FeedHealth::tooltip("Feed", status, {}).isEmpty());
    QCOMPARE(FeedHealth::finish(status, "cancelled", false), status);
    status = FeedHealth::finish(status, "-6 Invalid XML", false, now.addSecs(120));
    QCOMPARE(FeedHealth::read(status).failures, 2);
    QVERIFY(FeedHealth::read(status).warning);
    status = FeedHealth::finish(status, "0", false, now.addSecs(180));
    QCOMPARE(FeedHealth::read(status).failures, 0);
    QVERIFY(!FeedHealth::read(status).warning);
    QVERIFY(FeedHealth::read(status).error.isEmpty());
    QCOMPARE(FeedHealth::read(status).succeededAt, now.addSecs(180));
  }
  void manualAndLegacyErrors()
  {
    const QString status = FeedHealth::finish("0", "-6 Expected '-' or 'DOCTYPE'", true);
    QVERIFY(FeedHealth::read(status).warning);
    QCOMPARE(FeedHealth::read(status).failures, 1);
    QVERIFY(FeedHealth::read("-1 Server unavailable").warning);
    QVERIFY(FeedHealth::read("Unsupported or invalid feed").warning);
    QVERIFY(FeedHealth::read("1 Update").error.isEmpty());
  }
  void safeTooltipAndSqlRoundTrip()
  {
    const QString status = FeedHealth::finish("0", "-6 Expected '<tag>' & \"quote\"", true);
    const QString tip = FeedHealth::tooltip("<b>Feed</b>", status, {});
    QVERIFY(tip.contains("&lt;b&gt;Feed&lt;/b&gt;"));
    QVERIFY(!tip.contains("<tag>"));
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", "health-test");
      db.setDatabaseName(":memory:");
      QVERIFY(db.open());
      QSqlQuery query(db);
      QVERIFY(query.exec("CREATE TABLE feeds(id INTEGER PRIMARY KEY,status INTEGER)"));
      QVERIFY(query.exec("INSERT INTO feeds VALUES(1,0)"));
      query.prepare("UPDATE feeds SET status=? WHERE id=?");
      query.addBindValue(status);
      query.addBindValue(1);
      QVERIFY(query.exec());
      QVERIFY(query.exec("SELECT status FROM feeds WHERE id=1"));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toString(), status);
      QCOMPARE(FeedHealth::read(query.value(0).toString()).error, FeedHealth::read(status).error);
    }
    QSqlDatabase::removeDatabase("health-test");
  }
};
QTEST_GUILESS_MAIN(FeedHealthTest)
#include "test_feedhealth.moc"

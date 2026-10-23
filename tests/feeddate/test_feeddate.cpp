// SPDX-License-Identifier: GPL-3.0-or-later
#include "feeddate.h"
#include "newsretention.h"
#include <QtTest>
#include <QTimeZone>

class FeedDateTest : public QObject
{
  Q_OBJECT
private slots:
  void normalize_data()
  {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("UTC") << "2026-10-23T12:00:00Z" << "2026-10-23T12:00:00";
    QTest::newRow("ISO half hour") << "2026-10-23T12:00:00+05:30" << "2026-10-23T06:30:00";
    QTest::newRow("ISO quarter hour") << "2026-10-23T12:00:00+05:45" << "2026-10-23T06:15:00";
    QTest::newRow("ISO negative") << "2026-10-23T12:00:00-03:30" << "2026-10-23T15:30:00";
    QTest::newRow("compact offset") << "2026-10-23T12:00:00+0545" << "2026-10-23T06:15:00";
    QTest::newRow("fractional seconds") << "2026-10-23T12:00:00.123+05:30" << "2026-10-23T06:30:00";
    QTest::newRow("comma fractional seconds") << "2026-10-23T12:00:00,123Z" << "2026-10-23T12:00:00";
    QTest::newRow("fractional minutes") << "2026-10-23T12:00.5Z" << "2026-10-23T12:00:30";
    QTest::newRow("ISO minutes") << "2026-10-23T12:00Z" << "2026-10-23T12:00:00";
    QTest::newRow("ISO end of day") << "2026-10-23T24:00Z" << "2026-10-24T00:00:00";
    QTest::newRow("year boundary") << "2026-01-01T00:15:00+05:45" << "2025-12-31T18:30:00";
    QTest::newRow("RFC half hour") << "Fri, 23 Oct 2026 12:00:00 +0530" << "2026-10-23T06:30:00";
    QTest::newRow("RFC colon offset") << "23 Oct 2026 12:00:00 +05:45" << "2026-10-23T06:15:00";
    QTest::newRow("RFC negative") << "23 Oct 2026 12:00:00 -0330" << "2026-10-23T15:30:00";
    QTest::newRow("RFC GMT") << "Fri, 23 Oct 2026 12:00:00 GMT" << "2026-10-23T12:00:00";
    QTest::newRow("RFC EDT") << "23 Oct 2026 12:00:00 EDT" << "2026-10-23T16:00:00";
    QTest::newRow("RFC EST") << "23 Oct 2026 12:00:00 EST" << "2026-10-23T17:00:00";
    QTest::newRow("RFC comment") << "23 Oct 2026 12:00:00 +0530 (IST)" << "2026-10-23T06:30:00";
    QTest::newRow("legacy space") << "2026-10-23 12:00:00 +05:45" << "2026-10-23T06:15:00";
    QTest::newRow("ISO-shaped GMT") << "2026-10-23 12:00:00 GMT" << "2026-10-23T12:00:00";
    QTest::newRow("ISO-shaped EDT") << "2026-10-23T12:00:00 EDT" << "2026-10-23T16:00:00";
    QTest::newRow("ISO-shaped lowercase zone") << "2026-10-23T12:00:00 est" << "2026-10-23T17:00:00";
    QTest::newRow("ISO-shaped fractional GMT") << "2026-10-23T12:00:00.123 GMT" << "2026-10-23T12:00:00";
    QTest::newRow("legacy short year") << "23 Oct 26 12:00:00 GMT" << "2026-10-23T12:00:00";
    QTest::newRow("legacy DST gap wall fields") << "08 Mar 26 02:30:00 +0530" << "2026-03-07T21:00:00";
    QTest::newRow("legacy date only") << "23 Oct 2026 GMT" << "2026-10-23T00:00:00";
    QTest::newRow("invalid offset minutes") << "2026-10-23T12:00:00+05:99" << QString();
    QTest::newRow("invalid offset hours") << "23 Oct 2026 12:00:00 +2500" << QString();
    QTest::newRow("unknown zone") << "23 Oct 2026 12:00:00 XYZ" << QString();
    QTest::newRow("ISO-shaped unknown zone") << "2026-10-23T12:00:00 XYZ" << QString();
    QTest::newRow("ISO-shaped trailing text") << "2026-10-23T12:00:00garbage" << QString();
    QTest::newRow("fractional trailing text") << "2026-10-23T12:00:00.123456garbage" << QString();
    QTest::newRow("text before UTC suffix") << "2026-10-23T12:00:00garbageZ" << QString();
    QTest::newRow("bad date") << "31 Feb 2026 12:00:00 GMT" << QString();
    QTest::newRow("empty") << QString() << QString();
  }
  void normalize()
  {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(FeedDate::normalize(input), expected);
  }

  void timezoneLessMeansLocal()
  {
    const QDateTime local(QDate(2026, 1, 15), QTime(12, 30), QTimeZone::systemTimeZone());
    const QString expected = local.toUTC().toString("yyyy-MM-ddTHH:mm:ss");
    QCOMPARE(FeedDate::normalize("2026-01-15T12:30:00"), expected);
    QCOMPARE(FeedDate::normalize("2026-01-15 12:30:00"), expected);
    QCOMPARE(FeedDate::normalize("15 Jan 2026 12:30:00"), expected);
    const QDateTime midnight(QDate(2026, 1, 15), QTime(0, 0), QTimeZone::systemTimeZone());
    QCOMPARE(FeedDate::normalize("2026-01-15"), midnight.toUTC().toString("yyyy-MM-ddTHH:mm:ss"));
  }

  void consistentArticleDates()
  {
    const QDateTime utc = QDateTime::fromString("2026-01-15T12:30:00Z", Qt::ISODate);
    QCOMPARE(FeedDate::articleLocalTime("2026-01-15T12:30:00", QString()).toUTC(), utc);
    QCOMPARE(FeedDate::articleLocalTime("2026-01-15T12:30:00Z", QString()).toUTC(), utc);
    QCOMPARE(FeedDate::articleLocalTime("2026-01-15T18:00:00+05:30", QString()).toUTC(), utc);
    QCOMPARE(FeedDate::articleLocalTime(QString(), "2026-01-15T18:00:00+05:30").toUTC(), utc);
    QCOMPARE(FeedDate::articleLocalTime("invalid", "2026-01-15T12:30:00Z").toUTC(), utc);
    const QDateTime receipt = QDateTime::fromString("2026-01-15T12:30:00", Qt::ISODate);
    QCOMPARE(FeedDate::articleLocalTime(QString(), "2026-01-15T12:30:00").toUTC(), receipt.toUTC());
    QVERIFY(!FeedDate::articleLocalTime(QString(), QString()).isValid());
  }

  void historicalDst()
  {
    const QTimeZone zone("America/New_York");
    if (!zone.isValid()) QSKIP("America/New_York timezone data unavailable");
    const auto winter = FeedDate::articleLocalTime("2026-01-15T12:00:00", QString()).toTimeZone(zone);
    const auto summer = FeedDate::articleLocalTime("2026-07-15T12:00:00", QString()).toTimeZone(zone);
    QCOMPARE(winter.time(), QTime(7, 0));
    QCOMPARE(summer.time(), QTime(8, 0));
    const auto beforeGap = FeedDate::articleLocalTime("2026-03-08T06:30:00", QString()).toTimeZone(zone);
    const auto afterGap = FeedDate::articleLocalTime("2026-03-08T07:30:00", QString()).toTimeZone(zone);
    QCOMPARE(beforeGap.time(), QTime(1, 30));
    QCOMPARE(afterGap.time(), QTime(3, 30));
    // These UTC wall-clock fields must remain valid even when a local zone
    // happens to skip 02:30 that day.
    const auto utcGapFields = NewsRetention::publicationDate("2026-03-08T02:30:00");
    QVERIFY(utcGapFields.isValid());
    QCOMPARE(utcGapFields.toUTC().time(), QTime(2, 30));
  }

  void publicationRetentionContract()
  {
    const QString published = FeedDate::normalize("2026-10-23T12:00:00+05:45");
    QCOMPARE(published.size(), 19);
    const QDateTime cutoff = QDateTime::fromString("2026-10-23T06:15:01Z", Qt::ISODate);
    QVERIFY(NewsRetention::expired(published, cutoff));
    // A legacy receipt-derived publication value stays ineligible for cleanup.
    QVERIFY(!NewsRetention::expired(published + 'Z', cutoff));
    QVERIFY(!NewsRetention::expired(QString(), cutoff));
  }
};
QTEST_GUILESS_MAIN(FeedDateTest)
#include "test_feeddate.moc"

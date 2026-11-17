#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "app/releaseinfo.h"

class ReleaseInfoTest : public QObject
{
  Q_OBJECT
private slots:
  void versions_data()
  {
    QTest::addColumn<QString>("installed");
    QTest::addColumn<QString>("offered");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<bool>("newer");
    QTest::newRow("old upstream") << QStringLiteral("0.90.2") << QStringLiteral("0.19.4") << true << false;
    QTest::newRow("numeric") << QStringLiteral("0.9.9") << QStringLiteral("v0.10.0") << true << true;
    QTest::newRow("same") << QStringLiteral("0.90.2") << QStringLiteral("0.90.2") << true << false;
    QTest::newRow("trailing zero") << QStringLiteral("1.2.0") << QStringLiteral("1.2") << true << false;
    QTest::newRow("zero") << QStringLiteral("0.0.0") << QStringLiteral("0.0.1") << true << true;
    QTest::newRow("suffix") << QStringLiteral("1.2.0") << QStringLiteral("1.3.0-beta") << false << false;
    QTest::newRow("overflow") << QStringLiteral("1.2.0") << QStringLiteral("999999999999.0.0") << false << false;
    QTest::newRow("invalid installed") << QStringLiteral("unknown") << QStringLiteral("1.3.0") << false << false;
  }
  void versions()
  {
    QFETCH(QString, installed);
    QFETCH(QString, offered);
    QFETCH(bool, valid);
    QFETCH(bool, newer);
    const QJsonObject object{{"tag_name", offered}, {"draft", false}, {"prerelease", false}, {"body", "Notes"}};
    const auto result = parseReleaseInfo(QJsonDocument(object).toJson(), installed);
    QCOMPARE(result.valid, valid);
    QCOMPARE(result.newer, newer);
    if (valid) QCOMPARE(result.notes, QString("Notes"));
  }
  void invalidResponses()
  {
    for (const QByteArray json : {QByteArray("not json"), QByteArray("[]"), QByteArray("{}"),
         QByteArray(R"({"message":"Not Found"})"),
         QByteArray(R"({"tag_name":"2.0.0","draft":true,"prerelease":false})"),
         QByteArray(R"({"tag_name":"2.0.0","draft":false,"prerelease":true})")}) {
      QVERIFY(!parseReleaseInfo(json, "1.0.0").valid);
    }
  }
};
QTEST_GUILESS_MAIN(ReleaseInfoTest)
#include "test_releaseinfo.moc"

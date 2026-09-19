#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "websiteoverrides.h"

class WebsiteOverridesTest : public QObject
{
  Q_OBJECT
  QTemporaryDir directory_;
  QString path() const { return directory_.filePath("overrides.ini"); }
  void write(const QByteArray &text)
  {
    QFile file(path());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(text), qint64(text.size()));
  }

private slots:
  void absentFile()
  {
    WebsiteOverrides rules;
    rules.load(directory_.filePath("absent.ini"));
    QVERIFY(!rules.loaded());
    QVERIFY(rules.error().isEmpty());
    QCOMPARE(rules.userAgent(QUrl("https://example.com"), "UI agent"), QString("UI agent"));
  }

  void hostMatchingAndLiveFallback()
  {
    write("[example.com]\nIncludeSubdomains=true\nUserAgent=parent\n"
          "[news.example.com]\nUserAgent=specific\n");
    WebsiteOverrides rules;
    rules.load(path());
    QVERIFY2(rules.loaded(), qPrintable(rules.error()));
    QCOMPARE(rules.userAgent(QUrl("https://EXAMPLE.COM.:8443/feed"), "default"), QString("parent"));
    QCOMPARE(rules.userAgent(QUrl("https://news.example.com/feed"), "default"), QString("specific"));
    QCOMPARE(rules.userAgent(QUrl("https://other.example.com/feed"), "default"), QString("parent"));
    QCOMPARE(rules.userAgent(QUrl("https://example.com.evil.test/feed"), "first"), QString("first"));
    QCOMPARE(rules.userAgent(QUrl("https://notexample.com/feed"), "second"), QString("second"));
    QCOMPARE(rules.userAgent(QUrl("https://unmatched.test/feed"), "new UI value"), QString("new UI value"));
    write("[example.com]\nUserAgent=exact\n");
    rules.load(path());
    QCOMPARE(rules.userAgent(QUrl("https://www.example.com/feed"), "default"), QString("default"));
  }

  void queryRequirements()
  {
    write("[example.com]\nIncludeSubdomains=true\nRequiredQueryParameters=user,feed\n"
          "MissingParametersMessage=Copy the complete URL.\n"
          "[public.example.com]\nRequiredQueryParameters=\n");
    WebsiteOverrides rules;
    rules.load(path());
    QVERIFY2(rules.loaded(), qPrintable(rules.error()));
    QVERIFY(rules.validationError(QUrl("https://example.com/feed?user=a&feed=token")).isEmpty());
    QVERIFY(rules.validationError(QUrl("https://example.com/feed?user=a&feed=a%26b")).isEmpty());
    for (const QString &query : {QString(), QString("?user=a&feed="),
         QString("?User=a&feed=token"), QString("?user=a&feed=token&feed="),
         QString("?user=a#feed=token")}) {
      const QString error = rules.validationError(QUrl("https://example.com/feed" + query));
      QVERIFY(!error.isEmpty());
      QVERIFY(error.contains("Copy the complete URL."));
    }
    QVERIFY(rules.validationError(QUrl("https://public.example.com/feed")).isEmpty());
    QVERIFY(rules.validationError(QUrl("https://elsewhere.test/feed")).isEmpty());
    QVERIFY(rules.validationError(QUrl("file:///tmp/feed.xml")).isEmpty());
  }

  void invalidFileDoesNotPartiallyApply()
  {
    for (const QByteArray &bad : {QByteArray("[*]\nUserAgent=wildcard\n"),
         QByteArray("[z.example]\nUnknownKey=value\n"),
         QByteArray("[z.example]\nIncludeSubdomains=maybe\n"),
         QByteArray("[z.example]\nUserAgent=\n")}) {
      write(QByteArray("[a.example]\nUserAgent=must-not-apply\n") + bad);
      WebsiteOverrides rules;
      rules.load(path());
      QVERIFY(!rules.loaded());
      QVERIFY(!rules.error().isEmpty());
      QCOMPARE(rules.userAgent(QUrl("https://a.example/feed"), "default"), QString("default"));
    }
  }
};

QTEST_GUILESS_MAIN(WebsiteOverridesTest)
#include "test_websiteoverrides.moc"

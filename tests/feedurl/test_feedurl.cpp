// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include "feedurl.h"
#include "networkpolicy.h"
#include "opmlinput.h"
#include "credentialscope.h"

class FeedUrlTest : public QObject
{
  Q_OBJECT
private slots:
  void credentialProtectionSpaces()
  {
    const QUrl url("https://example.org/feed");
    const QString key = CredentialScope::server(url, "private");
    QCOMPARE(key, CredentialScope::server(QUrl("https://EXAMPLE.org:443/other"), "private"));
    QVERIFY(key != CredentialScope::server(QUrl("http://example.org/feed"), "private"));
    QVERIFY(key != CredentialScope::server(QUrl("https://example.org:8443/feed"), "private"));
    QVERIFY(key != CredentialScope::server(url, "other"));
    QVERIFY(key != CredentialScope::server(url, "private#suffix"));
    QVERIFY(key != CredentialScope::server(url, "private%23suffix"));
    QVERIFY(key != QStringLiteral("example.org"));

    QNetworkProxy proxy(QNetworkProxy::HttpProxy, "example.org", 443);
    const QString proxyKey = CredentialScope::proxy(proxy, "private");
    QVERIFY(proxyKey != key);
    proxy.setPort(8443);
    QVERIFY(proxyKey != CredentialScope::proxy(proxy, "private"));
    proxy.setPort(443);
    proxy.setType(QNetworkProxy::Socks5Proxy);
    QVERIFY(proxyKey != CredentialScope::proxy(proxy, "private"));
    proxy.setType(QNetworkProxy::HttpProxy);
    QVERIFY(proxyKey != CredentialScope::proxy(proxy, "other"));
  }

  void explicitFeedCredentials()
  {
    const QUrl url("https://example.org/feed?account=one");
    const QString key = CredentialScope::feed(url);
    QCOMPARE(key, CredentialScope::feed(QUrl("https://example.org:443/feed?account=one#section")));
    QVERIFY(key != CredentialScope::feed(QUrl("http://example.org/feed?account=one")));
    QVERIFY(key != CredentialScope::feed(QUrl("https://example.org:8443/feed?account=one")));
    QVERIFY(key != CredentialScope::feed(QUrl("https://example.org/other?account=one")));
    QVERIFY(key != CredentialScope::feed(QUrl("https://example.org/feed?account=two")));
    QVERIFY(key != CredentialScope::server(url, QString()));
    QVERIFY(key != QStringLiteral("example.org"));
    QCOMPARE(CredentialScope::server(QUrl("https://[::1]/feed"), "r"),
             CredentialScope::server(QUrl("https://[::1]:443/other"), "r"));
  }

  void redirectCredentialScope()
  {
    const QUrl source("https://alice:secret@example.org/feed?account=one");
    QCOMPARE(CredentialScope::redirectTarget(source, QUrl("#section")).userInfo(), source.userInfo());
    for (const QUrl &target : {QUrl("/other"), QUrl("?account=two"),
                              QUrl("https://example.org:8443/feed?account=one"),
                              QUrl("https://other.org/feed"),
                              QUrl("https://mallory:injected@example.org/other")})
      QVERIFY(CredentialScope::redirectTarget(source, target).userInfo().isEmpty());
    QCOMPARE(CredentialScope::redirectTarget(source,
             QUrl("https://mallory:injected@example.org/feed?account=one")).userInfo(), source.userInfo());
  }

  void normalization_data()
  {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("bare") << "example.org/rss" << "https://example.org/rss";
    QTest::newRow("wrapper") << "feed://example.org/rss" << "https://example.org/rss";
    QTest::newRow("wrapped-http") << "feed:http://example.org/rss" << "http://example.org/rss";
    QTest::newRow("wrapped-https") << "FEED:https://example.org/rss" << "https://example.org/rss";
    QTest::newRow("query-not-scheme") << "feed:http://example.org/rss?next=https://other.org" << "http://example.org/rss?next=https://other.org";
    QTest::newRow("port") << "localhost:8080/rss" << "https://localhost:8080/rss";
    QTest::newRow("relative-scheme") << "//example.org/rss" << "https://example.org/rss";
    QTest::newRow("local") << "file:///tmp/feed.xml" << "file:///tmp/feed.xml";
    QTest::newRow("explicit-http") << "http://example.org/rss" << "http://example.org/rss";
  }
  void normalization()
  {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    const QUrl result = FeedUrl::normalize(input);
    QCOMPARE(result, QUrl(expected));
    QVERIFY(NetworkPolicy::isRequestUrl(result));
  }
  void upgradeAndIdentity()
  {
    QCOMPARE(FeedUrl::upgrade(QUrl("http://example.org:80/rss?a=%2F&b=2")),
             QUrl("https://example.org/rss?a=%2F&b=2"));
    QCOMPARE(FeedUrl::upgrade(QUrl("http://example.org:8080/rss")).port(), 8080);
    QCOMPARE(FeedUrl::identity(QUrl("http://example.org:80/rss")),
             FeedUrl::identity(QUrl("https://example.org:443/rss")));
    QVERIFY(FeedUrl::identity(QUrl("https://example.org/A")) !=
            FeedUrl::identity(QUrl("https://example.org/a")));
    QVERIFY(FeedUrl::identity(QUrl("https://example.org/rss?user=one")) !=
            FeedUrl::identity(QUrl("https://example.org/rss?user=two")));
    QVERIFY(!NetworkPolicy::isRequestUrl(FeedUrl::normalize("https://")));
    QVERIFY(!NetworkPolicy::isRequestUrl(FeedUrl::normalize("")));
    QVERIFY(!NetworkPolicy::isRequestUrl(FeedUrl::normalize("ftp://example.org/rss")));
  }
  void opmlPreflight()
  {
    int count = -1;
    QString error;
    const QByteArray data = OpmlInput::prepare(
        "<opml><body><outline text='Folder'><outline xmlUrl='feed:http://example.org/rss?a=1&b=2'/>"
        "<outline xmlUrl='https://example.net/rss'/><outline xmlUrl='feed://example.com/rss'/>"
        "</outline></body></opml>");
    QVERIFY(OpmlInput::inspect(data, count, error));
    QCOMPARE(count, 1);
    QVERIFY(!OpmlInput::inspect("<opml><body><outline", count, error));
    QVERIFY(!OpmlInput::inspect("<rss/>", count, error));
    QVERIFY(!OpmlInput::inspect("<opml><outline xmlUrl='javascript:alert(1)'/></opml>", count, error));
  }
};
QTEST_GUILESS_MAIN(FeedUrlTest)
#include "test_feedurl.moc"

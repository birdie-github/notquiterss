#include "cookiejar.h"
#include "logfile.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <thread>

// Store tests do not need the application's logging UI or configuration.
bool LogFile::consoleLoggingEnabled() { return false; }

class CookieTests : public QObject
{
  Q_OBJECT
  QByteArray record(const QByteArray &domain, bool subdomains, const QByteArray &name,
                    qint64 expiry, const QByteArray &path = "/", bool secure = true)
  {
    return domain + '\t' + (subdomains ? "TRUE" : "FALSE") + '\t' + path + '\t' +
        (secure ? "TRUE" : "FALSE") + '\t' + QByteArray::number(expiry) + '\t' +
        name + "\tsecret\n";
  }
  bool write(const QString &path, const QByteArray &bytes)
  {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
  }
private slots:
  void importAndRoundTrip()
  {
    QTemporaryDir dir;
    const auto expiry = QDateTime::currentSecsSinceEpoch() + 86400;
    const QString input = dir.filePath("import.txt");
    QVERIFY(write(input, "# Netscape HTTP Cookie File\n#HttpOnly_" +
        record("example.org", true, "clearance", expiry, "/feed") +
        record("www.example.org", false, "session", 0) +
        record("example.org", true, "old", 1)));
    CookieJar::Import data;
    QString error;
    QVERIFY2(CookieJar::readImport(input, &data, &error), qPrintable(error));
    QCOMPARE(data.cookies.size(), 2);
    QCOMPARE(data.expired, 1);
    const QString storePath = dir.filePath("cookies.txt");
    {
      CookieJar store(storePath);
      QVERIFY(store.importCookies(data, &error));
      QCOMPARE(store.enabledSites(), QStringList{QStringLiteral(".example.org")});
      QCOMPARE(store.cookiesForUrl(QUrl("https://www.example.org/feed/rss")).size(), 2);
      QCOMPARE(store.cookiesForUrl(QUrl("https://other.example.org/feed/rss")).size(), 1);
      QVERIFY(store.cookiesForUrl(QUrl("http://www.example.org/feed/rss")).isEmpty());
      QVERIFY(store.cookiesForUrl(QUrl("https://other.example.org/elsewhere")).isEmpty());
      QVERIFY(store.cookiesForUrl(QUrl("https://example.org.evil.test/feed/rss")).isEmpty());
    }
    CookieJar restored(storePath);
    const auto cookies = restored.cookiesForUrl(QUrl("https://www.example.org/feed/rss"));
    QCOMPARE(cookies.size(), 1);
    QVERIFY(cookies.first().isHttpOnly());
    QVERIFY(restored.removeSite(".example.org", &error));
    QVERIFY(restored.cookiesForUrl(QUrl("https://www.example.org/feed/rss")).isEmpty());
    CookieJar empty(storePath);
    QVERIFY(empty.enabledSites().isEmpty());
  }

  void hostOnlyAndServerCookies()
  {
    QTemporaryDir dir;
    const auto expiry = QDateTime::currentSecsSinceEpoch() + 86400;
    const auto path = dir.filePath("cookies.txt");
    const auto input = dir.filePath("import.txt");
    QVERIFY(write(input, record(".example.org", false, "imported", expiry)));
    CookieJar::Import data;
    QString error;
    QVERIFY(CookieJar::readImport(input, &data, &error));
    CookieJar store(path);
    QVERIFY(store.importCookies(data, &error));
    QVERIFY(store.cookiesForUrl(QUrl("https://sub.example.org/")).isEmpty());
    QNetworkCookie session("session", "value"); // No Domain attribute: exact host.
    QVERIFY(store.setCookiesFromUrl({session}, QUrl("https://example.org/")));
    QCOMPARE(store.cookiesForUrl(QUrl("https://example.org/")).size(), 2);
    session.setDomain(".example.org");
    QVERIFY(!store.setCookiesFromUrl({session}, QUrl("https://example.org/")));
    QVERIFY(!store.setCookiesFromUrl({session}, QUrl("https://unrelated.org/")));
    // Session-only changes must not cause a persistent write.
    QVERIFY(QFile::remove(path));
    QVERIFY(store.saveCookies());
    QVERIFY(!QFile::exists(path));
    QNetworkCookie persistent("new", "value");
    persistent.setExpirationDate(QDateTime::fromSecsSinceEpoch(expiry, Qt::UTC));
    QVERIFY(store.setCookiesFromUrl({persistent}, QUrl("https://example.org/")));
    QVERIFY(!QFile::exists(path)); // Network responses never write immediately.
    QVERIFY(store.saveCookies());
    QVERIFY(QFile::exists(path));
    QVERIFY(QFile::remove(path));
    QVERIFY(store.setCookiesFromUrl({persistent}, QUrl("https://example.org/")));
    QVERIFY(store.saveCookies());
    QVERIFY(!QFile::exists(path)); // Identical persistent cookies are not dirty.
  }

  void malformedAndFailedSave()
  {
    QTemporaryDir dir;
    const auto input = dir.filePath("bad.txt");
    QVERIFY(write(input, "not\ta\tcookie\n"));
    CookieJar::Import data;
    QString error;
    QVERIFY(!CookieJar::readImport(input, &data, &error));
    CookieJar broken(input);
    QVERIFY(broken.saveCookies()); // No truncation of unreadable input.
    QFile f(input);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("not\ta\tcookie\n"));
    f.close();
    QVERIFY(write(input, record("example.org", true, "one", QDateTime::currentSecsSinceEpoch()+86400)));
    QVERIFY(CookieJar::readImport(input, &data, &error));
    CookieJar failed(dir.filePath("missing/cookies.txt"));
    QVERIFY(!failed.importCookies(data, &error));
    QVERIFY(failed.enabledSites().isEmpty());
    QVERIFY(failed.cookiesForUrl(QUrl("https://example.org/")).isEmpty());
    QVERIFY(!broken.importCookies(data, &error));
  }

  void workerAdapters()
  {
    QTemporaryDir dir;
    const auto input = dir.filePath("import.txt");
    QVERIFY(write(input, record("example.org", true, "one", QDateTime::currentSecsSinceEpoch()+86400)));
    CookieJar::Import data;
    QString error;
    QVERIFY(CookieJar::readImport(input, &data, &error));
    CookieJar store(dir.filePath("cookies.txt"));
    QVERIFY(store.importCookies(data, &error));
    auto work = [&store]() {
      auto *jar = store.createNetworkJar(nullptr);
      for (int i = 0; i < 100; ++i) {
        jar->setCookiesFromUrl({QNetworkCookie("session", "value")}, QUrl("https://example.org/"));
        jar->cookiesForUrl(QUrl("https://example.org/"));
      }
      delete jar;
    };
    std::thread first(work), second(work);
    first.join();
    second.join();
    QCOMPARE(store.cookiesForUrl(QUrl("https://example.org/")).size(), 2);
    QVERIFY(store.saveCookies());
  }
};
QTEST_GUILESS_MAIN(CookieTests)
#include "test_cookies.moc"

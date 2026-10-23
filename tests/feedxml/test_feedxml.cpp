// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedxml.h"
#include <QtTest>

class FeedXmlTest : public QObject
{
  Q_OBJECT
private slots:
  void validDocumentIsNotTruncated()
  {
    const QByteArray input = "<rss><channel><item><description><![CDATA["
                             "Closing examples: </rss> </feed> </rdf:RDF>."
                             "]]></description></item>"
                             "<!-- An example </rss> in a comment -->"
                             "<item><title>Second article</title></item></channel></rss>"
                             "<!-- A valid trailing comment -->";
    QDomDocument doc;
    QString error;
    int line = 0, column = 0;
    QVERIFY2(FeedXml::parse(doc, input, error, line, column), qPrintable(error));
    QCOMPARE(doc.elementsByTagName("item").count(), 2);
    QCOMPARE(doc.elementsByTagName("description").at(0).toElement().text(),
             QString("Closing examples: </rss> </feed> </rdf:RDF>."));
    // A successful full parse must retain legal material after the root too.
    QVERIFY(doc.lastChild().isComment());
    QCOMPARE(doc.lastChild().nodeValue(), QString(" A valid trailing comment "));
  }

  void trailingGarbage_data()
  {
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QString>("root");
    QTest::newRow("RSS CDATA") << QByteArray(
        "<rss><channel><item><description><![CDATA[Example </rss>]]></description></item>"
        "<item><title>Second</title></item></channel></rss>server garbage") << QString("rss");
    QTest::newRow("Atom comment") << QByteArray(
        "<feed xmlns='http://www.w3.org/2005/Atom'><!-- example </feed> -->"
        "<entry><title>One</title></entry><entry><title>Two</title></entry></feed><html>footer</html>")
        << QString("feed");
    QTest::newRow("RDF prefixes") << QByteArray(
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
        "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
        "<item rdf:about='https://example.org/'><dc:creator>Author</dc:creator></item>"
        "</rdf:RDF>junk") << QString("rdf:RDF");
    QTest::newRow("spaced closing tag") << QByteArray("<rss><channel/></rss >junk") << QString("rss");
    QTest::newRow("self closing root") << QByteArray("<feed/>junk") << QString("feed");
  }
  void trailingGarbage()
  {
    QFETCH(QByteArray, input);
    QFETCH(QString, root);
    QDomDocument doc;
    QString error;
    int line = 0, column = 0;
    QVERIFY2(FeedXml::parse(doc, input, error, line, column), qPrintable(error));
    QCOMPARE(doc.documentElement().tagName(), root);
    QVERIFY(error.isEmpty());
    if (root == "rss" && input.contains("CDATA")) {
      QCOMPARE(doc.elementsByTagName("item").count(), 2);
      QCOMPARE(doc.elementsByTagName("description").at(0).toElement().text(), QString("Example </rss>"));
    } else if (root == "feed" && input.contains("entry")) {
      QCOMPARE(doc.elementsByTagName("entry").count(), 2);
      QCOMPARE(doc.documentElement().attribute("xmlns"), QString("http://www.w3.org/2005/Atom"));
    } else if (root == "rdf:RDF") {
      QCOMPARE(doc.elementsByTagName("dc:creator").at(0).toElement().text(), QString("Author"));
      QCOMPARE(doc.elementsByTagName("item").at(0).toElement().attribute("rdf:about"),
               QString("https://example.org/"));
    }
  }

  void encodingAndDecodedInput()
  {
    const QString xml = QString::fromUtf8("<?xml version='1.0' encoding='UTF-16'?>"
                                         "<feed><entry><title>Пример 😀</title></entry></feed>junk");
    QByteArray utf16("\xff\xfe", 2);
    for (QChar character : xml) {
      utf16.append(char(character.unicode() & 255));
      utf16.append(char(character.unicode() >> 8));
    }
    QDomDocument bytesDoc, textDoc;
    QString error;
    int line = 0, column = 0;
    QVERIFY2(FeedXml::parse(bytesDoc, utf16, error, line, column), qPrintable(error));
    QVERIFY2(FeedXml::parse(textDoc, xml, error, line, column), qPrintable(error));
    const QString expected = QString::fromUtf8("Пример 😀");
    QCOMPARE(bytesDoc.elementsByTagName("title").at(0).toElement().text(), expected);
    QCOMPARE(textDoc.elementsByTagName("title").at(0).toElement().text(), expected);
  }

  void malformedRootIsNotRecovered_data()
  {
    QTest::addColumn<QByteArray>("input");
    QTest::newRow("mismatched") << QByteArray("<rss><channel></rss>junk");
    QTest::newRow("unfinished CDATA") << QByteArray("<rss><![CDATA[</rss>junk");
    QTest::newRow("unfinished comment") << QByteArray("<feed><!-- </feed>junk");
    QTest::newRow("unfinished root") << QByteArray("<rss><channel/>");
    QTest::newRow("other root") << QByteArray("<html><rss/></html>junk");
  }
  void malformedRootIsNotRecovered()
  {
    QFETCH(QByteArray, input);
    QDomDocument doc;
    QString error;
    int line = 0, column = 0;
    QVERIFY(!FeedXml::parse(doc, input, error, line, column));
    QVERIFY(!error.isEmpty());
    QVERIFY(line > 0);
  }
};
QTEST_GUILESS_MAIN(FeedXmlTest)
#include "test_feedxml.moc"

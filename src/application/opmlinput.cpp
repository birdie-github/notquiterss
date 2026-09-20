// SPDX-License-Identifier: GPL-3.0-or-later
#include "opmlinput.h"
#include "../network/feedurl.h"
#include "../network/networkpolicy.h"
#include <QRegularExpression>
#include <QTextCodec>
#include <QStringList>
#include <QXmlStreamReader>
#include <QObject>

QByteArray OpmlInput::prepare(QByteArray xmlData)
{
  QString convertData;
  bool codecOk = false;

  QRegularExpression rx("&(?!([a-z0-9#]+;))",
      QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
  int pos = 0;
  // Latin-1 keeps match offsets aligned with the QByteArray being edited.
  while ((pos = rx.match(QString::fromLatin1(xmlData), pos).capturedStart()) != -1) {
    xmlData.replace(pos, 1, "&amp;");
    pos += 1;
  }

  rx.setPattern("encoding=\"([^\"]+)");
  pos = rx.match(QString::fromUtf8(xmlData)).capturedStart();
  if (pos == -1) {
    rx.setPattern("encoding='([^']+)");
    pos = rx.match(QString::fromUtf8(xmlData)).capturedStart();
  }
  if (pos == -1) {
    QStringList codecNameList;
    codecNameList << "UTF-8" << "Windows-1251" << "KOI8-R" << "KOI8-U"
                  << "ISO 8859-5" << "IBM 866";
    foreach (QString codecNameT, codecNameList) {
      QTextCodec *codec = QTextCodec::codecForName(codecNameT.toUtf8());
      if (codec && codec->canEncode(xmlData)) {
        convertData = codec->toUnicode(xmlData);
        codecOk = true;
        break;
      }
    }
  }
  if (codecOk) {
    return convertData.toUtf8();
  } else {
    return xmlData;
  }

}

bool OpmlInput::inspect(const QByteArray &data, int &httpCount, QString &error)
{
  httpCount = 0;
  QXmlStreamReader xml(data);
  bool rootSeen = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) continue;
    if (!rootSeen) {
      rootSeen = true;
      if (xml.name() != QLatin1String("opml")) {
        error = QObject::tr("The file is not an OPML document.");
        return false;
      }
    }
    if (xml.name() != QLatin1String("outline")) continue;
    const QString value = xml.attributes().value("xmlUrl").toString();
    if (value.isEmpty()) continue;
    const QUrl url = FeedUrl::normalize(value);
    if (!NetworkPolicy::isRequestUrl(url)) {
      error = QObject::tr("The OPML file contains an invalid feed URL: %1").arg(value);
      return false;
    }
    if (url.scheme() == "http") ++httpCount;
  }
  if (xml.hasError() || !rootSeen) {
    error = QObject::tr("Invalid OPML at line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
    return false;
  }
  return true;
}

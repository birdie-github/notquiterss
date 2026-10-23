// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedxml.h"

#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
template<typename Input>
bool parseComplete(QDomDocument &doc, const Input &input, QString &error, int &line, int &column)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const auto result = doc.setContent(input, QDomDocument::ParseOption::Default);
  if (!result) {
    error = result.errorMessage;
    line = int(result.errorLine);
    column = int(result.errorColumn);
    return false;
  }
#else
  if (!doc.setContent(input, false, &error, &line, &column)) return false;
#endif
  error.clear();
  line = column = 0;
  return true;
}

template<typename Input>
bool parseFeed(QDomDocument &doc, const Input &input, QString &error, int &line, int &column)
{
  if (parseComplete(doc, input, error, line, column)) return true;

  QXmlStreamReader reader(input);
  // Match the existing DOM mode, including qualified RSS extension names.
  reader.setNamespaceProcessing(false);
  QString prefix;
  QXmlStreamWriter writer(&prefix);
  int depth = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.hasError()) return false;
    // Re-serialize tokens instead of using byte offsets into encoded input.
    // CDATA, comments and multibyte encodings retain their XML meaning.
    if (reader.isStartElement()) {
      if (depth == 0) {
        const QString root = reader.qualifiedName().toString();
        if (root != QLatin1String("rss") && root != QLatin1String("feed") &&
            root != QLatin1String("rdf:RDF")) return false;
      }
      ++depth;
      writer.writeStartElement(reader.qualifiedName().toString());
      for (const auto &attribute : reader.attributes())
        writer.writeAttribute(attribute.qualifiedName().toString(), attribute.value().toString());
    } else {
      writer.writeCurrentToken(reader);
    }
    if (reader.isEndElement() && --depth == 0) {
      if (writer.hasError()) return false;
      QDomDocument recovered;
      QString recoveryError;
      int recoveryLine = 0;
      int recoveryColumn = 0;
      if (!parseComplete(recovered, prefix, recoveryError, recoveryLine, recoveryColumn))
        return false;
      doc = recovered;
      error.clear();
      line = column = 0;
      return true;
    }
  }
  // Preserve the original DOM error if there was no complete, valid feed root.
  return false;
}
}

bool FeedXml::parse(QDomDocument &doc, const QByteArray &input, QString &error, int &line, int &column)
{
  return parseFeed(doc, input, error, line, column);
}

bool FeedXml::parse(QDomDocument &doc, const QString &input, QString &error, int &line, int &column)
{
  return parseFeed(doc, input, error, line, column);
}

QString FeedXml::atomText(const QDomElement &element)
{
  if (element.isNull()) return QString();
  const QString type = element.attribute(QStringLiteral("type"), QStringLiteral("text"))
                           .section(QLatin1Char(';'), 0, 0).trimmed().toLower();
  if (type == QLatin1String("html") || type == QLatin1String("text/html"))
    return element.text();
  if (type == QLatin1String("xhtml") || type == QLatin1String("application/xhtml+xml")) {
    QString markup;
    QTextStream output(&markup);
    for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling())
      child.save(output, 0);
    output.flush();
    return markup;
  }
  // XML decoding has already happened. Escape once before the HTML stage so
  // literal tags/entities cannot become formatting, links or image requests.
  QString text = element.text().toHtmlEscaped();
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  text.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
  return text;
}

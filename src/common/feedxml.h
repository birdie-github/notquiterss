// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDXML_H
#define FEEDXML_H

#include <QByteArray>
#include <QDomDocument>
#include <QString>

namespace FeedXml {
// Parse the complete input first. Only a complete RSS/Atom root may be recovered
// when trailing material makes the full document invalid.
bool parse(QDomDocument &doc, const QByteArray &input, QString &error, int &line, int &column);
bool parse(QDomDocument &doc, const QString &input, QString &error, int &line, int &column);
}

#endif // FEEDXML_H

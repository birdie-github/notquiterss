#include "releaseinfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QStringList>

namespace {
QVersionNumber numericVersion(const QString &text)
{
  static const QRegularExpression pattern(QStringLiteral("^[vV]?([0-9]+(?:\\.[0-9]+){1,3})$"));
  const auto match = pattern.match(text);
  if (!match.hasMatch()) return QVersionNumber();
  const QString number = match.captured(1);
  const QStringList components = number.split(QLatin1Char('.'));
  for (const QString &component : components) {
    bool ok = false;
    const int segment = component.toInt(&ok);
    if (!ok || segment < 0) return QVersionNumber();
  }
  return QVersionNumber::fromString(number);
}
}

ReleaseInfo parseReleaseInfo(const QByteArray &json, const QString &installedVersion)
{
  ReleaseInfo result;
  const QJsonDocument document = QJsonDocument::fromJson(json);
  if (!document.isObject()) return result;
  const QJsonObject object = document.object();
  if (!object.value("draft").isBool() || object.value("draft").toBool() ||
      !object.value("prerelease").isBool() || object.value("prerelease").toBool() ||
      !object.value("tag_name").isString()) return result;
  const QVersionNumber installed = numericVersion(installedVersion);
  const QVersionNumber offered = numericVersion(object.value("tag_name").toString());
  if (installed.isNull() || offered.isNull()) return result;
  result.valid = true;
  result.newer = QVersionNumber::compare(offered.normalized(), installed.normalized()) > 0;
  result.version = offered.toString();
  result.notes = object.value("body").toString();
  return result;
}

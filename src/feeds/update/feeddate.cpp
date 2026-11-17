// SPDX-License-Identifier: GPL-3.0-or-later
#include "feeddate.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

namespace {
QString stored(const QDateTime &date)
{
  return date.isValid() ? date.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) : QString();
}

QDateTime storedDate(QString value)
{
  // Interpret UTC wall-clock fields as UTC before any local-time conversion,
  // including during a local DST gap. Suffixed dates already specify an instant.
  if (value.size() == 19) value += QLatin1Char('Z');
  return QDateTime::fromString(value, Qt::ISODate);
}
}

QString FeedDate::normalize(const QString &input)
{
  QString body = input.simplified();
  if (body.isEmpty()) return QString();
  // Qt 5 accepts trailing text after ISO seconds, silently treating e.g. GMT
  // as an absent zone. Validate the complete syntax before taking this path;
  // named zones and legacy formats must reach the explicit handling below.
  static const QRegularExpression isoSyntax(QStringLiteral(
      "\\A[0-9]{4}-[0-9]{2}-[0-9]{2}(?:[Tt ][0-9]{2}:[0-9]{2}"
      "(?::[0-9]{2})?(?:[.,][0-9]+)?(?:[Zz]|[+-][0-9]{2}(?::?[0-9]{2})?)?)?\\z"));
  if (isoSyntax.match(body).hasMatch()) {
    const QDateTime iso = QDateTime::fromString(body, Qt::ISODate);
    if (iso.isValid()) return stored(iso);
  }

  // Retain the existing weekday-prefix and localized-month fallbacks.
  const int comma = body.indexOf(QLatin1Char(','));
  if (comma >= 0) body = body.mid(comma + 1).trimmed();
  // A trailing RFC date comment does not change the numeric/known zone before it.
  static const QRegularExpression comment(QStringLiteral("\\s*\\([^()]*\\)$"));
  body.remove(comment);
  static const QRegularExpression zone(QStringLiteral(
      "\\s*(Z|UT|UTC|GMT|EST|EDT|CST|CDT|MST|MDT|PST|PDT|[+-][0-9]{2}(?::?[0-9]{2})?)$"),
      QRegularExpression::CaseInsensitiveOption);
  const auto zoneMatch = zone.match(body);
  const bool explicitZone = zoneMatch.hasMatch();
  int offset = 0;
  if (explicitZone) {
    const QString suffix = zoneMatch.captured(1).toUpper();
    body.truncate(zoneMatch.capturedStart());
    body = body.trimmed();
    if (suffix.startsWith(QLatin1Char('+')) || suffix.startsWith(QLatin1Char('-'))) {
      QString digits = suffix.mid(1);
      digits.remove(QLatin1Char(':'));
      const int hours = digits.left(2).toInt();
      const int minutes = digits.mid(2).toInt();
      if (hours > 23 || minutes > 59) return QString();
      offset = (hours * 60 + minutes) * 60 * (suffix.at(0) == QLatin1Char('-') ? -1 : 1);
    } else {
      // Fixed offsets for RFC 822's obsolete US zone names, not the machine's
      // current timezone or its current daylight-saving offset.
      static const QHash<QString, int> offsets{{"EST", -5}, {"EDT", -4}, {"CST", -6}, {"CDT", -5},
                                              {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7}};
      offset = offsets.value(suffix, 0) * 3600;
    }
  }

  // Guard Qt's permissive RFC parser: some Qt versions accept unknown trailing
  // text as an absent zone. Supply only a validated, complete numeric suffix.
  static const QRegularExpression rfc(QStringLiteral(
      "^[0-9]{1,2} [A-Za-z]{3} [0-9]{4} [0-9]{2}:[0-9]{2}(?::[0-9]{2})?$"));
  if (explicitZone && rfc.match(body).hasMatch()) {
    const int absoluteMinutes = qAbs(offset) / 60;
    const QString numericZone = QStringLiteral(" %1%2%3")
        .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(absoluteMinutes / 60, 2, 10, QLatin1Char('0'))
        .arg(absoluteMinutes % 60, 2, 10, QLatin1Char('0'));
    const QDateTime parsed = QDateTime::fromString(body + numericZone, Qt::RFC2822Date);
    if (parsed.isValid()) return stored(parsed);
  }

  static const QRegularExpression parts(QStringLiteral(
      "^(.*?)(?:[Tt ]([0-9]{1,2}:[0-9]{2}(?::[0-9]{2}(?:\\.[0-9]+)?)?))?$"));
  const auto match = parts.match(body);
  if (!match.hasMatch()) return QString();
  QString dateText = match.captured(1);
  const QString clock = match.captured(2);
  const QTime time = clock.isEmpty() ? QTime(0, 0) : QTime::fromString(clock, Qt::ISODate);
  if (!time.isValid()) return QString();
  // Preserve the former two-digit-year pivot in the legacy month-name fallback.
  static const QRegularExpression shortYear(QStringLiteral("^([0-9]{1,2}\\s+\\S+\\s+)([0-9]{2})$"));
  const auto yearMatch = shortYear.match(dateText);
  if (yearMatch.hasMatch()) {
    const int year = yearMatch.captured(2).toInt();
    dateText = yearMatch.captured(1) + QString::number((year > 70 ? 1900 : 2000) + year);
  }
  for (const QLocale &locale : {QLocale::c(), QLocale::system()}) {
    for (const QString &format : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("d MMM yyyy"),
                                  QStringLiteral("dd MMM yyyy")}) {
      const QDate date = locale.toDate(dateText, format);
      if (!date.isValid()) continue;
      if (explicitZone) {
        // Construct wall-clock fields in UTC first. Using local time here could
        // invalidate or adjust a valid publisher date during a local DST gap.
        return stored(QDateTime(date, time, QTimeZone(0)).addSecs(-offset));
      }
      return stored(QDateTime(date, time, QTimeZone::systemTimeZone()));
    }
  }
  return QString();
}

QDateTime FeedDate::articleLocalTime(const QString &published, const QString &received)
{
  const QDateTime date = storedDate(published);
  // Receipt timestamps come from currentDateTime().toString(ISODate): a value
  // without a suffix is local time, unlike normalized publisher timestamps.
  return (date.isValid() ? date : QDateTime::fromString(received, Qt::ISODate)).toLocalTime();
}

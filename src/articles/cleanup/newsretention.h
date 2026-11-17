#ifndef NEWSRETENTION_H
#define NEWSRETENTION_H

#include <QTimeZone>
#include <QDateTime>
#include <QString>

namespace NewsRetention {
// parseDate() normalizes publisher dates to this UTC format. Older versions
// stored a synthetic receipt timestamp with a trailing Z when no date existed.
// Do not mistake those synthetic dates for publisher dates during cleanup.
inline QDateTime publicationDate(const QString &value)
{
  if (value.size() != 19 || value.at(10) != QLatin1Char('T')) return QDateTime();
  // Parse in UTC from the outset; constructing a local date first can reject
  // valid UTC wall-clock fields during the machine's spring DST gap.
  return QDateTime::fromString(value + QLatin1Char('Z'), Qt::ISODate);
}

inline QDateTime cutoff(int days)
{
  return QDateTime::currentDateTimeUtc().addDays(-qMax(0, days));
}

inline bool expired(const QString &published, const QDateTime &limit)
{
  const QDateTime date = publicationDate(published);
  return limit.isValid() && date.isValid() && date < limit;
}
}

#endif // NEWSRETENTION_H

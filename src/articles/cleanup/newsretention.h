#ifndef NEWSRETENTION_H
#define NEWSRETENTION_H

#include <QTimeZone>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace NewsRetention {
struct Policy
{
  bool dayCleanUpOn = true;
  int maxDayCleanUp = 30;
  bool newsCleanUpOn = true;
  int maxNewsCleanUp = 200;
  bool readCleanUp = false;
  bool neverUnreadCleanUp = true;
  bool neverStarCleanUp = true;
  bool neverLabelCleanUp = true;
};

enum class Disposition
{
  Keep,
  Remove,
  RemoveIfRead,
};

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

inline bool isEligible(const Policy &policy, int read, int starred, const QString &label)
{
  if (policy.neverUnreadCleanUp && read == 0) return false;
  if (policy.neverStarCleanUp && starred != 0) return false;
  if (policy.neverLabelCleanUp && !label.isEmpty() && label != QLatin1String(",")) return false;
  return true;
}

inline QString eligibilitySql(const Policy &policy)
{
  QStringList clauses;
  if (policy.neverUnreadCleanUp) clauses.append(QStringLiteral("read!=0"));
  if (policy.neverStarCleanUp) clauses.append(QStringLiteral("starred==0"));
  if (policy.neverLabelCleanUp) {
    clauses.append(QStringLiteral("(label=='' OR label==',' OR label IS NULL)"));
  }
  return clauses.join(QLatin1String(" AND "));
}

inline Disposition evaluateArticle(const Policy &policy, bool isShutdown,
                                   int countAllNews, int &countDelNews,
                                   const QString &received, const QString &published,
                                   const QDateTime &now)
{
  if (policy.newsCleanUpOn && (countDelNews < (countAllNews - policy.maxNewsCleanUp))) {
    ++countDelNews;
    return Disposition::Remove;
  }

  const QDateTime dateTime = QDateTime::fromString(received, Qt::ISODate);
  // Dated entries were handled above using publication age. Undated
  // entries retain receipt-age cleanup and their duplicate identities.
  const bool useReceiptAge = !isShutdown || !publicationDate(published).isValid();
  if (policy.dayCleanUpOn && useReceiptAge &&
      (dateTime.daysTo(now) > policy.maxDayCleanUp)) {
    ++countDelNews;
    return Disposition::Remove;
  }

  if (policy.readCleanUp) {
    ++countDelNews;
    return Disposition::RemoveIfRead;
  }

  return Disposition::Keep;
}

inline QString clearArticleSql()
{
  return QStringLiteral(
      "UPDATE news SET description='', content='', received='', "
      "author_name='', author_uri='', author_email='', "
      "category='', new='', read='', starred='', label='', "
      "deleteDate='', feedParentId='', deleted=2");
}

inline QString purgeDeletedSql()
{
  return clearArticleSql() + QStringLiteral(" WHERE deleted==1");
}
}

#endif // NEWSRETENTION_H

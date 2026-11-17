// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedhealth.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <limits>
namespace FeedHealth {
State read(const QString &status)
{
  State state;
  const auto json = QJsonDocument::fromJson(status.section(' ', 1).toUtf8()).object();
  if (json.value("feedHealth").toInt() == 1) {
    state.failures = qMax(0, json.value("failures").toInt());
    state.warning = json.value("warning").toBool();
    state.error = json.value("error").toString();
    state.failedAt = QDateTime::fromString(json.value("failedAt").toString(), Qt::ISODate);
    state.succeededAt = QDateTime::fromString(json.value("succeededAt").toString(), Qt::ISODate);
  } else if (status.section(' ', 0, 0).toInt() < 0 ||
             (!status.isEmpty() && status != "0" && status != "1" && !status.startsWith("1 "))) {
    state.failures = 1;
    state.warning = true;
    state.error = status.section(' ', 0, 0).toInt() < 0 ? status.section(' ', 1) : status;
  }
  return state;
}
QString finish(const QString &previous, const QString &result, bool manual, const QDateTime &now)
{
  if (result == "cancelled") return previous;
  State state = read(previous);
  if (result == "0") {
    state.failures = 0;
    state.warning = false;
    state.error.clear();
    state.failedAt = QDateTime();
    state.succeededAt = now;
  } else {
    if (state.failures < std::numeric_limits<int>::max()) ++state.failures;
    state.warning = state.warning || manual || state.failures >= 2;
    state.error = result.section(' ', 0, 0).toInt() < 0 ? result.section(' ', 1) : result;
    state.failedAt = now;
  }
  QJsonObject json{{"feedHealth",1},{"failures",state.failures},{"warning",state.warning},
    {"error",state.error},{"failedAt",state.failedAt.toString(Qt::ISODate)},
    {"succeededAt",state.succeededAt.toString(Qt::ISODate)}};
  // Keep the old status prefix protocol; presentation uses WarningRole instead
  // of embedding a small error bitmap into the favicon.
  return "0 " + QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
QString tooltip(const QString &title, const QString &status, const QDateTime &fallbackSuccess)
{
  const auto state = read(status);
  if (state.error.isEmpty()) return {};
  auto tr = [](const char *s) { return QCoreApplication::translate("FeedHealth", s); };
  auto date = [&](const QDateTime &d) {
    return d.isValid() ? QLocale().toString(d.toLocalTime(), QLocale::ShortFormat) : tr("Unknown");
  };
  const QString text = title + "\n\n" + tr("Last refresh failed: %1").arg(state.error) +
    "\n" + tr("Consecutive failed refreshes: %1").arg(state.failures) +
    "\n" + tr("Last failure: %1").arg(date(state.failedAt)) +
    "\n" + tr("Last successful retrieval: %1").arg(date(state.succeededAt.isValid() ? state.succeededAt : fallbackSuccess));
  return "<qt>" + text.toHtmlEscaped().replace("\n", "<br>") + "</qt>";
}
}

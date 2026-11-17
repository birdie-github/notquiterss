// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDHEALTH_H
#define FEEDHEALTH_H
#include <QString>
#include <QDateTime>
namespace FeedHealth {
constexpr int WarningRole = Qt::UserRole + 77;
constexpr int DisabledRole = Qt::UserRole + 78;
struct State {
  int failures = 0;
  bool warning = false;
  QString error;
  QDateTime failedAt, succeededAt;
};
State read(const QString &status);
QString finish(const QString &previous, const QString &result, bool manual,
               const QDateTime &now = QDateTime::currentDateTimeUtc());
QString tooltip(const QString &title, const QString &status, const QDateTime &fallbackSuccess);
}
#endif

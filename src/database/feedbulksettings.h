// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDBULKSETTINGS_H
#define FEEDBULKSETTINGS_H

#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>
#include <optional>

namespace FeedBulkSettings {
struct Schedule {
  int mode; // -1: global, 0: no scheduled updates, 1: custom
  int interval;
  int unit; // -1: seconds, 0: minutes, 1: hours
};
struct Columns {
  QString columns;
  int sortBy;
  int sortOrder;
};
struct Changes {
  std::optional<bool> disabled;
  std::optional<Schedule> schedule;
  std::optional<int> images; // Qt check state: 0 off, 1 global, 2 on
  std::optional<bool> rightToLeft;
  std::optional<Columns> columns;

  QMap<QString, QVariant> values() const;
};

// A single transaction; callers update models/timers only after success.
bool apply(QSqlDatabase db, const QList<int> &ids, const Changes &changes, QString &error);
}
#endif

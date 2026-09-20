// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedbulksettings.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace FeedBulkSettings {
QMap<QString, QVariant> Changes::values() const
{
  QMap<QString, QVariant> result;
  if (disabled) result.insert("disableUpdate", *disabled ? 1 : 0);
  if (schedule) {
    result.insert("updateIntervalEnable", schedule->mode);
    result.insert("updateInterval", schedule->interval);
    result.insert("updateIntervalType", schedule->unit);
  }
  if (images) result.insert("displayEmbeddedImages", *images);
  if (rightToLeft) result.insert("layoutDirection", *rightToLeft ? 1 : 0);
  if (columns) {
    result.insert("columns", columns->columns);
    result.insert("sort", columns->sortBy);
    result.insert("sortType", columns->sortOrder);
  }
  return result;
}

bool apply(QSqlDatabase db, const QList<int> &ids, const Changes &changes, QString &error)
{
  error.clear();
  const auto values = changes.values();
  if (ids.isEmpty() || values.isEmpty()) return true;
  if (!db.transaction()) {
    error = db.lastError().text();
    return false;
  }
  QSqlQuery q(db);
  auto rollback = [&]() {
    q.finish();
    if (!db.rollback()) error += "\n" + db.lastError().text();
    return false;
  };
  QStringList assignments;
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    assignments.append(it.key() + " = ?"); // Keys are fixed above, never user input.
  if (!q.prepare("UPDATE feeds SET " + assignments.join(", ") +
                 " WHERE id = ? AND xmlUrl != ''")) {
    error = q.lastError().text();
    return rollback();
  }
  for (int id : ids) {
    int position = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      q.bindValue(position++, it.value());
    q.bindValue(position, id);
    if (!q.exec()) {
      error = q.lastError().text();
      return rollback();
    }
    if (q.numRowsAffected() != 1) {
      error = QCoreApplication::translate("FeedBulkSettings", "A selected feed no longer exists. No changes were applied.");
      return rollback();
    }
    q.finish();
  }
  if (!db.commit()) {
    error = db.lastError().text();
    return rollback();
  }
  return true;
}
}

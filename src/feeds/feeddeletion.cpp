// SPDX-License-Identifier: GPL-3.0-or-later
#include "feeddeletion.h"

#include <QCoreApplication>
#include <QQueue>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <utility>

namespace {
QString tr(const char *text)
{
  return QCoreApplication::translate("FeedDeletion", text);
}
}

bool FeedDeletion::apply(QSqlDatabase db, const QList<int> &ids, QString &error)
{
  error.clear();
  if (ids.isEmpty()) return true;
  if (!db.transaction()) {
    error = db.lastError().text();
    return false;
  }
  QSqlQuery q(db);
  auto rollback = [&]() {
    q.finish();
    if (!db.rollback())
      error += '\n' + tr("Rollback failed: %1").arg(db.lastError().text());
    return false;
  };
  auto execute = [&](const QString &sql, const QVariantList &values) {
    if (!q.prepare(sql)) {
      error = q.lastError().text();
      return false;
    }
    for (const QVariant &value : values) q.addBindValue(value);
    if (!q.exec()) {
      error = q.lastError().text();
      return false;
    }
    return true;
  };
  auto fetched = [&]() {
    if (!q.lastError().isValid()) return true;
    error = q.lastError().text();
    return false;
  };

  QQueue<int> pending;
  for (int id : ids) pending.enqueue(id);
  QSet<int> removed;
  QList<int> order;
  QSet<int> parents;
  while (!pending.isEmpty()) {
    const int id = pending.dequeue();
    // Overlapping selections must neither repeat deletes nor loop forever.
    if (removed.contains(id)) continue;
    if (!execute("SELECT parentId FROM feeds WHERE id=?", {id})) return rollback();
    if (!q.next()) {
      error = q.lastError().isValid() ? q.lastError().text()
          : tr("A selected feed or folder no longer exists. Please select it again.");
      return rollback();
    }
    parents.insert(q.value(0).toInt());
    removed.insert(id);
    order.append(id);
    if (!execute("SELECT id FROM feeds WHERE parentId=?", {id})) return rollback();
    while (q.next()) pending.enqueue(q.value(0).toInt());
    if (!fetched()) return rollback();
  }
  for (int id : std::as_const(removed)) parents.remove(id);

  for (auto it = order.crbegin(); it != order.crend(); ++it) {
    const int id = *it;
    // Bound per-feed operations avoid expression/parameter limits for a large
    // subtree. Remove dependent records before their owning feed.
    for (const char *sql : {"DELETE FROM news_ex WHERE feedId=?",
                            "DELETE FROM news WHERE feedId=?",
                            "DELETE FROM feeds_ex WHERE feedId=?",
                            "DELETE FROM feeds WHERE id=?"}) {
      if (!execute(QString::fromLatin1(sql), {id})) return rollback();
    }
    if (q.numRowsAffected() != 1) {
      error = tr("A selected feed or folder could not be deleted.");
      return rollback();
    }
  }

  for (int parent : std::as_const(parents)) {
    QList<int> siblings;
    if (!execute("SELECT id FROM feeds WHERE parentId=? ORDER BY rowToParent", {parent}))
      return rollback();
    while (q.next()) siblings.append(q.value(0).toInt());
    if (!fetched()) return rollback();
    for (int row = 0; row < siblings.size(); ++row) {
      if (!execute("UPDATE feeds SET rowToParent=? WHERE id=?", {row, siblings.at(row)}))
        return rollback();
    }

    // Recount bottom-up on each affected branch. Shared ancestors may need
    // recounting again after another branch has been repaired.
    QSet<int> visited;
    while (parent > 0) {
      if (visited.contains(parent)) {
        error = tr("The feed folder hierarchy contains a cycle.");
        return rollback();
      }
      visited.insert(parent);
      if (!execute("SELECT parentId FROM feeds WHERE id=?", {parent})) return rollback();
      if (!q.next()) {
        error = q.lastError().isValid() ? q.lastError().text()
            : tr("A parent folder no longer exists.");
        return rollback();
      }
      const int next = q.value(0).toInt();
      if (!execute("SELECT sum(unread), sum(undeleteCount), sum(newCount) "
                   "FROM feeds WHERE parentId=?", {parent})) return rollback();
      if (!q.next()) {
        error = q.lastError().isValid() ? q.lastError().text()
            : tr("Cannot recount the parent folder.");
        return rollback();
      }
      const QVariantList counts{q.value(0).toInt(), q.value(1).toInt(), q.value(2).toInt(), parent};
      if (!execute("UPDATE feeds SET unread=?, undeleteCount=?, newCount=? WHERE id=?", counts))
        return rollback();
      parent = next;
    }
  }
  q.finish();
  if (!db.commit()) {
    error = db.lastError().text();
    return rollback();
  }
  return true;
}

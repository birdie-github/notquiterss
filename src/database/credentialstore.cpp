// SPDX-License-Identifier: GPL-3.0-or-later
#include "credentialstore.h"

#include <QByteArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
QByteArray encodePassword(const QString &password)
{
  return password.toUtf8().toBase64();
}

QString decodePassword(const QVariant &value)
{
  return QString::fromUtf8(QByteArray::fromBase64(value.toByteArray()));
}

bool hasServerRow(QSqlQuery &q, const QString &server)
{
  if (!q.prepare(QStringLiteral("SELECT id FROM passwords WHERE server=?"))) return false;
  q.addBindValue(server);
  if (!q.exec()) return false;
  const bool exists = q.next();
  q.finish();
  return exists;
}

bool insertRow(QSqlQuery &q, const QString &server,
               const QString &username, const QString &password)
{
  if (!q.prepare(QStringLiteral(
          "INSERT INTO passwords (server, username, password) VALUES (?, ?, ?)"))) {
    return false;
  }
  q.addBindValue(server);
  q.addBindValue(username);
  q.addBindValue(encodePassword(password));
  return q.exec();
}
} // namespace

QList<CredentialStore::Entry> CredentialStore::loadAll(const QSqlDatabase &db)
{
  QList<Entry> entries;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (!q.exec(QStringLiteral("SELECT id, server, username, password FROM passwords")))
    return entries;
  while (q.next()) {
    entries.append({q.value(0).toInt(),
                    q.value(1).toString(),
                    q.value(2).toString(),
                    decodePassword(q.value(3))});
  }
  return entries;
}

bool CredentialStore::load(const QSqlDatabase &db, const QString &server, Credentials &out)
{
  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (!q.prepare(QStringLiteral("SELECT username, password FROM passwords WHERE server=?")))
    return false;
  q.addBindValue(server);
  if (!q.exec() || !q.next()) return false;
  out.username = q.value(0).toString();
  out.password = decodePassword(q.value(1));
  return true;
}

bool CredentialStore::insertIfMissing(const QSqlDatabase &db, const QString &server,
                                      const QString &username, const QString &password)
{
  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (hasServerRow(q, server)) return true;
  if (q.lastError().isValid()) return false;
  return insertRow(q, server, username, password);
}

bool CredentialStore::save(const QSqlDatabase &db, const QString &server,
                           const QString &username, const QString &password,
                           bool insertWhenMissing)
{
  QSqlQuery q(db);
  q.setForwardOnly(true);
  if (hasServerRow(q, server)) {
    if (!q.prepare(QStringLiteral("UPDATE passwords SET username=?, password=? WHERE server=?")))
      return false;
    q.addBindValue(username);
    q.addBindValue(encodePassword(password));
    q.addBindValue(server);
    return q.exec();
  }
  if (q.lastError().isValid() || !insertWhenMissing) return !q.lastError().isValid();
  return insertRow(q, server, username, password);
}

bool CredentialStore::removeById(const QSqlDatabase &db, int id)
{
  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("DELETE FROM passwords WHERE id=?"))) return false;
  q.addBindValue(id);
  return q.exec();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CREDENTIALSTORE_H
#define CREDENTIALSTORE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace CredentialStore {

struct Entry {
  int id = 0;
  QString server;
  QString username;
  QString password;
};

struct Credentials {
  QString username;
  QString password;
};

// Load all stored credential entries with Base64-decoded passwords.
QList<Entry> loadAll(const QSqlDatabase &db = QSqlDatabase::database());

// Look up credentials for a CredentialScope key; returns false if absent.
bool load(const QSqlDatabase &db, const QString &server, Credentials &out);
inline bool load(const QString &server, Credentials &out)
{
  return load(QSqlDatabase::database(), server, out);
}

// Insert credentials for server if no row exists yet.
bool insertIfMissing(const QSqlDatabase &db, const QString &server,
                     const QString &username, const QString &password);
inline bool insertIfMissing(const QString &server, const QString &username,
                            const QString &password)
{
  return insertIfMissing(QSqlDatabase::database(), server, username, password);
}

// Update existing credentials for server, or insert when insertWhenMissing is true.
bool save(const QSqlDatabase &db, const QString &server,
          const QString &username, const QString &password,
          bool insertWhenMissing = true);
inline bool save(const QString &server, const QString &username,
                 const QString &password, bool insertWhenMissing = true)
{
  return save(QSqlDatabase::database(), server, username, password, insertWhenMissing);
}

// Delete a stored entry by primary key using a prepared statement.
bool removeById(const QSqlDatabase &db, int id);

} // namespace CredentialStore

#endif // CREDENTIALSTORE_H

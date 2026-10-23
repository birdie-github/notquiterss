// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PENDINGDATABASE_H
#define PENDINGDATABASE_H

#include <QString>

namespace PendingDatabase {
// Run before opening the application's SQL connections. On failure startup must
// stop, and the pending file is retained for recovery or another attempt.
bool install(const QString &databasePath, QString &error);
}

#endif // PENDINGDATABASE_H

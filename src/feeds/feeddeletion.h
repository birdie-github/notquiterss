// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDDELETION_H
#define FEEDDELETION_H

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace FeedDeletion {
// The caller holds database access. Delete the selected subtrees and repair
// sibling ordering and surviving ancestor counts in one checked transaction.
bool apply(QSqlDatabase db, const QList<int> &ids, QString &error);
}

#endif // FEEDDELETION_H

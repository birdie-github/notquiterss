// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDDATE_H
#define FEEDDATE_H

#include <QDateTime>
#include <QString>

namespace FeedDate {
// Publisher dates are stored as UTC seconds, deliberately without a suffix.
QString normalize(const QString &input);
// Also accepts legacy synthetic publication dates with a suffix. Missing or
// invalid publication dates use the receipt timestamp for display only.
QDateTime articleLocalTime(const QString &published, const QString &received);
}

#endif // FEEDDATE_H

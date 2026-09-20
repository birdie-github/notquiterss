// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPMLINPUT_H
#define OPMLINPUT_H
#include <QByteArray>
#include <QString>
namespace OpmlInput {
QByteArray prepare(QByteArray xmlData);
bool inspect(const QByteArray &data, int &httpCount, QString &error);
}
#endif

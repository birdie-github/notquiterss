// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDURL_H
#define FEEDURL_H
#include <QUrl>
class QWidget;
namespace FeedUrl {
QUrl normalize(QString input);
QUrl upgrade(QUrl url);
QString identity(const QUrl &url);
bool confirm(QWidget *parent, QUrl &url);
bool confirmImportHttp(QWidget *parent, int count);
}
#endif

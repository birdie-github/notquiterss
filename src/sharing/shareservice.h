/* ============================================================
* NotQuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* ============================================================ */
#ifndef SHARESERVICE_H
#define SHARESERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

struct ShareService
{
  QString id;
  QString name;
  QString actionName;
  QString iconPath;
  QString urlTemplate;
};

struct ShareServiceConfiguration
{
  QList<ShareService> services;
  QString installedFile;
  QString userFile;
  QString selectedFile;
  QString imageDirectory;
  QStringList errors;
};

class ShareServiceLoader
{
public:
  static ShareServiceConfiguration load(const QString &installedDirectory,
                                        const QString &userDirectory);
  static QUrl createUrl(const QString &urlTemplate, const QString &title,
                        const QString &articleUrl, QString *error = 0);
  static void writeDiagnostic(const QString &message);

private:
  static void log(const QString &message);
};

#endif // SHARESERVICE_H

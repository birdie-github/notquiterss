/* ============================================================
* NotQuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* ============================================================ */
#include "projectmetadata.h"
#include "shareservice.h"

#include <QDebug>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>

namespace {
const QString configurationFileName = ProjectMetadata::sharingConfig();
const QString imageDirectoryName = ProjectMetadata::sharingIcons();

QString encoded(const QString &value)
{
  return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

bool supportedUrl(const QUrl &url)
{
  const QString scheme = url.scheme().toLower();
  return url.isValid() && url.userInfo().isEmpty() &&
      (((scheme == QLatin1String("http") || scheme == QLatin1String("https")) &&
        !url.host().isEmpty()) || scheme == QLatin1String("mailto"));
}

bool validTemplate(const QString &urlTemplate, QString *error)
{
  if (error) error->clear();
  if (urlTemplate.isEmpty()) {
    if (error) *error = QStringLiteral("the URL template is empty");
    return false;
  }

  QString testUrl = urlTemplate;
  testUrl.replace(QStringLiteral("{url}"), QStringLiteral("https%3A%2F%2Fexample.com"));
  testUrl.replace(QStringLiteral("{title}"), QStringLiteral("Example"));
  if (testUrl.contains(QRegularExpression(QStringLiteral("\\{[^{}]+\\}")))) {
    if (error) *error = QStringLiteral("the URL template contains an unknown placeholder");
    return false;
  }

  const QUrl url = QUrl::fromEncoded(testUrl.toUtf8(), QUrl::StrictMode);
  if (!supportedUrl(url)) {
    if (error) {
      *error = QStringLiteral(
          "the URL template must produce an HTTP, HTTPS or mailto URL without user credentials");
    }
    return false;
  }
  return true;
}
}

ShareServiceConfiguration ShareServiceLoader::load(const QString &installedDirectory,
                                                    const QString &userDirectory)
{
  ShareServiceConfiguration result;
  result.installedFile = QDir(QDir(installedDirectory).filePath(imageDirectoryName)).filePath(configurationFileName);
  result.userFile = QDir(QDir(userDirectory).filePath(imageDirectoryName)).filePath(configurationFileName);

  if (QFileInfo(result.installedFile).isFile()) {
    result.selectedFile = result.installedFile;
    result.imageDirectory = QDir(installedDirectory).filePath(imageDirectoryName);
    log(QStringLiteral("using installed configuration: %1").arg(result.selectedFile));
  } else if (QFileInfo(result.userFile).isFile()) {
    result.selectedFile = result.userFile;
    result.imageDirectory = QDir(userDirectory).filePath(imageDirectoryName);
    log(QStringLiteral("installed configuration not found: %1").arg(result.installedFile));
    log(QStringLiteral("using user configuration: %1").arg(result.selectedFile));
  } else {
    result.errors << QStringLiteral("Neither configuration file exists.");
    log(QStringLiteral("installed configuration not found: %1").arg(result.installedFile));
    log(QStringLiteral("user configuration not found: %1").arg(result.userFile));
    return result;
  }

  log(QStringLiteral("using image directory: %1").arg(result.imageDirectory));
  QSettings settings(result.selectedFile, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  settings.setIniCodec("UTF-8");
#endif
  settings.beginGroup(QStringLiteral("services"));
  QStringList serviceIds = settings.value(QStringLiteral("order")).toStringList();
  settings.endGroup();
  if (settings.status() != QSettings::NoError) {
    result.errors << QStringLiteral("The selected configuration could not be parsed.");
    log(result.errors.constLast());
    return result;
  }
  // QSettings normally parses a comma-separated INI value as a QStringList,
  // but accept a scalar value as well for files written by other INI editors.
  if (serviceIds.size() == 1 && serviceIds.constFirst().contains(QLatin1Char(',')))
    serviceIds = serviceIds.constFirst().split(QLatin1Char(','));
  if (serviceIds.isEmpty()) {
    result.errors << QStringLiteral("The [services] order list is empty.");
    log(result.errors.constLast());
    return result;
  }

  QSet<QString> seenIds;
  QSet<QString> seenActionNames;
  for (const QString &idValue : serviceIds) {
    const QString id = idValue.trimmed();
    if (id.isEmpty() || seenIds.contains(id)) {
      const QString error = id.isEmpty()
          ? QStringLiteral("The service list contains an empty identifier.")
          : QStringLiteral("Service '%1' is listed more than once.").arg(id);
      result.errors << error;
      log(error);
      continue;
    }
    seenIds.insert(id);

    settings.beginGroup(id);
    const bool enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    ShareService service;
    service.id = id;
    service.name = settings.value(QStringLiteral("name")).toString().trimmed();
    service.actionName = settings.value(QStringLiteral("action")).toString().trimmed();
    const QString iconName = settings.value(QStringLiteral("icon")).toString().trimmed();
    service.urlTemplate = settings.value(QStringLiteral("url")).toString().trimmed();
    settings.endGroup();

    if (!enabled) {
      log(QStringLiteral("service '%1' is disabled").arg(id));
      continue;
    }

    QString error;
    if (service.name.isEmpty()) {
      error = QStringLiteral("the name is empty");
    } else if (iconName.isEmpty() || QFileInfo(iconName).fileName() != iconName) {
      error = QStringLiteral("the icon must be a filename in the social-networks directory");
    } else {
      service.iconPath = QDir(result.imageDirectory).filePath(iconName);
      if (!QFileInfo(service.iconPath).isFile())
        error = QStringLiteral("icon not found: %1").arg(service.iconPath);
      else
        validTemplate(service.urlTemplate, &error);
    }

    if (!error.isEmpty()) {
      const QString message = QStringLiteral("service '%1' ignored: %2").arg(id, error);
      result.errors << message;
      log(message);
      continue;
    }

    if (service.actionName.isEmpty())
      service.actionName = QStringLiteral("share.%1").arg(id);
    if (seenActionNames.contains(service.actionName)) {
      const QString message = QStringLiteral(
          "service '%1' ignored: action name '%2' is already in use")
          .arg(id, service.actionName);
      result.errors << message;
      log(message);
      continue;
    }
    seenActionNames.insert(service.actionName);
    result.services.append(service);
    log(QStringLiteral("loaded service '%1'").arg(id));
  }

  if (result.services.isEmpty()) {
    result.errors << QStringLiteral("The selected configuration contains no usable services.");
    log(result.errors.constLast());
  }
  return result;
}

QUrl ShareServiceLoader::createUrl(const QString &urlTemplate, const QString &title,
                                   const QString &articleUrl, QString *error)
{
  QString expanded = urlTemplate;
  expanded.replace(QStringLiteral("{url}"), encoded(articleUrl));
  expanded.replace(QStringLiteral("{title}"), encoded(title));
  const QUrl url = QUrl::fromEncoded(expanded.toUtf8(), QUrl::StrictMode);
  if (!supportedUrl(url)) {
    if (error) *error = QStringLiteral("the expanded sharing URL is unsupported or invalid");
    return QUrl();
  }
  if (error) error->clear();
  return url;
}

void ShareServiceLoader::writeDiagnostic(const QString &message)
{
  log(message);
}

void ShareServiceLoader::log(const QString &message)
{
  qInfo().noquote() << QStringLiteral("[article-sharing] %1").arg(message);
}

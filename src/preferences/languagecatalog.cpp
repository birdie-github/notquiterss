#include "projectmetadata.h"
#include "languagecatalog.h"

#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

namespace {
QString normalizedId(QString id)
{
  id.replace(QLatin1Char('-'), QLatin1Char('_'));
  QStringList parts = id.split(QLatin1Char('_'));
  for (int i = 0; i < parts.size(); ++i) {
    parts[i] = i == 0 ? parts[i].toLower() : parts[i].toUpper();
    if (i > 0 && parts[i].size() == 4) {
      parts[i] = parts[i].toLower();
      parts[i][0] = parts[i][0].toUpper();
    }
  }
  return parts.join(QLatin1Char('_'));
}

bool validId(const QString &id)
{
  static const QRegularExpression pattern(
      QStringLiteral("^[a-z]{2,3}(?:_[A-Z][a-z]{3})?(?:_(?:[A-Z]{2}|[0-9]{3}))?$"));
  return pattern.match(id).hasMatch();
}

QString languageName(const QString &id)
{
  const QLocale locale(id);
  // QLocale falls back for unknown identifiers; do not mislabel them.
  if (locale.language() == QLocale::C ||
      locale.name().section(QLatin1Char('_'), 0, 0) != id.section(QLatin1Char('_'), 0, 0))
    return id;
  const QString name = locale.nativeLanguageName();
  return name.isEmpty() ? id : name;
}
}

LanguageCatalog::LanguageCatalog(const QString &installedDirectory, const QString &userDirectory)
{
  QStringList directories{QDir::cleanPath(installedDirectory), QDir::cleanPath(userDirectory)};
  directories.removeDuplicates();
  QMap<QString, Entry> found;
  Entry english;
  english.id = QStringLiteral("en");
  english.name = QStringLiteral("English");
  found.insert(english.id, english);

  for (const QString &directory : directories) {
    const QDir dir(directory);
    const QStringList files = dir.entryList({ProjectMetadata::translationPrefix() + QStringLiteral("*.qm")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
      const QString id = normalizedId(file.mid(ProjectMetadata::translationPrefix().size(), file.size() - ProjectMetadata::translationPrefix().size() - 3));
      if (!validId(id) || id == QLatin1String("en")) continue;
      const QString path = dir.absoluteFilePath(file);
      QTranslator probe;
      // Reject Qt's implicit filename fallback: only this exact file registers
      // the locale, even if it disappears between directory scanning and loading.
      if (!probe.load(path) || probe.filePath() != path) {
        qWarning() << "Cannot load translation:" << path;
        continue;
      }
      Entry entry = found.value(id);
      entry.id = id;
      entry.fileName = path;
      if (entry.name.isEmpty()) entry.name = languageName(id);
      found.insert(id, entry);
    }
  }

  // Metadata never registers a language on its own. User keys override installed
  // keys individually; omitted keys inherit, while explicitly empty keys clear.
  for (const QString &directory : directories) {
    QSettings metadata(QDir(directory).filePath(QStringLiteral("languages.ini")), QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    metadata.setIniCodec("UTF-8");
#endif
    metadata.setFallbacksEnabled(false);
    const QStringList groups = metadata.childGroups();
    for (const QString &group : groups) {
      auto entry = found.find(normalizedId(group));
      if (entry == found.end()) continue;
      metadata.beginGroup(group);
      if (metadata.contains(QStringLiteral("name"))) entry->name = metadata.value(QStringLiteral("name")).toString();
      if (metadata.contains(QStringLiteral("author"))) entry->author = metadata.value(QStringLiteral("author")).toString();
      if (metadata.contains(QStringLiteral("contact"))) entry->contact = metadata.value(QStringLiteral("contact")).toString();
      if (metadata.contains(QStringLiteral("emoji"))) entry->emoji = metadata.value(QStringLiteral("emoji")).toString();
      metadata.endGroup();
    }
    if (metadata.status() != QSettings::NoError)
      qWarning() << "Cannot read language metadata:" << metadata.fileName();
  }
  for (Entry &entry : found) {
    if (entry.name.isEmpty()) entry.name = languageName(entry.id);
  }
  entries_.append(found.take(QStringLiteral("en")));
  entries_.append(found.values());
}

QString LanguageCatalog::translationFile(const QString &id) const
{
  const QString normalized = normalizedId(id);
  for (const Entry &entry : entries_) {
    if (entry.id == normalized) return entry.fileName;
  }
  return QString();
}

QString LanguageCatalog::defaultLanguage(const QStringList &preferredLanguages) const
{
  for (const QString &language : preferredLanguages) {
    QString id = normalizedId(language);
    while (validId(id)) {
      if (id == QLatin1String("en") || !translationFile(id).isEmpty()) return id;
      const int separator = id.lastIndexOf(QLatin1Char('_'));
      if (separator < 0) break;
      id.truncate(separator);
    }
  }
  return QStringLiteral("en");
}

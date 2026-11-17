#ifndef LANGUAGECATALOG_H
#define LANGUAGECATALOG_H

#include <QList>
#include <QString>
#include <QStringList>

// A snapshot of installed translations with per-user overrides. No UI ownership.
class LanguageCatalog
{
public:
  struct Entry {
    QString id;
    QString name;
    QString author;
    QString contact;
    QString emoji;
    QString fileName;
  };

  LanguageCatalog(const QString &installedDirectory, const QString &userDirectory);
  const QList<Entry> &entries() const { return entries_; }
  QString translationFile(const QString &id) const;
  QString defaultLanguage(const QStringList &preferredLanguages) const;

private:
  QList<Entry> entries_;
};

#endif // LANGUAGECATALOG_H

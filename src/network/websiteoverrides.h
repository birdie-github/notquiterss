#ifndef WEBSITEOVERRIDES_H
#define WEBSITEOVERRIDES_H

#include <QList>
#include <QStringList>
#include <QUrl>

// Loaded before workers start; immutable during request processing.
class WebsiteOverrides
{
public:
  void load(const QString &path);
  QString userAgent(const QUrl &url, const QString &fallback) const;
  QString validationError(const QUrl &url) const;
  QString path() const { return path_; }
  QString error() const { return error_; }
  bool loaded() const { return loaded_; }

private:
  struct Rule {
    QString host, agent, message;
    QStringList required;
    bool subdomains = false;
    bool hasAgent = false, hasRequired = false, hasMessage = false;
  };
  Rule resolve(const QUrl &url) const;
  QList<Rule> rules_;
  QString path_, error_;
  bool loaded_ = false;
};
#endif

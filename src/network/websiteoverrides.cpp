#include "websiteoverrides.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QUrlQuery>
#include <algorithm>

void WebsiteOverrides::load(const QString &path)
{
  path_ = path;
  error_.clear();
  rules_.clear();
  loaded_ = false;
  if (!QFile::exists(path)) return;
  auto fail = [this](const QString &detail) {
    rules_.clear();
    error_ = QCoreApplication::translate("WebsiteOverrides", "Invalid overrides file: %1").arg(detail);
  };
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    fail(file.errorString());
    return;
  }
  file.close();
  QSettings settings(path, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  settings.setIniCodec("UTF-8");
#endif
  settings.setFallbacksEnabled(false);
  if (!settings.childKeys().isEmpty()) {
    fail(QStringLiteral("keys must be inside a host section"));
    return;
  }
  const QStringList allowed{QStringLiteral("UserAgent"), QStringLiteral("IncludeSubdomains"),
      QStringLiteral("RequiredQueryParameters"), QStringLiteral("MissingParametersMessage")};
  QStringList hosts;
  for (const QString &section : settings.childGroups()) {
    Rule rule;
    rule.host = QString::fromLatin1(QUrl::toAce(section)).toLower();
    if (rule.host.endsWith(QLatin1Char('.'))) rule.host.chop(1);
    static const QRegularExpression hostname(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*$"));
    if (!hostname.match(rule.host).hasMatch() || rule.host.size() > 253 ||
        section.contains(QLatin1Char('*')) || hosts.contains(rule.host)) {
      fail(QStringLiteral("invalid or duplicate host section: ") + section);
      return;
    }
    hosts.append(rule.host);
    settings.beginGroup(section);
    for (const QString &key : settings.allKeys()) {
      if (!allowed.contains(key)) {
        fail(section + QLatin1Char('/') + key);
        return;
      }
    }
    const QString subdomains = settings.value("IncludeSubdomains", "false").toString().toLower();
    if (subdomains != QLatin1String("true") && subdomains != QLatin1String("false")) {
      fail(section + QStringLiteral("/IncludeSubdomains must be true or false"));
      return;
    }
    rule.subdomains = subdomains == QLatin1String("true");
    rule.hasAgent = settings.contains("UserAgent");
    rule.agent = settings.value("UserAgent").toString();
    if (rule.hasAgent && (rule.agent.isEmpty() || rule.agent.contains(QLatin1Char('\r')) ||
                         rule.agent.contains(QLatin1Char('\n')))) {
      fail(section + QStringLiteral("/UserAgent must be nonempty and single-line"));
      return;
    }
    rule.hasRequired = settings.contains("RequiredQueryParameters");
    rule.required = settings.value("RequiredQueryParameters").toStringList();
    for (QString &parameter : rule.required) parameter = parameter.trimmed();
    rule.required.removeAll(QString());
    rule.required.removeDuplicates();
    rule.hasMessage = settings.contains("MissingParametersMessage");
    rule.message = settings.value("MissingParametersMessage").toString();
    settings.endGroup();
    rules_.append(rule);
  }
  if (settings.status() != QSettings::NoError) {
    fail(QStringLiteral("cannot parse INI file"));
    return;
  }
  std::stable_sort(rules_.begin(), rules_.end(), [](const Rule &a, const Rule &b) {
    return a.host.size() < b.host.size();
  });
  loaded_ = true;
}

WebsiteOverrides::Rule WebsiteOverrides::resolve(const QUrl &url) const
{
  Rule result;
  QString host = QString::fromLatin1(QUrl::toAce(url.host())).toLower();
  if (host.endsWith(QLatin1Char('.'))) host.chop(1);
  for (const Rule &rule : rules_) {
    if (rule.host != host &&
        !(rule.subdomains && host.endsWith(QLatin1Char('.') + rule.host)))
      continue;
    if (rule.hasAgent) result.agent = rule.agent;
    if (rule.hasRequired) result.required = rule.required;
    if (rule.hasMessage) result.message = rule.message;
  }
  return result;
}

QString WebsiteOverrides::userAgent(const QUrl &url, const QString &fallback) const
{
  const QString agent = resolve(url).agent;
  return agent.isEmpty() ? fallback : agent;
}

QString WebsiteOverrides::validationError(const QUrl &url) const
{
  if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
    return {};
  const Rule rule = resolve(url);
  QStringList missing;
  const QUrlQuery query(url);
  for (const QString &parameter : rule.required) {
    const QStringList values = query.allQueryItemValues(parameter, QUrl::FullyDecoded);
    // Duplicate keys with empty values are ambiguous to servers; reject them.
    if (values.isEmpty() || values.contains(QString())) missing.append(parameter);
  }
  if (missing.isEmpty()) return {};
  QString message = QCoreApplication::translate("WebsiteOverrides",
      "Feed URL for %1 is missing nonempty query parameters: %2.")
      .arg(url.host(), missing.join(QLatin1String(", ")));
  if (!rule.message.isEmpty()) message += QStringLiteral("\n\n") + rule.message;
  return message;
}

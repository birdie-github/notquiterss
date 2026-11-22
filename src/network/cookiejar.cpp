/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#include "cookiejar.h"
#include "common/logfile.h"

#include <QDateTime>
#include <QFile>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QDebug>
#include <algorithm>

namespace {
constexpr qint64 maximumFileSize = 8 * 1024 * 1024;
constexpr int maximumCookies = 10000;
const QByteArray sitePrefix("# NotQuiteRSS-site\t");

QString hostOf(QString scope)
{
  if (scope.startsWith(QLatin1Char('.'))) scope.remove(0, 1);
  return scope;
}

// A leading dot explicitly permits subdomains. Exact hosts do not.
bool covers(const QString &scope, const QString &candidate)
{
  if (scope == candidate) return true;
  if (!scope.startsWith(QLatin1Char('.'))) return false;
  const QString host = hostOf(candidate);
  return host == hostOf(scope) || host.endsWith(scope);
}

QString normalizedScope(QString text)
{
  const bool subdomains = text.startsWith(QLatin1Char('.'));
  text = hostOf(text);
  const QByteArray ace = QUrl::toAce(text);
  if (ace.isEmpty() || ace.size() > 253) return {};
  const QString host = QString::fromLatin1(ace).toLower();
  // Cookie scopes are DNS names, not URLs, wildcard expressions or paths.
  for (const QChar ch : host) {
    if (!(ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) &&
        !(ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) &&
        ch != QLatin1Char('-') && ch != QLatin1Char('.')) return {};
  }
  for (const QString &label : host.split(QLatin1Char('.'))) {
    if (label.isEmpty() || label.size() > 63 || label.startsWith(QLatin1Char('-')) ||
        label.endsWith(QLatin1Char('-'))) return {};
  }
  return (subdomains ? QStringLiteral(".") : QString()) + QUrl::fromAce(ace).toLower();
}

bool withinLimits(const QList<QNetworkCookie> &cookies)
{
  if (cookies.size() > maximumCookies) return false;
  qint64 bytes = 0;
  for (const auto &cookie : cookies) {
    const auto size = cookie.toRawForm(QNetworkCookie::Full).size();
    if (size > 65536) return false;
    bytes += size;
    if (bytes > maximumFileSize) return false;
  }
  return true;
}

bool safeField(const QByteArray &field)
{
  for (unsigned char ch : field) if (ch < 32 || ch == 127) return false;
  return true;
}

bool readFile(const QString &path, CookieJar::Import *result,
              QSet<QString> *storedSites, QString *error)
{
  *result = {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = file.errorString();
    return false;
  }
  if (file.size() > maximumFileSize) {
    *error = CookieJar::tr("Cookie files must be no larger than 8 MiB.");
    return false;
  }
  const QByteArray bytes = file.read(maximumFileSize + 1);
  if (file.error() != QFile::NoError || bytes.size() > maximumFileSize) {
    *error = CookieJar::tr("Could not read the cookie file completely.");
    return false;
  }
  QSet<QString> sites;
  int lineNumber = 0;
  int records = 0;
  const auto now = QDateTime::currentDateTimeUtc();
  for (QByteArray line : bytes.split('\n')) {
    ++lineNumber;
    if (line.endsWith('\r')) line.chop(1);
    if (line.isEmpty()) continue;
    if (storedSites && line.startsWith(sitePrefix)) {
      const QString site = normalizedScope(QString::fromUtf8(line.mid(sitePrefix.size())));
      if (site.isEmpty() || storedSites->size() >= maximumCookies) {
        *error = CookieJar::tr("Invalid website entry on line %1.").arg(lineNumber);
        return false;
      }
      storedSites->insert(site);
      continue;
    }
    const bool httpOnly = line.startsWith("#HttpOnly_");
    if (httpOnly) line.remove(0, 10);
    else if (line.startsWith('#')) continue;
    const QList<QByteArray> fields = line.split('\t');
    auto invalid = [&](const QString &reason) {
      *error = CookieJar::tr("Line %1: %2").arg(lineNumber).arg(reason);
      return false;
    };
    if (line.size() > 65536)
      return invalid(CookieJar::tr("This cookie exceeds the 64 KiB size limit."));
    if (fields.size() != 7)
      return invalid(CookieJar::tr("Expected 7 tab-separated fields, found %1. "
                                   "Export the cookies in Netscape cookies.txt format.").arg(fields.size()));
    if (++records > maximumCookies)
      return invalid(CookieJar::tr("The file exceeds the limit of %1 cookies.").arg(maximumCookies));
    for (int i = 0; i < fields.size(); ++i) {
      if (!safeField(fields[i]))
        return invalid(CookieJar::tr("Field %1 contains a control character.").arg(i + 1));
    }
    if (fields[1] != "TRUE" && fields[1] != "FALSE")
      return invalid(CookieJar::tr("The subdomain flag (field 2) must be TRUE or FALSE."));
    if (fields[3] != "TRUE" && fields[3] != "FALSE")
      return invalid(CookieJar::tr("The secure flag (field 4) must be TRUE or FALSE."));
    QString domain = hostOf(QString::fromUtf8(fields[0]));
    if (fields[1] == "TRUE") domain.prepend(QLatin1Char('.'));
    domain = normalizedScope(domain);
    if (domain.isEmpty())
      return invalid(CookieJar::tr("Field 1 must contain a valid domain name."));
    const QByteArray &timestamp = fields[4];
    const auto dot = timestamp.indexOf('.');
    bool ok = false;
    const QByteArray wholeSeconds = dot >= 0 ? timestamp.left(dot) : timestamp;
    const qint64 expiry = wholeSeconds.toLongLong(&ok);
    bool hasFraction = false;
    if (dot >= 0) {
      // Browser exporters may include fractional seconds in a Netscape file.
      const auto digit = [](char ch) { return ch >= '0' && ch <= '9'; };
      const auto point = timestamp.cbegin() + dot;
      ok = ok && dot > 0 && point + 1 != timestamp.cend() &&
           std::all_of(timestamp.cbegin(), point, digit) &&
           std::all_of(point + 1, timestamp.cend(), digit);
      hasFraction = std::any_of(point + 1, timestamp.cend(), [](char ch) { return ch != '0'; });
    }
    if (!ok || expiry < 0)
      return invalid(CookieJar::tr("The expiry time (field 5) must be a Unix timestamp in seconds, "
                                   "or 0 for a session cookie."));
    if (!fields[2].startsWith('/'))
      return invalid(CookieJar::tr("The cookie path (field 3) must start with a slash."));
    if (fields[5].isEmpty())
      return invalid(CookieJar::tr("The cookie name (field 6) is empty."));
    // Validate name/value without interpreting the value as Set-Cookie attributes.
    const QByteArray raw = fields[5] + '=' + fields[6];
    const auto parsed = QNetworkCookie::parseCookies(raw);
    if (parsed.size() != 1 || parsed.first().name() != fields[5] ||
        parsed.first().value() != fields[6])
      return invalid(CookieJar::tr("The cookie name or value (fields 6 and 7) could not be read exactly as written."));
    QNetworkCookie cookie(fields[5], fields[6]);
    cookie.setDomain(domain);
    cookie.setPath(QString::fromUtf8(fields[2]));
    cookie.setSecure(fields[3] == "TRUE");
    cookie.setHttpOnly(httpOnly);
    if (expiry || hasFraction) {
      const auto date = QDateTime::fromSecsSinceEpoch(expiry, QTimeZone(0));
      if (!date.isValid())
        return invalid(CookieJar::tr("The expiry time (field 5) is outside the supported date range."));
      cookie.setExpirationDate(date);
      if (date <= now) { ++result->expired; continue; }
    }
    sites.insert(domain);
    result->cookies.append(cookie);
  }
  result->sites = sites.values();
  result->sites.sort();
  return true;
}

class NetworkCookieJar final : public QNetworkCookieJar
{
public:
  NetworkCookieJar(CookieJar *store, QObject *parent)
    : QNetworkCookieJar(parent), store_(store) {}
  QList<QNetworkCookie> cookiesForUrl(const QUrl &url) const override
  { return store_->cookiesForUrl(url); }
  bool setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url) override
  { return store_->setCookiesFromUrl(cookies, url); }
private:
  CookieJar *store_;
};
}

CookieJar::CookieJar(const QString &path, QObject *parent)
  : QNetworkCookieJar(parent), path_(path)
{
  loadCookies();
  auto *timer = new QTimer(this);
  timer->setInterval(6 * 60 * 60 * 1000);
  connect(timer, &QTimer::timeout, this, [this]() { saveCookies(); });
  timer->start();
}

QNetworkCookieJar *CookieJar::createNetworkJar(QObject *parent)
{
  return new NetworkCookieJar(this, parent);
}

bool CookieJar::enabled(const QString &scope) const
{
  for (const auto &site : sites_) if (covers(site, scope)) return true;
  return false;
}

void CookieJar::addSite(const QString &scope)
{
  if (enabled(scope)) return;
  for (auto it = sites_.begin(); it != sites_.end();) {
    if (covers(scope, *it)) it = sites_.erase(it);
    else ++it;
  }
  sites_.insert(scope);
}

QList<QNetworkCookie> CookieJar::cookiesForUrl(const QUrl &url) const
{
  QMutexLocker lock(&mutex_);
  if (!enabled(url.host().toLower())) return {};
  const auto cookies = QNetworkCookieJar::cookiesForUrl(url);
  if (LogFile::consoleLoggingEnabled()) {
    QStringList names;
    for (const auto &cookie : cookies) names.append(QString::fromLatin1(cookie.name()));
    qInfo() << "[cookies] request host" << url.host() << "names" << names;
  }
  return cookies;
}

bool CookieJar::setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url)
{
  QMutexLocker lock(&mutex_);
  if (!enabled(url.host().toLower())) return false;
  const auto previous = allCookies();
  const QByteArray before = persistentData();
  bool changed = false;
  QStringList names;
  for (auto cookie : cookies) {
    cookie.normalize(url);
    // Do not let an enabled host enable its parent domain or sibling hosts.
    cookie.setDomain(normalizedScope(cookie.domain()));
    if (!cookie.domain().isEmpty() && enabled(cookie.domain()) &&
        safeField(cookie.name()) && safeField(cookie.value()) &&
        safeField(cookie.path().toUtf8()) && cookie.toRawForm().size() <= 65536 &&
        validateCookie(cookie, url)) {
      names.append(QString::fromLatin1(cookie.name()));
      // Already normalized: a second normalize() would turn a host-only cookie
      // into a domain cookie. Preserve Qt's validation, then insert directly.
      changed = insertCookie(cookie) || changed;
    }
  }
  const QByteArray after = persistentData();
  if (!withinLimits(allCookies()) || after.size() > maximumFileSize) {
    setAllCookies(previous);
    qWarning() << "[cookies] response rejected: store size limit";
    return false;
  }
  if (LogFile::consoleLoggingEnabled())
    qInfo() << "[cookies] response host" << url.host() << "accepted names" << names;
  if (before != after) dirty_ = true;
  return changed;
}

QByteArray CookieJar::persistentData() const
{
  QList<QByteArray> lines;
  const auto now = QDateTime::currentDateTimeUtc();
  for (const auto &cookie : allCookies()) {
    if (cookie.isSessionCookie() || cookie.expirationDate() <= now) continue;
    QByteArray line;
    if (cookie.isHttpOnly()) line += "#HttpOnly_";
    line += cookie.domain().toUtf8() + '\t';
    line += cookie.domain().startsWith(QLatin1Char('.')) ? "TRUE\t" : "FALSE\t";
    line += cookie.path().toUtf8() + '\t';
    line += cookie.isSecure() ? "TRUE\t" : "FALSE\t";
    line += QByteArray::number(cookie.expirationDate().toSecsSinceEpoch()) + '\t';
    line += cookie.name() + '\t' + cookie.value() + '\n';
    lines.append(line);
  }
  std::sort(lines.begin(), lines.end());
  QByteArray data("# Netscape HTTP Cookie File\n# Managed by NotQuiteRSS. Contains private credentials.\n");
  QStringList sites = sites_.values();
  sites.sort();
  for (const auto &site : sites) data += sitePrefix + site.toUtf8() + '\n';
  for (const auto &line : lines) data += line;
  return data;
}

bool CookieJar::saveLocked(QString *error)
{
  if (!dirty_) return true;
  if (!writable_) {
    if (error) *error = tr("The cookie file could not be loaded. Repair or move it and restart before making changes.");
    return false;
  }
  QSaveFile file(path_);
  const QByteArray data = persistentData();
  if (data.size() > maximumFileSize || sites_.size() > maximumCookies ||
      !withinLimits(allCookies())) {
    if (error) *error = tr("The cookie store exceeds its size limit.");
    qWarning() << "[cookies] store exceeds size limit; not saved";
    return false;
  }
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(data) != data.size() || !file.commit()) {
    if (error) *error = file.errorString();
    qWarning() << "[cookies] could not save" << path_ << file.errorString();
    return false;
  }
  dirty_ = false;
  return true;
}

bool CookieJar::saveCookies(QString *error)
{
  QMutexLocker lock(&mutex_);
  // Expiry alone also requires removing the old persistent record from disk.
  auto cookies = allCookies();
  const auto now = QDateTime::currentDateTimeUtc();
  for (auto it = cookies.begin(); it != cookies.end();) {
    if (!it->isSessionCookie() && it->expirationDate() <= now) {
      it = cookies.erase(it);
      dirty_ = true;
    } else ++it;
  }
  setAllCookies(cookies);
  return saveLocked(error);
}

void CookieJar::loadCookies()
{
  if (!QFile::exists(path_)) return;
  Import data;
  QSet<QString> sites;
  QString error;
  if (!readFile(path_, &data, &sites, &error)) {
    writable_ = false; // Never overwrite a malformed or unreadable store.
    qWarning() << "[cookies] could not load" << path_ << error;
    return;
  }
  for (const auto &site : sites) addSite(site);
  for (const auto &cookie : data.cookies) {
    if (!cookie.isSessionCookie() && enabled(cookie.domain())) insertCookie(cookie);
  }
  dirty_ = data.expired != 0;
  if (LogFile::consoleLoggingEnabled())
    qInfo() << "[cookies] loaded" << path_ << "cookies" << allCookies().size()
            << "expired" << data.expired << "sites" << sites_.size();
}

bool CookieJar::readImport(const QString &path, Import *data, QString *error)
{
  return readFile(path, data, nullptr, error);
}

bool CookieJar::importCookies(const Import &data, QString *error)
{
  QMutexLocker lock(&mutex_);
  const auto previous = allCookies();
  const auto previousSites = sites_;
  const bool previousDirty = dirty_;
  for (const auto &site : data.sites) addSite(site);
  for (const auto &cookie : data.cookies) insertCookie(cookie);
  dirty_ = true;
  if (saveLocked(error)) return true;
  setAllCookies(previous);
  sites_ = previousSites;
  dirty_ = previousDirty;
  return false;
}

QStringList CookieJar::enabledSites() const
{
  QMutexLocker lock(&mutex_);
  QStringList sites = sites_.values();
  sites.sort();
  return sites;
}

bool CookieJar::removeSite(const QString &site, QString *error)
{
  QMutexLocker lock(&mutex_);
  if (!sites_.contains(site)) return true;
  const auto previous = allCookies();
  const bool previousDirty = dirty_;
  auto cookies = previous;
  for (auto it = cookies.begin(); it != cookies.end();) {
    if (covers(site, it->domain())) it = cookies.erase(it);
    else ++it;
  }
  sites_.remove(site);
  setAllCookies(cookies);
  dirty_ = true;
  if (saveLocked(error)) return true;
  sites_.insert(site);
  setAllCookies(previous);
  dirty_ = previousDirty;
  return false;
}

/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
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
#ifndef COOKIEJAR_H
#define COOKIEJAR_H

#include <QMutex>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QSet>
#include <QStringList>

// One locked store; each network manager owns a separate forwarding jar.
class CookieJar : public QNetworkCookieJar
{
  Q_OBJECT
public:
  struct Import {
    QList<QNetworkCookie> cookies;
    QStringList sites;
    int expired = 0;
  };

  explicit CookieJar(const QString &path, QObject *parent = nullptr);
  QNetworkCookieJar *createNetworkJar(QObject *parent);
  QList<QNetworkCookie> cookiesForUrl(const QUrl &url) const override;
  bool setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url) override;
  static bool readImport(const QString &path, Import *result, QString *error);
  bool importCookies(const Import &data, QString *error);
  QStringList enabledSites() const;
  bool removeSite(const QString &site, QString *error);
  bool saveCookies(QString *error = nullptr);

private:
  void loadCookies();
  QByteArray persistentData() const; // caller holds mutex_
  bool saveLocked(QString *error);
  bool enabled(const QString &scope) const;
  void addSite(const QString &scope);
  const QString path_;
  mutable QMutex mutex_;
  QSet<QString> sites_;
  bool dirty_ = false;
  bool writable_ = true;
};

#endif // COOKIEJAR_H

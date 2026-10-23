#ifndef CREDENTIALSCOPE_H
#define CREDENTIALSCOPE_H

#include <QNetworkProxy>
#include <QUrl>

// Namespaced keys in passwords.server deliberately never match legacy host keys.
namespace CredentialScope {
inline QUrl origin(const QUrl &url)
{
  QUrl result;
  result.setScheme(url.scheme().toLower());
  result.setHost(url.host().toLower());
  result.setPort(url.port(url.scheme() == QLatin1String("https") ? 443 : 80));
  return result;
}

inline QString server(const QUrl &url, const QString &realm)
{
  return QStringLiteral("http-auth:") + origin(url).toString(QUrl::FullyEncoded) +
      QLatin1Char('#') + QString::fromLatin1(QUrl::toPercentEncoding(realm));
}

inline QString proxy(const QNetworkProxy &proxy, const QString &realm)
{
  QUrl endpoint;
  endpoint.setScheme(QStringLiteral("proxy-%1").arg(int(proxy.type())));
  endpoint.setHost(proxy.hostName().toLower());
  endpoint.setPort(proxy.port());
  return QStringLiteral("proxy-auth:") + endpoint.toString(QUrl::FullyEncoded) +
      QLatin1Char('#') + QString::fromLatin1(QUrl::toPercentEncoding(realm));
}

// Explicit credentials belong to the subscription URL, not a server challenge.
inline QString feed(const QUrl &url)
{
  QUrl endpoint(url);
  endpoint.setUserInfo(QString());
  endpoint.setFragment(QString());
  endpoint.setPort(url.port(url.scheme() == QLatin1String("https") ? 443 : 80));
  return QStringLiteral("feed-auth:") + endpoint.toString(QUrl::FullyEncoded);
}
}

#endif // CREDENTIALSCOPE_H

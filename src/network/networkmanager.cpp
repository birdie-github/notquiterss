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
#include "networkmanager.h"
#include "networkpolicy.h"

#include "mainapplication.h"
#include "globals.h"
#include "settings.h"
#include "authenticationdialog.h"

#include <QNetworkReply>
#include <QSslConfiguration>
#include <QDirIterator>
#include <QFile>

NetworkManager::NetworkManager(bool isThread, QObject* parent)
  : NetworkManager(isThread, parent, false)
{
}

NetworkManager::NetworkManager(bool isThread, QObject *parent, bool isolatedTransport)
  : QNetworkAccessManager(parent), isThread_(isThread), isolatedTransport_(isolatedTransport)
{
  setCookieJar(mainApp->cookieJar()->createNetworkJar(this));

#ifndef QT_NO_NETWORKPROXY
  qRegisterMetaType<QNetworkProxy>("QNetworkProxy");
  qRegisterMetaType<QList<QSslError> >("QList<QSslError>");
#endif

  connect(this, &QNetworkAccessManager::sslErrors, this, &NetworkManager::slotSslError);

  if (isThread) {
    connect(this, &QNetworkAccessManager::authenticationRequired,
            mainApp->networkManager(), &NetworkManager::slotAuthentication, Qt::BlockingQueuedConnection);
    connect(this, &QNetworkAccessManager::proxyAuthenticationRequired,
            mainApp->networkManager(), &NetworkManager::slotProxyAuthentication, Qt::BlockingQueuedConnection);

  } else {
    connect(this, &QNetworkAccessManager::authenticationRequired, this, &NetworkManager::slotAuthentication);
    connect(this, &QNetworkAccessManager::proxyAuthenticationRequired,
            this, &NetworkManager::slotProxyAuthentication);

    if (!isolatedTransport_) loadSettings();
  }
}

NetworkManager::~NetworkManager()
{
}

void NetworkManager::loadSettings()
{
  // Load roots from Qt's platform trust store, not the retired bundled file.
  loadCertificates();
}

void NetworkManager::loadCertificates()
{
  Settings settings("SSL-Configuration");
  const QStringList certPaths = settings.value("CACertPaths", QStringList()).toStringList();

  // CA Certificates
  QList<QSslCertificate> caCerts = QSslConfiguration::systemCaCertificates();

  for (const QString &path : certPaths) {
    QDirIterator it(path, QDir::Files, QDirIterator::FollowSymlinks | QDirIterator::Subdirectories);
    while (it.hasNext()) {
      QString filePath = it.next();
      if (!filePath.endsWith(QLatin1String(".crt"))) {
        continue;
      }

      QFile file(filePath);
      if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        caCerts += QSslCertificate::fromData(file.readAll(), QSsl::Pem);
      }
    }
  }
  // Saved certificate exceptions are not CA trust anchors. Only platform
  // roots and explicitly configured CA paths participate in verification.
  QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
  ssl.setProtocol(QSsl::TlsV1_2OrLater);
  ssl.setCaCertificates(caCerts);
  QSslConfiguration::setDefaultConfiguration(ssl);

}

/** @brief Request authentification
 *---------------------------------------------------------------------------*/
void NetworkManager::slotAuthentication(QNetworkReply *reply, QAuthenticator *auth)
{
  AuthenticationDialog *authenticationDialog =
      new AuthenticationDialog(reply->url(), auth);

  if (!authenticationDialog->save_->isChecked())
    authenticationDialog->exec();

  delete authenticationDialog;
}
/** @brief Request proxy authentification
 *---------------------------------------------------------------------------*/
void NetworkManager::slotProxyAuthentication(const QNetworkProxy &proxy, QAuthenticator *auth)
{
  AuthenticationDialog *authenticationDialog =
      new AuthenticationDialog(proxy, auth);

  if (!authenticationDialog->save_->isChecked())
    authenticationDialog->exec();

  delete authenticationDialog;
}

void NetworkManager::slotSslError(QNetworkReply *reply, QList<QSslError> errors)
{
  // Leave verification enabled. Qt will fail the request through its normal
  // error/finished path, without a modal dialog or a worker-thread UI call.
  QStringList descriptions;
  for (const QSslError &error : errors) {
    if (error.error() != QSslError::NoError)
      descriptions.append(error.errorString());
  }
  descriptions.removeDuplicates();
  reply->setProperty("tlsCertificateErrors", descriptions.join(QLatin1String("; ")));
}

QNetworkReply *NetworkManager::createRequest(QNetworkAccessManager::Operation op,
                                             const QNetworkRequest &request,
                                             QIODevice *outgoingData)
{
  if (!NetworkPolicy::isRequestUrl(request.url()))
    return new NetworkPolicy::RejectedReply(op, request, this);
  QNetworkRequest checked(request);
  checked.setRawHeader("User-Agent", globals.requestUserAgent(checked.url()).toUtf8());
  // Feed, favicon and download callers resolve and validate redirects themselves.
  checked.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  if (!isolatedTransport_ && !checked.url().userInfo().isEmpty()) {
    // Explicit feed credentials must not enter the shared origin/realm cache.
    // A separate manager also isolates Qt's authenticated HTTP connections.
    auto *transport = new NetworkManager(isThread_, this, true);
    transport->setProxy(proxy());
    // Return the raw reply: the outer head()/get() call installs Qt's normal
    // reply forwarding once, while the HTTP backend belongs to this transport.
    QNetworkReply *reply = transport->createRequest(op, checked, outgoingData);
    // Callers may retain or reparent a finished reply (e.g. downloads). Keep its
    // backend alive until the reply itself is released, including cancellation.
    connect(reply, &QObject::destroyed, transport, &QObject::deleteLater);
    return reply;
  }
  return QNetworkAccessManager::createRequest(op, checked, outgoingData);
}

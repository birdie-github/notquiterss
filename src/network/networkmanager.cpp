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
#include "settings.h"
#include "authenticationdialog.h"

#include <QNetworkReply>
#include <QSslConfiguration>
#include <QDirIterator>
#include <QFile>

NetworkManager::NetworkManager(bool isThread, QObject* parent)
  : QNetworkAccessManager(parent)
{
  setCookieJar(mainApp->cookieJar());
  // CookieJar is shared between NetworkManagers
  mainApp->cookieJar()->setParent(0);

#ifndef QT_NO_NETWORKPROXY
  qRegisterMetaType<QNetworkProxy>("QNetworkProxy");
  qRegisterMetaType<QList<QSslError> >("QList<QSslError>");
#endif

  connect(this, SIGNAL(sslErrors(QNetworkReply*, QList<QSslError>)),
          this, SLOT(slotSslError(QNetworkReply*, QList<QSslError>)));

  if (isThread) {
    connect(this, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            mainApp->networkManager(), SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)),
            Qt::BlockingQueuedConnection);
    connect(this, SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)),
            mainApp->networkManager(), SLOT(slotProxyAuthentication(QNetworkProxy,QAuthenticator*)),
            Qt::BlockingQueuedConnection);

  } else {
    connect(this, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)));
    connect(this, SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)),
            SLOT(slotProxyAuthentication(QNetworkProxy,QAuthenticator*)));

    loadSettings();
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

  foreach (const QString &path, certPaths) {
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
      new AuthenticationDialog(proxy.hostName(), auth);

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
  // Feed, favicon and download callers resolve and validate redirects themselves.
  checked.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  return QNetworkAccessManager::createRequest(op, checked, outgoingData);
}

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
/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2014  David Rosca <nowrep@gmail.com>
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
#include "networkmanagerproxy.h"
#include "networkpolicy.h"
#include "cookiejar.h"
#include "mainapplication.h"
#include "globals.h"

#include <QNetworkRequest>

NetworkManagerProxy::NetworkManagerProxy(QObject* parent)
  : QNetworkAccessManager(parent)

{
  setCookieJar(mainApp->cookieJar()->createNetworkJar(this));

#ifndef QT_NO_NETWORKPROXY
  qRegisterMetaType<QNetworkProxy>("QNetworkProxy");
  qRegisterMetaType<QList<QSslError> >("QList<QSslError>");
#endif

  connect(this, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
          mainApp->networkManager(), SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)));
  connect(this, SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)),
          mainApp->networkManager(), SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)));
  connect(this, SIGNAL(finished(QNetworkReply*)),
          mainApp->networkManager(), SIGNAL(finished(QNetworkReply*)));

}

QNetworkReply* NetworkManagerProxy::createRequest(QNetworkAccessManager::Operation op,
                                                  const QNetworkRequest &request,
                                                  QIODevice* outgoingData)
{
  if (!NetworkPolicy::isHttpUrl(request.url()))
    return new NetworkPolicy::RejectedReply(op, request, this);
  QNetworkRequest checked(request);
  checked.setRawHeader("User-Agent", globals.requestUserAgent(checked.url()).toUtf8());
  checked.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
  checked.setMaximumRedirectsAllowed(10);
  QNetworkReply *reply = QNetworkAccessManager::createRequest(op, checked, outgoingData);
  QUrl initialUrl = request.url();
  connect(reply, &QNetworkReply::redirected, reply, [reply, initialUrl](const QUrl &target) mutable {
    const QUrl resolved = initialUrl.resolved(target);
    if (NetworkPolicy::isSafeRedirect(initialUrl, resolved)) {
      initialUrl = resolved;
      emit reply->redirectAllowed();
    } else {
      reply->abort();
    }
  });
  return reply;
}

void NetworkManagerProxy::disconnectObjects()
{

  disconnect(this);
  disconnect(mainApp->networkManager());
}

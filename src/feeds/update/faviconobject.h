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
#ifndef FAVICONOBJECT_H
#define FAVICONOBJECT_H

#include <QDateTime>
#include <QObject>
#include <QQueue>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

#include "network/networkmanager.h"

class FaviconObject : public QObject
{
  Q_OBJECT

public:
  explicit FaviconObject(QObject *parent = 0);

  void disconnectObjects();
  static QUrl initialPageUrl(const QString &websiteUrl, const QString &feedUrl);

public slots:
  void requestUrl(QString urlString, QString feedUrl);
  void requestUrlWithKey(QString urlString, QString feedUrl, QString resultKey);
  void stopRequest();

signals:
  void startTimer();
  void signalConnecting(QString resultKey, int secondsLeft);
  void signalConnected(QString resultKey);
  void signalIconRecived(QString resultKey, QByteArray byteArray, QString format);
  void signalIconFailed(QString resultKey, bool timedOut = false);

private slots:
  void getQueuedUrl();
  void finished(QNetworkReply *reply);
  void slotRequestTimeout();

private:
  struct RequestIdentity {
    // Host throttling follows the feed, while callers choose the result key.
    QString feedHost;
    QString resultKey;
  };

  struct QueuedRequest {
    QUrl pageUrl;
    RequestIdentity identity;
  };

  struct ActiveRequest {
    RequestIdentity identity;
    int count;
    int timeLeft;
    QNetworkReply *reply;
  };

  struct PendingRetry {
    quint64 token;
    QUrl getUrl;
    RequestIdentity identity;
    int count;
    bool isIcon;
  };

  void requestResource(const QUrl &url, const RequestIdentity &identity, int count, bool isIcon);
  void dispatchResource(const QUrl &url, const RequestIdentity &identity, int count, bool isIcon);
  int indexOfReply(QNetworkReply *reply) const;

  NetworkManager *networkManager_;
  bool shuttingDown_ = false;
  quint64 nextRetryToken_ = 0;

  QQueue<QueuedRequest> requestQueue_;

  QTimer *timeout_;
  QTimer *getUrlTimer_;
  QList<ActiveRequest> activeRequests_;
  QList<PendingRetry> pendingRetries_;
  QList<QString> hostList_;

};

#endif // FAVICONOBJECT_H

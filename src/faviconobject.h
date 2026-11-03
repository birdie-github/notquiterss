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
#ifndef FAVICONOBJECT_H
#define FAVICONOBJECT_H

#include <QDateTime>
#include <QObject>
#include <QQueue>
#include <QNetworkReply>
#include <QTimer>

#include "networkmanager.h"

class FaviconObject : public QObject
{
  Q_OBJECT

public:
  explicit FaviconObject(QObject *parent = 0);

  void disconnectObjects();

public slots:
  void requestUrl(QString urlString, QString feedUrl);
  void slotGet(const QUrl &getUrl, const QString &feedUrl, const int &count);

signals:
  void startTimer();
  void signalGet(const QUrl &getUrl, const QString &feedUrl, const int &count);
  void signalIconRecived(QString feedUrl, QByteArray byteArray, QString format);

private slots:
  void getQueuedUrl();
  void finished(QNetworkReply *reply);
  void slotRequestTimeout();

private:
  struct QueuedRequest {
    QString urlString;
    QString feedUrl;
  };

  struct ActiveRequest {
    QString feedUrl;
    int count;
    int timeLeft;
    QNetworkReply *reply;
  };

  struct PendingRetry {
    quint64 token;
    QUrl getUrl;
    QString feedUrl;
    int count;
    bool isIcon;
  };

  void requestResource(const QUrl &url, const QString &feedUrl, int count, bool isIcon);
  void dispatchResource(const QUrl &url, const QString &feedUrl, int count, bool isIcon);
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

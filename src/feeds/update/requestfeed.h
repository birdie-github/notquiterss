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
#ifndef REQUESTFEED_H
#define REQUESTFEED_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QNetworkReply>
#include <QUrl>

class HttpRequestQueue;

class RequestFeed : public QObject
{
  Q_OBJECT
public:
  explicit RequestFeed(int timeoutRequest, int numberRequests,
                       int numberRepeats, QObject *parent = 0);
  ~RequestFeed();
  // Database IDs are positive. The wizard assigns a distinct negative ID per scan.
  static constexpr int FirstProbe = -2;

  void disconnectObjects();

public slots:
  void requestUrl(int id, QString urlString, QDateTime date, QString userInfo = "");
  void requestTrackedUrl(int id, QString urlString, QDateTime date,
                         QString userInfo, QString identity);
  void stopRequest();
  void slotHead(const QUrl &getUrl, const int &id, const QString &feedUrl,
                const QDateTime &date, const int &count);
  void slotGet(const QUrl &getUrl, const int &id, const QString &feedUrl,
               const QDateTime &date, const int &count);

signals:
  void requestStarted(int feedId, QString identity, QDateTime startedAt);
  void getUrlDone(int result, int feedId, QString feedUrl = "",
                  QString error = "", QByteArray data = QByteArray(),
                  QDateTime dtReply = QDateTime(), QString codecName = "", QUrl effective = {}, QString mimeType = {});
  void signalHead(const QUrl &getUrl, const int &id, const QString &feedUrl,
                  const QDateTime &date, const int &count = 0);
  void signalGet(const QUrl &getUrl, const int &id, const QString &feedUrl,
                 const QDateTime &date, const int &count = 0);
  void setStatusFeed(int feedId, QString status);
  void feedProgressStage(int feedId, QString stage);

private slots:
  void finished(QNetworkReply *reply);

private:
  struct ActiveRequestMeta {
    int id = 0;
    QString feedUrl;
    QDateTime date;
    int count = 0;
    bool isHead = false;
  };

  void scheduleOrDispatch(bool isHead, const QUrl &getUrl, int id,
                          const QString &feedUrl, const QDateTime &date, int count);
  void dispatchHead(const QUrl &getUrl, int id, const QString &feedUrl,
                    const QDateTime &date, int count);
  void dispatchGet(const QUrl &getUrl, int id, const QString &feedUrl,
                   const QDateTime &date, int count);
  void trackActiveReply(QNetworkReply *reply, int id, const QString &feedUrl,
                        const QDateTime &date, int count, bool isHead);

  HttpRequestQueue *queue_;
  int numberRepeats_;
  QHash<QNetworkReply *, ActiveRequestMeta> activeMeta_;

};

#endif // REQUESTFEED_H

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
#include "requestfeed.h"
#include "networkpolicy.h"
#include "mainapplication.h"
#include "globals.h"
#include "common.h"

#include <QDebug>
#include <QtSql>
#include <QRegularExpression>

#define REPLY_MAX_COUNT 10

namespace {
QString browserChallengeProvider(QNetworkReply *reply)
{
  if (reply->rawHeader("cf-mitigated").trimmed().toLower() == "challenge")
    return QStringLiteral("Cloudflare");

  const QByteArray contentType = reply->rawHeader("Content-Type")
                                    .split(';').value(0).trimmed().toLower();
  if (contentType != "text/html") return {};

  // Anubis has no equivalent challenge header. Inspect a bounded prefix
  // without consuming the reply, and require its generated HTML/script
  // markers rather than matching the product name in article text.
  const QString html = QString::fromUtf8(reply->peek(256 * 1024));
  static const QRegularExpression document(
      QStringLiteral(R"(^\s*(?:<!doctype\s+html[^>]*>\s*)?<html(?:\s|>))"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression challenge(
      QStringLiteral(R"(<script\b[^>]*\sid\s*=\s*["']anubis_challenge["'][^>]*>)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression runner(
      QStringLiteral(R"(<script\b[^>]*\ssrc\s*=\s*["'][^"']*/\.within\.website/x/cmd/anubis/static/js/main\.mjs(?:\?[^"']*)?["'][^>]*>)"),
      QRegularExpression::CaseInsensitiveOption);
  // The alternate Preact challenge embeds its runner and uses preact_info.
  static const QRegularExpression preact(
      QStringLiteral(R"(<script\b[^>]*\sid\s*=\s*["']preact_info["'][^>]*>\s*\{)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression version(
      QStringLiteral(R"(<script\b[^>]*\sid\s*=\s*["']anubis_version["'][^>]*>)"),
      QRegularExpression::CaseInsensitiveOption);
  const bool preactChallenge = preact.match(html).hasMatch() &&
      version.match(html).hasMatch() &&
      html.contains(QStringLiteral("/.within.website/x/cmd/anubis/static/img/pensive.webp"));
  if (document.match(html).hasMatch() &&
      ((challenge.match(html).hasMatch() && runner.match(html).hasMatch()) ||
       preactChallenge))
    return QStringLiteral("Anubis");
  return {};
}
}

RequestFeed::RequestFeed(int timeoutRequest, int numberRequests,
                         int numberRepeats, QObject *parent)
  : QObject(parent)
  , networkManager_(NULL)
  , timeoutRequest_(timeoutRequest)
  , numberRequests_(numberRequests)
  , numberRepeats_(numberRepeats)
{
  setObjectName("requestFeed_");

  timeout_ = new QTimer(this);
  timeout_->setInterval(1000);
  connect(timeout_, SIGNAL(timeout()), this, SLOT(slotRequestTimeout()));

  getUrlTimer_ = new QTimer(this);
  getUrlTimer_->setSingleShot(true);
  getUrlTimer_->setInterval(50);
  connect(getUrlTimer_, SIGNAL(timeout()), this, SLOT(getQueuedUrl()));

  connect(this, SIGNAL(signalHead(QUrl,int,QString,QDateTime,int)),
          SLOT(slotHead(QUrl,int,QString,QDateTime,int)),
          Qt::QueuedConnection);
  connect(this, SIGNAL(signalGet(QUrl,int,QString,QDateTime,int)),
          SLOT(slotGet(QUrl,int,QString,QDateTime,int)),
          Qt::QueuedConnection);
}

RequestFeed::~RequestFeed()
{

}

void RequestFeed::disconnectObjects()
{
  disconnect(this);
  if (networkManager_)
    networkManager_->disconnect(networkManager_);
}

/** @brief Put URL in request queue
 *----------------------------------------------------------------------------*/
void RequestFeed::requestUrl(int id, QString urlString,
                              QDateTime date, QString userInfo)
{
  if (!networkManager_) {
    networkManager_ = new NetworkManager(true, this);
    connect(networkManager_, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(finished(QNetworkReply*)));
  }

  if (!timeout_->isActive())
    timeout_->start();

  idsQueue_.enqueue(id);
  feedsQueue_.enqueue(urlString);
  dateQueue_.enqueue(date);
  userInfo_.enqueue(userInfo);

  if (!getUrlTimer_->isActive())
    getUrlTimer_->start();

  qDebug() << "requestUrl() <<" << urlString << "countQueue=" << feedsQueue_.count();
}

void RequestFeed::stopRequest()
{
  while (!feedsQueue_.isEmpty()) {
    int feedId = idsQueue_.dequeue();
    QString feedUrl = feedsQueue_.dequeue();
    dateQueue_.clear();
    userInfo_.clear();

    emit getUrlDone(-7, feedId, feedUrl);
  }
}

/** @brief Process request queue on timer timeouts
 *----------------------------------------------------------------------------*/
void RequestFeed::getQueuedUrl()
{
  if ((currentFeeds_.count() >= numberRequests_) ||
      (currentFeeds_.count() >= REPLY_MAX_COUNT)) {
    getUrlTimer_->start();
    return;
  }

  if (!feedsQueue_.isEmpty()) {
    getUrlTimer_->start();
    QString feedUrl = feedsQueue_.head();

    if (hostList_.contains(QUrl(feedUrl).host())) {
      foreach (QString url, currentFeeds_) {
        if (QUrl(url).host() == QUrl(feedUrl).host()) {
          return;
        }
      }
    }
    int feedId = idsQueue_.dequeue();
    feedUrl = feedsQueue_.dequeue();

    emit setStatusFeed(feedId, "1 Update");

    QUrl getUrl = QUrl::fromEncoded(feedUrl.toUtf8());
    QString userInfo = userInfo_.dequeue();
    if (!userInfo.isEmpty()) {
      getUrl.setUserInfo(userInfo);
//      getUrl.addQueryItem("auth", getUrl.scheme());
    }

    qDebug() << "getQueuedUrl() >>" << feedUrl << "countQueue=" << feedsQueue_.count();
    QDateTime currentDate = dateQueue_.dequeue();
    if (currentDate.isValid())
      emit signalHead(getUrl, feedId, feedUrl, currentDate);
    else
      emit signalGet(getUrl, feedId, feedUrl, currentDate);
  }
}

/** @brief Prepare and send network request to get head
 *----------------------------------------------------------------------------*/
void RequestFeed::slotHead(const QUrl &getUrl, const int &id, const QString &feedUrl,
                            const QDateTime &date, const int &count)
{
  const QString overrideError = globals.overrides().validationError(getUrl);
  if (!overrideError.isEmpty()) {
    emit getUrlDone(-1, id, feedUrl, overrideError);
    return;
  }
  if (count)
    Common::sleep(30);

  qDebug() << objectName() << "::head:" << getUrl.toEncoded() << "feed:" << feedUrl << "countRepeats:" << count;
  QNetworkRequest request(getUrl);
  request.setRawHeader("User-Agent", globals.feedUserAgent(getUrl).toUtf8());

  currentUrls_.append(getUrl);
  currentIds_.append(id);
  currentFeeds_.append(feedUrl);
  currentDates_.append(date);
  currentCount_.append(count);
  currentHead_.append(true);
  currentTime_.append(timeoutRequest_);

  QNetworkReply *reply = networkManager_->head(request);
  reply->setProperty("feedReply", QVariant(true));
  requestUrl_.append(reply->url());
  networkReply_.append(reply);
}

/** @brief Prepare and send network request to get all data
 *----------------------------------------------------------------------------*/
void RequestFeed::slotGet(const QUrl &getUrl, const int &id, const QString &feedUrl,
                           const QDateTime &date, const int &count)
{
  const QString overrideError = globals.overrides().validationError(getUrl);
  if (!overrideError.isEmpty()) {
    emit getUrlDone(-1, id, feedUrl, overrideError);
    return;
  }
  if (count)
    Common::sleep(30);

  qDebug() << objectName() << "::get:" << getUrl.toEncoded() << "feed:" << feedUrl << "countRepeats:" <<count;
  QNetworkRequest request(getUrl);
  request.setRawHeader("Accept", "application/atom+xml,application/rss+xml;q=0.9,application/xml;q=0.8,text/xml;q=0.7,*/*;q=0.6");
  request.setRawHeader("User-Agent", globals.feedUserAgent(getUrl).toUtf8());

  currentUrls_.append(getUrl);
  currentIds_.append(id);
  currentFeeds_.append(feedUrl);
  currentDates_.append(date);
  currentCount_.append(count);
  currentHead_.append(false);
  currentTime_.append(timeoutRequest_);

  QNetworkReply *reply = networkManager_->get(request);
  reply->setProperty("feedReply", QVariant(true));
  requestUrl_.append(reply->url());
  networkReply_.append(reply);
}

/** @brief Process network reply
 *----------------------------------------------------------------------------*/
void RequestFeed::finished(QNetworkReply *reply)
{
  QUrl replyUrl = reply->url();

  qDebug() << "reply.finished():" << replyUrl.toString();
  qDebug() << reply->header(QNetworkRequest::ContentTypeHeader);
  qDebug() << reply->header(QNetworkRequest::ContentLengthHeader);
  qDebug() << reply->header(QNetworkRequest::LocationHeader);
  qDebug() << reply->header(QNetworkRequest::LastModifiedHeader);
  qDebug() << reply->header(QNetworkRequest::CookieHeader);
  qDebug() << reply->header(QNetworkRequest::SetCookieHeader);

  int currentReplyIndex = currentUrls_.indexOf(replyUrl);

  if (currentReplyIndex >= 0) {
    currentTime_.removeAt(currentReplyIndex);
    currentUrls_.removeAt(currentReplyIndex);
    int feedId    = currentIds_.takeAt(currentReplyIndex);
    QString feedUrl    = currentFeeds_.takeAt(currentReplyIndex);
    QDateTime feedDate = currentDates_.takeAt(currentReplyIndex);
    int count = currentCount_.takeAt(currentReplyIndex) + 1;
    bool headOk = currentHead_.takeAt(currentReplyIndex);
    const QString challengeProvider = browserChallengeProvider(reply);

    if (reply->error() == QNetworkReply::OperationCanceledError) {
      emit getUrlDone(-7, feedId, feedUrl);
    } else if (!challengeProvider.isEmpty()) {
      // Finish once, without retrying or passing the challenge HTML to parsing.
      emit getUrlDone(-1, feedId, feedUrl,
                     tr("%1 is blocking access to this feed.\n\n"
                        "This website requires browser verification before allowing access. "
                        "%2 cannot complete this verification because it does not include "
                        "a full web browser.\n\n"
                        "Please contact the website operator and ask them to exempt their "
                        "RSS/Atom feed URLs from browser challenges.")
                         .arg(challengeProvider, QCoreApplication::applicationName()));
    } else if (reply->error() != QNetworkReply::NoError) {
      qDebug() << "  error retrieving RSS feed:" << reply->error() << reply->errorString();
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      QString diagnostic = httpStatus > 0
          ? tr("HTTP %1: %2").arg(httpStatus).arg(reply->errorString())
          : tr("Network error: %1").arg(reply->errorString());
      const QString certificateErrors = reply->property("tlsCertificateErrors").toString();
      if (!certificateErrors.isEmpty())
        diagnostic = tr("TLS certificate error: %1").arg(certificateErrors);
      if (reply->error() == QNetworkReply::ProtocolUnknownError) {
        emit getUrlDone(-1, feedId, feedUrl, diagnostic);
      } else if (!headOk) {
        if (reply->error() == QNetworkReply::AuthenticationRequiredError)
          emit getUrlDone(-2, feedId, feedUrl, diagnostic);
        else if (reply->error() == QNetworkReply::ContentNotFoundError)
          emit getUrlDone(-5, feedId, feedUrl, diagnostic);
        else {
          if (reply->errorString().contains("Service Temporarily Unavailable")) {
            if (!hostList_.contains(QUrl(feedUrl).host())) {
              hostList_.append(QUrl(feedUrl).host());
              count--;
            }
          }

          if (count < numberRepeats_) {
            emit signalGet(replyUrl, feedId, feedUrl, feedDate, count);
          } else {
            emit getUrlDone(-1, feedId, feedUrl, diagnostic);
          }
        }
      } else {
        emit signalGet(replyUrl, feedId, feedUrl, feedDate);
      }
    } else {
      QUrl redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (!redirectionTarget.isEmpty()) {
        redirectionTarget = replyUrl.resolved(redirectionTarget);
        if (!NetworkPolicy::isSafeRedirect(replyUrl, redirectionTarget)) {
          emit getUrlDone(-4, feedId, feedUrl, tr("Unsupported or unsafe redirect URL."));
        } else if (count < (numberRepeats_ + 3)) {
          if (headOk && (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302)) {
            emit signalGet(replyUrl, feedId, feedUrl, feedDate);
          } else {
            if (reply->operation() == QNetworkAccessManager::HeadOperation) {
              qDebug() << objectName() << "  head redirect..." << redirectionTarget.toString();
              emit signalHead(redirectionTarget, feedId, feedUrl, feedDate, count);
            }
            else {
              qDebug() << objectName() << "  get redirect..." << redirectionTarget.toString();
              emit signalGet(redirectionTarget, feedId, feedUrl, feedDate, count);
            }
          }
        } else {
          emit getUrlDone(-4, feedId, feedUrl, tr("Redirect error!"));
        }
      } else {
        QDateTime replyDate = reply->header(QNetworkRequest::LastModifiedHeader).toDateTime();
        QDateTime replyLocalDate = QDateTime(replyDate.date(), replyDate.time());

        qDebug() << feedDate << replyDate << replyLocalDate;
        qDebug() << feedDate.toMSecsSinceEpoch() << replyDate.toMSecsSinceEpoch() << replyLocalDate.toMSecsSinceEpoch();
        if ((reply->operation() == QNetworkAccessManager::HeadOperation) &&
            ((!feedDate.isValid()) || (!replyLocalDate.isValid()) ||
             (feedDate != replyLocalDate) || !replyDate.toMSecsSinceEpoch())) {
          emit signalGet(replyUrl, feedId, feedUrl, feedDate);
        }
        else {
          QString codecName;
          QRegularExpression rx("charset=([^\t]+)$",
              QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
          QRegularExpressionMatch match = rx.match(reply->header(QNetworkRequest::ContentTypeHeader).toString());
          int pos = match.capturedStart();
          if (pos > -1) {
            codecName = match.captured(1);
          }

          QByteArray data = reply->readAll();
          data = data.trimmed();

          rx.setPattern("&(?!([a-z0-9#]+;))");
          pos = 0;
          // Latin-1 keeps match offsets aligned with the QByteArray being edited.
          while ((pos = rx.match(QString::fromLatin1(data), pos).capturedStart()) != -1) {
            data.replace(pos, 1, "&amp;");
            pos += 1;
          }

          data.replace("<br>", "<br/>");

          if (data.indexOf("</rss>") > 0)
            data.resize(data.indexOf("</rss>") + 6);
          if (data.indexOf("</feed>") > 0)
            data.resize(data.indexOf("</feed>") + 7);
          if (data.indexOf("</rdf:RDF>") > 0)
            data.resize(data.indexOf("</rdf:RDF>") + 10);

          if (data.isEmpty() && reply->operation() != QNetworkAccessManager::HeadOperation &&
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 304)
            emit getUrlDone(-6, feedId, feedUrl, tr("The server returned an empty feed response."));
          else
            emit getUrlDone(feedsQueue_.count(), feedId, feedUrl, "", data, replyLocalDate, codecName);
        }
      }
    }
  } else {
    qCritical() << "Request Url error: " << replyUrl.toString() << reply->errorString();
  }

  int replyIndex = requestUrl_.indexOf(replyUrl);
  if (replyIndex >= 0) {
    requestUrl_.removeAt(replyIndex);
    networkReply_.removeAt(replyIndex);
  }

  reply->abort();
  reply->deleteLater();
}

/** @brief Timeout to delete network requests which has no answer
 *----------------------------------------------------------------------------*/
void RequestFeed::slotRequestTimeout()
{
  for (int i = currentTime_.count() - 1; i >= 0; i--) {
    int time = currentTime_.at(i) - 1;
    if (time <= 0) {
      QUrl url = currentUrls_.takeAt(i);
      int feedId    = currentIds_.takeAt(i);
      QString feedUrl = currentFeeds_.takeAt(i);
      QDateTime feedDate = currentDates_.takeAt(i);
      int count = currentCount_.takeAt(i) + 1;
      currentTime_.removeAt(i);
      currentHead_.removeAt(i);

      int replyIndex = requestUrl_.indexOf(url);
      if (replyIndex >= 0) {
        QUrl replyUrl = requestUrl_.takeAt(replyIndex);
        QNetworkReply *reply = networkReply_.takeAt(replyIndex);
        reply->deleteLater();

        if (count < numberRepeats_) {
          emit signalGet(replyUrl, feedId, feedUrl, feedDate, count);
        } else {
          emit getUrlDone(-3, feedId, feedUrl, tr("Request timed out"));
        }
      }
    } else {
      currentTime_.replace(i, time);
    }
  }
}

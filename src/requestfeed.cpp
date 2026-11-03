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
#include "logfile.h"
#include "requestfeed.h"
#include <utility>
#include "networkpolicy.h"
#include "credentialscope.h"
#include "mainapplication.h"
#include "globals.h"
#include "common.h"

#include <QDebug>
#include <QtSql>
#include <QRegularExpression>
#include <QThread>

#define REPLY_MAX_COUNT 10

namespace {
// Full-content feeds can be large; cap download buffering before XML decoding
// and DOM construction amplify its memory cost.
constexpr qint64 FeedResponseLimit = 32LL * 1024 * 1024;
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
  , networkManager_(nullptr)
  , timeoutRequest_(timeoutRequest)
  , numberRequests_(numberRequests)
  , numberRepeats_(numberRepeats)
{
  setObjectName("requestFeed_");

  timeout_ = new QTimer(this);
  timeout_->setInterval(1000);
  connect(timeout_, &QTimer::timeout, this, &RequestFeed::slotRequestTimeout);

  getUrlTimer_ = new QTimer(this);
  getUrlTimer_->setSingleShot(true);
  getUrlTimer_->setInterval(50);
  connect(getUrlTimer_, &QTimer::timeout, this, &RequestFeed::getQueuedUrl);

  connect(this, &RequestFeed::signalHead, this, &RequestFeed::slotHead, Qt::QueuedConnection);
  connect(this, &RequestFeed::signalGet, this, &RequestFeed::slotGet, Qt::QueuedConnection);
}

RequestFeed::~RequestFeed()
{
  disconnectObjects();
}

void RequestFeed::disconnectObjects()
{
  // Disconnect outward notifications and self-dispatch immediately. Queued
  // slot calls may still arrive; the shutdown guards reject them in this thread.
  QObject::disconnect(this, nullptr, nullptr, nullptr);
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(this, [this] { disconnectObjects(); }, Qt::QueuedConnection);
    return;
  }
  if (shuttingDown_) return;
  shuttingDown_ = true;
  stopRequest();
  if (networkManager_)
    networkManager_->disconnect(this);
}

/** @brief Put URL in request queue
 *----------------------------------------------------------------------------*/
void RequestFeed::requestUrl(int id, QString urlString,
                              QDateTime date, QString userInfo)
{
  requestTrackedUrl(id, urlString, date, userInfo, QString());
}

void RequestFeed::requestTrackedUrl(int id, QString urlString, QDateTime date,
                                   QString userInfo, QString identity)
{
  if (shuttingDown_) return;

  if (!networkManager_) {
    networkManager_ = new NetworkManager(true, this);
    connect(networkManager_, &QNetworkAccessManager::finished, this, &RequestFeed::finished);
  }

  if (!timeout_->isActive())
    timeout_->start();

  requestQueue_.enqueue({id, urlString, date, userInfo, identity});

  if (!getUrlTimer_->isActive())
    getUrlTimer_->start();

  qDebug() << "requestUrl() <<" << LogFile::urlForLog(QUrl(urlString)) << "countQueue=" << requestQueue_.count();
}

void RequestFeed::stopRequest()
{
  // Do not start more work while cancellation is in progress.
  getUrlTimer_->stop();
  timeout_->stop();

  // Cancel requests which have not started yet.
  while (!requestQueue_.isEmpty()) {
    const QueuedRequest queued = requestQueue_.dequeue();
    emit getUrlDone(-7, queued.id, queued.feedUrl);
  }

  // Cancel delayed retries before their single-shot timer fires.
  while (!pendingRetries_.isEmpty()) {
    const PendingRetry retry = pendingRetries_.takeFirst();
    emit getUrlDone(-7, retry.id, retry.feedUrl);
  }

  // abort() may synchronously cause finished() to run in this thread, which
  // removes entries from activeRequests_. Iterate over a snapshot of replies.
  QList<QNetworkReply*> replies;
  replies.reserve(activeRequests_.size());
  for (const ActiveRequest &req : std::as_const(activeRequests_)) {
    replies.append(req.reply);
  }
  for (QNetworkReply *reply : std::as_const(replies)) {
    if (reply)
      reply->abort();
  }
}

/** @brief Process request queue on timer timeouts
 *----------------------------------------------------------------------------*/
void RequestFeed::getQueuedUrl()
{
  if (shuttingDown_) return;

  const int inFlightCount = activeRequests_.count() + pendingRetries_.count();
  if ((inFlightCount >= numberRequests_) ||
      (inFlightCount >= REPLY_MAX_COUNT)) {
    getUrlTimer_->start();
    return;
  }

  if (!requestQueue_.isEmpty()) {
    getUrlTimer_->start();
    const QString candidateHost = QUrl(requestQueue_.head().feedUrl).host();

    if (hostList_.contains(candidateHost)) {
      for (const ActiveRequest &req : std::as_const(activeRequests_)) {
        if (QUrl(req.feedUrl).host() == candidateHost) {
          return;
        }
      }
      for (const PendingRetry &retry : std::as_const(pendingRetries_)) {
        if (QUrl(retry.feedUrl).host() == candidateHost) {
          return;
        }
      }
    }
    const QueuedRequest queued = requestQueue_.dequeue();

    emit setStatusFeed(queued.id, "1 Update");

    QUrl getUrl = QUrl::fromEncoded(queued.feedUrl.toUtf8());
    if (!queued.userInfo.isEmpty()) {
      getUrl.setUserInfo(queued.userInfo);
//      getUrl.addQueryItem("auth", getUrl.scheme());
    }

    qDebug() << "getQueuedUrl() >>" << LogFile::urlForLog(QUrl(queued.feedUrl)) << "countQueue=" << requestQueue_.count();
    if (!queued.identity.isEmpty())
      emit requestStarted(queued.id, queued.identity, QDateTime::currentDateTimeUtc());
    if (queued.date.isValid())
      emit signalHead(getUrl, queued.id, queued.feedUrl, queued.date);
    else
      emit signalGet(getUrl, queued.id, queued.feedUrl, queued.date);
  }
}

/** @brief Prepare and send network request to get head
 *----------------------------------------------------------------------------*/
void RequestFeed::slotHead(const QUrl &getUrl, const int &id, const QString &feedUrl,
                            const QDateTime &date, const int &count)
{
  if (shuttingDown_) return;

  const QString overrideError = globals.overrides().validationError(getUrl);
  if (!overrideError.isEmpty()) {
    emit getUrlDone(-1, id, feedUrl, overrideError);
    return;
  }
  if (count > 0) {
    const quint64 token = ++nextRetryToken_;
    pendingRetries_.append({token, getUrl, id, feedUrl, date, count, true});
    QTimer::singleShot(30, this, [this, token] {
      if (shuttingDown_) return;
      for (int i = 0; i < pendingRetries_.size(); ++i) {
        if (pendingRetries_.at(i).token == token) {
          const PendingRetry retry = pendingRetries_.takeAt(i);
          dispatchHead(retry.getUrl, retry.id, retry.feedUrl, retry.date, retry.count);
          return;
        }
      }
    });
    return;
  }

  dispatchHead(getUrl, id, feedUrl, date, count);
}

void RequestFeed::dispatchHead(const QUrl &getUrl, int id, const QString &feedUrl,
                               const QDateTime &date, int count)
{
  if (shuttingDown_) return;

  qDebug() << objectName() << "::head:" << LogFile::urlForLog(getUrl) << "feed:" << LogFile::urlForLog(QUrl(feedUrl)) << "countRepeats:" << count;
  QNetworkRequest request(getUrl);

  emit feedProgressStage(id, tr("Checking…"));
  QNetworkReply *reply = networkManager_->head(request);
  reply->setProperty("feedReply", QVariant(true));
  activeRequests_.append({id, feedUrl, date, count, true, timeoutRequest_, reply});
}

/** @brief Prepare and send network request to get all data
 *----------------------------------------------------------------------------*/
void RequestFeed::slotGet(const QUrl &getUrl, const int &id, const QString &feedUrl,
                           const QDateTime &date, const int &count)
{
  if (shuttingDown_) return;

  const QString overrideError = globals.overrides().validationError(getUrl);
  if (!overrideError.isEmpty()) {
    emit getUrlDone(-1, id, feedUrl, overrideError);
    return;
  }
  if (count > 0) {
    const quint64 token = ++nextRetryToken_;
    pendingRetries_.append({token, getUrl, id, feedUrl, date, count, false});
    QTimer::singleShot(30, this, [this, token] {
      if (shuttingDown_) return;
      for (int i = 0; i < pendingRetries_.size(); ++i) {
        if (pendingRetries_.at(i).token == token) {
          const PendingRetry retry = pendingRetries_.takeAt(i);
          dispatchGet(retry.getUrl, retry.id, retry.feedUrl, retry.date, retry.count);
          return;
        }
      }
    });
    return;
  }

  dispatchGet(getUrl, id, feedUrl, date, count);
}

void RequestFeed::dispatchGet(const QUrl &getUrl, int id, const QString &feedUrl,
                              const QDateTime &date, int count)
{
  if (shuttingDown_) return;

  qDebug() << objectName() << "::get:" << LogFile::urlForLog(getUrl) << "feed:" << LogFile::urlForLog(QUrl(feedUrl)) << "countRepeats:" << count;
  QNetworkRequest request(getUrl);
  if (id <= FirstProbe) {
    request.setAttribute(NetworkPolicy::DiscoveryProbe, true);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
  }
  request.setRawHeader("Accept", "application/atom+xml,application/rss+xml;q=0.9,application/xml;q=0.8,text/xml;q=0.7,*/*;q=0.6");

  emit feedProgressStage(id, count ? tr("Retrying…") : tr("Connecting…"));
  QNetworkReply *reply = networkManager_->get(request);
  // Report the stage once, not on every data chunk. Length may be unknown.
  connect(reply, &QIODevice::readyRead, this, [this, reply, id] {
    if (indexOfReply(reply) < 0 ||
        reply->property("progressDownloading").toBool()) return;
    reply->setProperty("progressDownloading", true);
    emit feedProgressStage(id, tr("Downloading…"));
  });
  reply->setProperty("feedReply", QVariant(true));
  activeRequests_.append({id, feedUrl, date, count, false, timeoutRequest_, reply});
  NetworkPolicy::limitReplySize(reply, FeedResponseLimit);
}

int RequestFeed::indexOfReply(QNetworkReply *reply) const
{
  for (int i = 0; i < activeRequests_.size(); ++i) {
    if (activeRequests_.at(i).reply == reply)
      return i;
  }
  return -1;
}

/** @brief Process network reply
 *----------------------------------------------------------------------------*/
void RequestFeed::finished(QNetworkReply *reply)
{
  QUrl replyUrl = reply->url();

  qDebug() << "reply.finished():" << LogFile::urlForLog(replyUrl);
  qDebug() << reply->header(QNetworkRequest::ContentTypeHeader);
  qDebug() << reply->header(QNetworkRequest::ContentLengthHeader);
  qDebug() << "Location:" << LogFile::urlForLog(replyUrl.resolved(reply->header(QNetworkRequest::LocationHeader).toUrl()));
  qDebug() << reply->header(QNetworkRequest::LastModifiedHeader);
  // Cookie values are credentials; never write raw cookie headers to the log.

  const int currentReplyIndex = indexOfReply(reply);

  if (currentReplyIndex >= 0) {
    // Retire this exact reply before dispatching any completion or retry.
    const ActiveRequest active = activeRequests_.takeAt(currentReplyIndex);
    int feedId         = active.id;
    QString feedUrl    = active.feedUrl;
    QDateTime feedDate = active.date;
    int count          = active.count + 1;
    bool headOk        = active.isHead;
    const QString challengeProvider = browserChallengeProvider(reply);

    if (!headOk && NetworkPolicy::replyExceedsSizeLimit(reply, FeedResponseLimit)) {
      // Size rejection is neither a user cancellation nor a retryable failure.
      // Never send a partial body to the parser.
      emit getUrlDone(-6, feedId, feedUrl,
                     tr("Feed exceeds the %1 MiB size limit.")
                         .arg(FeedResponseLimit / (1024 * 1024)));
    } else if (reply->error() == QNetworkReply::OperationCanceledError) {
      emit getUrlDone(-7, feedId, feedUrl);
    } else if (!challengeProvider.isEmpty()) {
      // Finish once, without retrying or passing the challenge HTML to parsing.
      emit getUrlDone(-1, feedId, feedUrl,
                     tr("%1 is blocking access to this feed.\n\n"
                        "This website requires browser verification before allowing access. "
                        "%2 cannot complete this verification because it does not include "
                        "a full web browser.\n\n"
                        "You can try completing verification in your browser, then exporting "
                        "cookies in Netscape format and importing them in Tools > "
                        "Website cookies. A matching User-Agent may also be required "
                        "in overrides.ini. Importing cookies does not guarantee access. "
                        "Alternatively, ask the website operator to exempt RSS/Atom feeds.")
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

          if (feedId > FirstProbe && count < numberRepeats_) {
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
        redirectionTarget = CredentialScope::redirectTarget(replyUrl, redirectionTarget);
        if (!NetworkPolicy::isSafeRedirect(replyUrl, redirectionTarget) ||
            (feedId <= FirstProbe &&
             CredentialScope::origin(redirectionTarget) != CredentialScope::origin(QUrl(feedUrl)))) {
          emit getUrlDone(-4, feedId, feedUrl, tr("Unsupported or unsafe redirect URL."));
        } else if (count < (numberRepeats_ + 3)) {
          if (headOk && (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302)) {
            emit signalGet(replyUrl, feedId, feedUrl, feedDate);
          } else {
            if (reply->operation() == QNetworkAccessManager::HeadOperation) {
              qDebug() << objectName() << "  head redirect..." << LogFile::urlForLog(redirectionTarget);
              emit signalHead(redirectionTarget, feedId, feedUrl, feedDate, count);
            }
            else {
              qDebug() << objectName() << "  get redirect..." << LogFile::urlForLog(redirectionTarget);
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

          data = Common::repairXmlAmpersands(data);

          data.replace("<br>", "<br/>");

          if (data.isEmpty() && reply->operation() != QNetworkAccessManager::HeadOperation &&
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 304)
            emit getUrlDone(-6, feedId, feedUrl, tr("The server returned an empty feed response."));
          else
            emit getUrlDone(requestQueue_.count(), feedId, feedUrl, "", data, replyLocalDate, codecName,
                            replyUrl.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveFragment),
                            reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0).trimmed().toLower());
        }
      }
    }
  } else {
    // A timeout removes its reply before aborting it. Ignore that completion
    // so it cannot consume another request or dispatch a second result.
    reply->deleteLater();
    return;
  }

  reply->abort();
  reply->deleteLater();
}

/** @brief Timeout to delete network requests which has no answer
 *----------------------------------------------------------------------------*/
void RequestFeed::slotRequestTimeout()
{
  if (shuttingDown_) return;

  for (int i = activeRequests_.count() - 1; i >= 0; i--) {
    int time = activeRequests_.at(i).timeLeft - 1;
    if (time <= 0) {
      const ActiveRequest active = activeRequests_.takeAt(i);
      QNetworkReply *reply = active.reply;
      const QUrl replyUrl  = reply->url();
      const int count      = active.count + 1;

      // Remove all bookkeeping before abort(): finished() may run synchronously.
      reply->abort();
      reply->deleteLater();

      if (active.id > FirstProbe && count < numberRepeats_) {
        emit signalGet(replyUrl, active.id, active.feedUrl, active.date, count);
      } else {
        emit getUrlDone(-3, active.id, active.feedUrl, tr("Request timed out"));
      }
    } else {
      activeRequests_[i].timeLeft = time;
    }
  }
}

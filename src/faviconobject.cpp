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
#include "faviconobject.h"
#include <utility>
#include "networkpolicy.h"
#include "credentialscope.h"
#include "mainapplication.h"
#include "globals.h"
#include "common.h"

#include <QDebug>
#include <QtSql>
#include <QRegularExpression>
#include <QTextDocumentFragment>

#define REPLY_MAX_COUNT 4
#define REQUEST_TIMEOUT 30

namespace {
// Discovery examines only the first 64 KiB, but allow ordinary website pages.
constexpr qint64 FaviconPageLimit = 1LL * 1024 * 1024;
// Allow large PNGs and ICO files containing multiple icon resolutions.
constexpr qint64 FaviconImageLimit = 4LL * 1024 * 1024;
QUrl faviconUrl(const QByteArray &data, const QUrl &pageUrl)
{
  // Bound parsing work even for large pages. Icon declarations normally live
  // near the start of the head; keep the existing /favicon.ico fallback.
  const QString html = QString::fromUtf8(data.left(64 * 1024));

  // Skip comments and script bodies. Quoted attributes may contain '>'.
  static const QRegularExpression tags(
      QStringLiteral(R"rx(<!--.*?(?:-->|$)|<script\b[^>]*>.*?(?:</script\s*>|$)|<link\b((?:[^'"<>]++|'[^']*+'|"[^"]*+")*+)>|(</head\s*>))rx"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression attributes(
      QStringLiteral(R"rx((?:^|\s)([a-z][a-z0-9_-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s'"=<>`]+)))rx"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));

  QUrl bestUrl;
  int bestScore = -1;
  auto links = tags.globalMatch(html);
  while (links.hasNext()) {
    const auto link = links.next();
    // Recognize the head end only outside the comments and scripts skipped above.
    if (link.capturedStart(2) >= 0) break;
    if (link.capturedStart(1) < 0) continue;
    QString rel, href, sizes;
    auto attrs = attributes.globalMatch(link.captured(1));
    while (attrs.hasNext()) {
      const auto attr = attrs.next();
      const QString name = attr.captured(1).toLower();
      const QString value = attr.capturedStart(2) >= 0 ? attr.captured(2)
          : attr.capturedStart(3) >= 0 ? attr.captured(3) : attr.captured(4);
      if (name == QLatin1String("rel") && rel.isNull()) rel = value;
      else if (name == QLatin1String("href") && href.isNull()) href = value;
      else if (name == QLatin1String("sizes") && sizes.isNull()) sizes = value;
    }
    const QStringList relations = rel.toLower().split(whitespace, Qt::SkipEmptyParts);
    const bool icon = relations.contains(QStringLiteral("icon"));
    if (!icon && !relations.contains(QStringLiteral("apple-touch-icon"))) continue;
    if (href.trimmed().isEmpty()) continue;
    const QString decodedHref = QTextDocumentFragment::fromHtml(href).toPlainText().trimmed();
    const QUrl target = pageUrl.resolved(QUrl(decodedHref));
    if (!NetworkPolicy::isHttpUrl(target)) continue;
    const QStringList dimensions = sizes.toLower().split(whitespace, Qt::SkipEmptyParts);
    const int score = (icon ? 100 : 0) + (dimensions.contains(QStringLiteral("16x16")) ? 3
        : dimensions.contains(QStringLiteral("32x32")) ? 2 : 1);
    if (score > bestScore) {
      bestScore = score;
      bestUrl = target;
    }
  }
  return bestUrl;
}
}

FaviconObject::FaviconObject(QObject *parent)
  : QObject(parent)
  , networkManager_(nullptr)
{
  setObjectName("faviconObject_");

  timeout_ = new QTimer(this);
  timeout_->setInterval(1000);
  connect(timeout_, &QTimer::timeout, this, &FaviconObject::slotRequestTimeout);

  getUrlTimer_ = new QTimer(this);
  getUrlTimer_->setSingleShot(true);
  getUrlTimer_->setInterval(20);
  connect(getUrlTimer_, &QTimer::timeout, this, &FaviconObject::getQueuedUrl);

  connect(this, &FaviconObject::signalGet, this, &FaviconObject::slotGet);
}

void FaviconObject::disconnectObjects()
{
  shuttingDown_ = true;
  if (getUrlTimer_)
    getUrlTimer_->stop();
  if (timeout_)
    timeout_->stop();
  pendingRetries_.clear();
  disconnect(this);
  if (networkManager_)
    networkManager_->disconnect(networkManager_);
}

/** @brief Put requested URL in request queue
 *----------------------------------------------------------------------------*/
void FaviconObject::requestUrl(QString urlString, QString feedUrl)
{
  if (shuttingDown_) return;

  if (!networkManager_) {
    networkManager_ = new NetworkManager(true, this);
    connect(networkManager_, &QNetworkAccessManager::finished, this, &FaviconObject::finished);
  }

  if (!timeout_->isActive())
    timeout_->start();

  requestQueue_.enqueue({urlString, feedUrl});

  if (!getUrlTimer_->isActive())
    getUrlTimer_->start();
}

/** @brief Process request queue by timer
 *----------------------------------------------------------------------------*/
void FaviconObject::getQueuedUrl()
{
  if (shuttingDown_) return;

  if ((activeRequests_.count() + pendingRetries_.count()) >= REPLY_MAX_COUNT) {
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

    QUrl url = QUrl::fromEncoded(queued.urlString.toUtf8());
    url.setUrl(QString("%1://%2").arg(url.scheme()).arg(url.host()));
    if (!url.isValid()) {
      url = QUrl::fromEncoded(queued.feedUrl.toUtf8());
      url.setUrl(QString("%1://%2").arg(url.scheme()).arg(url.host()));
    }
    emit signalGet(url, queued.feedUrl, 0);
  }
}

/** @brief Prepare and send network request to receive all data
 *----------------------------------------------------------------------------*/
void FaviconObject::slotGet(const QUrl &getUrl, const QString &feedUrl, const int &count)
{
  // Existing dispatch uses 0/2 for pages and 1/3 for discovered/fallback icons.
  // Redirects use requestResource() to preserve the role independently of count.
  requestResource(getUrl, feedUrl, count, count == 1 || count == 3);
}

void FaviconObject::requestResource(const QUrl &getUrl, const QString &feedUrl,
                                   int count, bool isIcon)
{
  if (shuttingDown_) return;

  if (count > 0) {
    const quint64 token = ++nextRetryToken_;
    pendingRetries_.append({token, getUrl, feedUrl, count, isIcon});
    QTimer::singleShot(30, this, [this, token] {
      if (shuttingDown_) return;
      for (int i = 0; i < pendingRetries_.size(); ++i) {
        if (pendingRetries_.at(i).token == token) {
          const PendingRetry retry = pendingRetries_.takeAt(i);
          dispatchResource(retry.getUrl, retry.feedUrl, retry.count, retry.isIcon);
          return;
        }
      }
    });
    return;
  }

  dispatchResource(getUrl, feedUrl, count, isIcon);
}

void FaviconObject::dispatchResource(const QUrl &getUrl, const QString &feedUrl,
                                     int count, bool isIcon)
{
  if (shuttingDown_) return;

  QNetworkRequest request(getUrl);

  QNetworkReply *reply = networkManager_->get(request);
  reply->setProperty("feedReply", QVariant(true));
  reply->setProperty("faviconImage", isIcon);
  activeRequests_.append({feedUrl, count, REQUEST_TIMEOUT, reply});
  NetworkPolicy::limitReplySize(reply, isIcon ? FaviconImageLimit : FaviconPageLimit);
}

int FaviconObject::indexOfReply(QNetworkReply *reply) const
{
  for (int i = 0; i < activeRequests_.size(); ++i) {
    if (activeRequests_.at(i).reply == reply)
      return i;
  }
  return -1;
}

/** @brief Finish network request processing
 *----------------------------------------------------------------------------*/
void FaviconObject::finished(QNetworkReply *reply)
{
  const int currentReplyIndex = indexOfReply(reply);
  if (currentReplyIndex >= 0) {
    // Retries dispatch directly here, so retire the old reply first.
    const ActiveRequest active = activeRequests_.takeAt(currentReplyIndex);
    const QUrl url = reply->url();
    QString feedUrl = active.feedUrl;
    int cntRequests = active.count;

    const bool isIcon = reply->property("faviconImage").toBool();
    const qint64 limit = isIcon ? FaviconImageLimit : FaviconPageLimit;
    if (NetworkPolicy::replyExceedsSizeLimit(reply, limit)) {
      qWarning() << "Favicon" << (isIcon ? "image" : "page")
                 << "exceeds size limit:" << limit << "bytes";
      if (!isIcon) {
        // An oversized discovery page is not useful. Try the conventional icon
        // directly; an oversized icon must not start another page/icon cycle.
        const QUrl iconUrl = url.resolved(QUrl(QStringLiteral("/favicon.ico")));
        requestResource(iconUrl, feedUrl, cntRequests + 1, true);
      }
    } else if ((reply->error() == QNetworkReply::NoError) || (reply->error() == QNetworkReply::UnknownContentError)) {
      QUrl redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (redirectionTarget.isValid()) {
        if ((cntRequests == 0) || (cntRequests == 1) || (cntRequests == 3)) {
          redirectionTarget = CredentialScope::redirectTarget(reply->url(), redirectionTarget);
          if (NetworkPolicy::isSafeRedirect(reply->url(), redirectionTarget))
            requestResource(redirectionTarget, feedUrl, cntRequests+2, isIcon);
        }
      } else {
        QByteArray data = reply->readAll();
        if (!data.isNull()) {
          if (!isIcon) {
            const QUrl iconUrl = faviconUrl(data, url);
            const QString linkFavicon = iconUrl.toString();
            if (!linkFavicon.isEmpty()) {
              qDebug() << "Favicon URL:" << LogFile::urlForLog(QUrl(linkFavicon));
              emit signalGet(iconUrl, feedUrl, cntRequests+1);
            }
            if (linkFavicon.isEmpty()) {
              if (!isIcon) {
                QString link = QString("%1://%2/favicon.ico").arg(url.scheme()).arg(url.host());
                emit signalGet(link, feedUrl, cntRequests+1);
              }
            }
          } else {
            // Emit receiced data in main thread
            QFileInfo info(url.path());
            emit signalIconRecived(feedUrl, data, info.suffix());
          }
        } else {
          if (!isIcon) {
            QString link = QString("%1://%2/favicon.ico").arg(url.scheme()).arg(url.host());
            emit signalGet(link, feedUrl, cntRequests+1);
          }
        }
      }
    } else {
      if (reply->errorString().contains("Service Temporarily Unavailable")) {
        if (!hostList_.contains(QUrl(feedUrl).host())) {
          hostList_.append(QUrl(feedUrl).host());
        }
      }

      if ((cntRequests == 0) || (cntRequests == 1)) {
        QString link = QString("%1://%2").arg(url.scheme()).arg(url.host());
        emit signalGet(link, feedUrl, 2);
        qDebug() << "Request Url error: " << LogFile::urlForLog(reply->url()) << reply->errorString();
      }
    }
  } else {
    // Timed-out replies have already been retired; their abort completion
    // must not consume another request with the same URL.
    reply->deleteLater();
    return;
  }

  reply->abort();
  reply->deleteLater();
}

/** @brief Timeout to delete requests without answer from server
 *----------------------------------------------------------------------------*/
void FaviconObject::slotRequestTimeout()
{
  if (shuttingDown_) return;

  for (int i = activeRequests_.count() - 1; i >= 0; i--) {
    int time = activeRequests_.at(i).timeLeft - 1;
    if (time <= 0) {
      const ActiveRequest active = activeRequests_.takeAt(i);
      QNetworkReply *reply = active.reply;
      const QUrl url = reply->url();

      // abort() can invoke finished() before returning. The request is retired.
      reply->abort();
      reply->deleteLater();

      if (active.count == 0) {
        emit signalGet(url, active.feedUrl, 2);
      }
    } else {
      activeRequests_[i].timeLeft = time;
    }
  }
}

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
#include "faviconobject.h"
#include "networkpolicy.h"
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
  , networkManager_(NULL)
{
  setObjectName("faviconObject_");

  timeout_ = new QTimer(this);
  timeout_->setInterval(1000);
  connect(timeout_, SIGNAL(timeout()), this, SLOT(slotRequestTimeout()));

  getUrlTimer_ = new QTimer(this);
  getUrlTimer_->setSingleShot(true);
  getUrlTimer_->setInterval(20);
  connect(getUrlTimer_, SIGNAL(timeout()), this, SLOT(getQueuedUrl()));

  connect(this, SIGNAL(signalGet(QUrl,QString,int)),
          SLOT(slotGet(QUrl,QString,int)));
}

void FaviconObject::disconnectObjects()
{
  disconnect(this);
  if (networkManager_)
    networkManager_->disconnect(networkManager_);
}

/** @brief Put requested URL in request queue
 *----------------------------------------------------------------------------*/
void FaviconObject::requestUrl(QString urlString, QString feedUrl)
{
  if (!networkManager_) {
    networkManager_ = new NetworkManager(true, this);
    connect(networkManager_, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(finished(QNetworkReply*)));
  }

  if (!timeout_->isActive())
    timeout_->start();

  urlsQueue_.enqueue(urlString);
  feedsQueue_.enqueue(feedUrl);

  if (!getUrlTimer_->isActive())
    getUrlTimer_->start();
}

/** @brief Process request queue by timer
 *----------------------------------------------------------------------------*/
void FaviconObject::getQueuedUrl()
{
  if (currentFeeds_.count() >= REPLY_MAX_COUNT) {
    getUrlTimer_->start();
    return;
  }

  if (!urlsQueue_.isEmpty()) {
    getUrlTimer_->start();

    QString feedUrl = feedsQueue_.head();

    if (hostList_.contains(QUrl(feedUrl).host())) {
      foreach (QString url, currentFeeds_) {
        if (QUrl(url).host() == QUrl(feedUrl).host()) {
          return;
        }
      }
    }

    QString urlString = urlsQueue_.dequeue();
    feedUrl = feedsQueue_.dequeue();

    QUrl url = QUrl::fromEncoded(urlString.toUtf8());
    url.setUrl(QString("%1://%2").arg(url.scheme()).arg(url.host()));
    if (!url.isValid()) {
      url = QUrl::fromEncoded(feedUrl.toUtf8());
      url.setUrl(QString("%1://%2").arg(url.scheme()).arg(url.host()));
    }
    emit signalGet(url, feedUrl, 0);
  }
}

/** @brief Prepare and send network request to receive all data
 *----------------------------------------------------------------------------*/
void FaviconObject::slotGet(const QUrl &getUrl, const QString &feedUrl, const int &count)
{
  if (count)
    Common::sleep(30);

  QNetworkRequest request(getUrl);
  request.setRawHeader("User-Agent", globals.userAgent().toUtf8());

  currentUrls_.append(getUrl);
  currentFeeds_.append(feedUrl);
  currentCntRequests_.append(count);
  currentTime_.append(REQUEST_TIMEOUT);

  QNetworkReply *reply = networkManager_->get(request);
  reply->setProperty("feedReply", QVariant(true));
  requestUrl_.append(reply->url());
  networkReply_.append(reply);
}

/** @brief Finish network request processing
 *----------------------------------------------------------------------------*/
void FaviconObject::finished(QNetworkReply *reply)
{
  int currentReplyIndex = currentUrls_.indexOf(reply->url());
  if (currentReplyIndex >= 0) {
    currentTime_.removeAt(currentReplyIndex);
    QUrl url = currentUrls_.takeAt(currentReplyIndex);
    QString feedUrl = currentFeeds_.takeAt(currentReplyIndex);
    int cntRequests = currentCntRequests_.takeAt(currentReplyIndex);

    if ((reply->error() == QNetworkReply::NoError) || (reply->error() == QNetworkReply::UnknownContentError)) {
      QUrl redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (redirectionTarget.isValid()) {
        if ((cntRequests == 0) || (cntRequests == 1) || (cntRequests == 3)) {
          redirectionTarget = reply->url().resolved(redirectionTarget);
          if (NetworkPolicy::isSafeRedirect(reply->url(), redirectionTarget))
            emit signalGet(redirectionTarget, feedUrl, cntRequests+2);
        }
      } else {
        QByteArray data = reply->readAll();
        if (!data.isNull()) {
          if ((cntRequests == 0) || (cntRequests == 2)) {
            const QUrl iconUrl = faviconUrl(data, url);
            const QString linkFavicon = iconUrl.toString();
            if (!linkFavicon.isEmpty()) {
              qDebug() << "Favicon URL:" << linkFavicon;
              emit signalGet(iconUrl, feedUrl, cntRequests+1);
            }
            if (linkFavicon.isEmpty()) {
              if ((cntRequests == 0) || (cntRequests == 2)) {
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
          if ((cntRequests == 0) || (cntRequests == 2)) {
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
        qDebug() << "Request Url error: " << reply->url().toString() << reply->errorString();
      }
    }
  } else {
    qCritical() << "Request Url error: " << reply->url().toString() << reply->errorString();
  }

  int replyIndex = requestUrl_.indexOf(reply->url());
  if (replyIndex >= 0) {
    requestUrl_.removeAt(replyIndex);
    networkReply_.removeAt(replyIndex);
  }
  reply->abort();
  reply->deleteLater();
}

/** @brief Timeout to delete requests without answer from server
 *----------------------------------------------------------------------------*/
void FaviconObject::slotRequestTimeout()
{
  for (int i = currentTime_.count() - 1; i >= 0; i--) {
    int time = currentTime_.at(i) - 1;
    if (time <= 0) {
      QUrl url = currentUrls_.takeAt(i);
      QString feedUrl = currentFeeds_.takeAt(i);
      int cntRequests = currentCntRequests_.takeAt(i);
      currentTime_.removeAt(i);

      int replyIndex = requestUrl_.indexOf(url);
      if (replyIndex >= 0) {
        requestUrl_.removeAt(replyIndex);
        QNetworkReply *reply = networkReply_.takeAt(replyIndex);
        reply->deleteLater();

        if (cntRequests == 0) {
          emit signalGet(url, feedUrl, 2);
        }
      }
    } else {
      currentTime_.replace(i, time);
    }
  }
}

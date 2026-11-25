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
#include "common/logfile.h"
#include "faviconobject.h"
#include <utility>
#include "network/httprequestqueue.h"
#include "network/networkmanager.h"
#include "network/networkpolicy.h"
#include "network/credentialscope.h"
#include "app/mainapplication.h"
#include "app/globals.h"
#include "common/common.h"

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
QUrl siteRoot(const QUrl &url)
{
  QUrl root = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
  root.setPath(QStringLiteral("/"));
  return root;
}

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
  , queue_(new HttpRequestQueue(20, REPLY_MAX_COUNT, REQUEST_TIMEOUT, this))
{
  setObjectName("faviconObject_");
}

void FaviconObject::disconnectObjects()
{
  queue_->setShuttingDown(true);
  stopRequest();
  disconnect(this);
  queue_->disconnectManager(nullptr);
}

void FaviconObject::stopRequest()
{
  queue_->stop(true);
  activeMeta_.clear();
}

/** @brief Put requested URL in request queue
 *----------------------------------------------------------------------------*/
QUrl FaviconObject::initialPageUrl(const QString &websiteUrl, const QString &feedUrl)
{
  const QUrl website = QUrl::fromEncoded(websiteUrl.trimmed().toUtf8());
  if (NetworkPolicy::isHttpUrl(website)) return siteRoot(website);
  const QUrl feed = QUrl::fromEncoded(feedUrl.trimmed().toUtf8());
  return NetworkPolicy::isHttpUrl(feed) ? siteRoot(feed) : QUrl();
}

void FaviconObject::requestUrl(QString urlString, QString feedUrl)
{
  requestUrlWithKey(urlString, feedUrl, feedUrl);
}

void FaviconObject::requestUrlWithKey(QString urlString, QString feedUrl, QString resultKey)
{
  if (queue_->isShuttingDown()) return;

  if (!queue_->networkManager()) {
    NetworkManager *nm = queue_->ensureNetworkManager();
    connect(nm, &QNetworkAccessManager::finished, this, &FaviconObject::finished);
  }

  const QUrl pageUrl = initialPageUrl(urlString, feedUrl);
  const RequestIdentity identity{QUrl(feedUrl).host(), resultKey};
  queue_->enqueue(identity.feedHost, [this, pageUrl, identity] {
    if (pageUrl.isEmpty()) {
      emit signalIconFailed(identity.resultKey);
      return;
    }
    requestResource(pageUrl, identity, 0, false);
  });
}

void FaviconObject::requestResource(const QUrl &getUrl, const RequestIdentity &identity,
                                   int count, bool isIcon)
{
  if (queue_->isShuttingDown()) return;

  if (count > 0) {
    queue_->scheduleRetry(identity.feedHost, 30, [this, getUrl, identity, count, isIcon] {
      dispatchResource(getUrl, identity, count, isIcon);
    });
    return;
  }

  dispatchResource(getUrl, identity, count, isIcon);
}

void FaviconObject::dispatchResource(const QUrl &getUrl, const RequestIdentity &identity,
                                     int count, bool isIcon)
{
  if (queue_->isShuttingDown()) return;

  QNetworkRequest request(getUrl);

  QNetworkReply *reply = queue_->networkManager()->get(request);
  emit signalConnecting(identity.resultKey, REQUEST_TIMEOUT);
  connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, resultKey = identity.resultKey] {
    if (!queue_->containsReply(reply) || reply->property("faviconConnected").toBool())
      return;
    reply->setProperty("faviconConnected", true);
    emit signalConnected(resultKey);
  });
  reply->setProperty("feedReply", QVariant(true));
  reply->setProperty("faviconImage", isIcon);
  activeMeta_.insert(reply, {identity, count});
  queue_->trackReply(
      identity.feedHost, reply,
      [this, reply](const QUrl &url) {
        const ActiveRequestMeta meta = activeMeta_.take(reply);
        if (meta.count == 0) {
          requestResource(url, meta.identity, 2, false);
        } else {
          emit signalIconFailed(meta.identity.resultKey, true);
        }
      },
      [this, reply, resultKey = identity.resultKey](int secondsLeft) {
        if (!reply->property("faviconConnected").toBool())
          emit signalConnecting(resultKey, secondsLeft);
      });
  NetworkPolicy::limitReplySize(reply, isIcon ? FaviconImageLimit : FaviconPageLimit);
}

/** @brief Finish network request processing
 *----------------------------------------------------------------------------*/
void FaviconObject::finished(QNetworkReply *reply)
{
  if (queue_->takeReply(reply)) {
    // Retries dispatch directly here, so retire the old reply first.
    const ActiveRequestMeta active = activeMeta_.take(reply);
    const QUrl url = reply->url();
    const RequestIdentity &identity = active.identity;
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
        requestResource(iconUrl, identity, cntRequests + 1, true);
      } else {
        emit signalIconFailed(identity.resultKey);
      }
    } else if ((reply->error() == QNetworkReply::NoError) || (reply->error() == QNetworkReply::UnknownContentError)) {
      QUrl redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (redirectionTarget.isValid()) {
        if ((cntRequests == 0) || (cntRequests == 1) || (cntRequests == 3)) {
          redirectionTarget = CredentialScope::redirectTarget(reply->url(), redirectionTarget);
          if (NetworkPolicy::isSafeRedirect(reply->url(), redirectionTarget))
            requestResource(redirectionTarget, identity, cntRequests+2, isIcon);
          else
            emit signalIconFailed(identity.resultKey);
        } else {
          emit signalIconFailed(identity.resultKey);
        }
      } else {
        QByteArray data = reply->readAll();
        if (!data.isNull()) {
          if (!isIcon) {
            const QUrl iconUrl = faviconUrl(data, url);
            if (!iconUrl.isEmpty())
              qDebug() << "Favicon URL:" << LogFile::urlForLog(iconUrl);
            const QUrl target = iconUrl.isEmpty() ? url.resolved(QUrl(QStringLiteral("/favicon.ico"))) : iconUrl;
            requestResource(target, identity, cntRequests + 1, true);
          } else {
            // Emit receiced data in main thread
            QFileInfo info(url.path());
            emit signalIconRecived(identity.resultKey, data, info.suffix());
          }
        } else {
          if (!isIcon) {
            const QUrl iconUrl = url.resolved(QUrl(QStringLiteral("/favicon.ico")));
            requestResource(iconUrl, identity, cntRequests + 1, true);
          } else {
            emit signalIconFailed(identity.resultKey);
          }
        }
      }
    } else {
      queue_->markHostIfRateLimited(identity.feedHost, reply);

      if ((cntRequests == 0) || (cntRequests == 1)) {
        requestResource(siteRoot(url), identity, 2, false);
        qDebug() << "Request Url error: " << LogFile::urlForLog(reply->url()) << reply->errorString();
      } else {
        emit signalIconFailed(identity.resultKey);
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

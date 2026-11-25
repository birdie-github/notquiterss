// SPDX-License-Identifier: GPL-3.0-or-later
#include "httprequestqueue.h"

#include "network/networkmanager.h"

#include <QNetworkReply>
#include <utility>

HttpRequestQueue::HttpRequestQueue(int dispatchIntervalMs, int maxInFlight,
                                   int timeoutSeconds, QObject *parent)
  : QObject(parent)
  , maxInFlight_(maxInFlight)
  , timeoutSeconds_(timeoutSeconds)
{
  timeoutTimer_ = new QTimer(this);
  timeoutTimer_->setInterval(1000);
  connect(timeoutTimer_, &QTimer::timeout, this, &HttpRequestQueue::onTimeoutTick);

  dispatchTimer_ = new QTimer(this);
  dispatchTimer_->setSingleShot(true);
  dispatchTimer_->setInterval(dispatchIntervalMs);
  connect(dispatchTimer_, &QTimer::timeout, this, &HttpRequestQueue::processQueue);
}

NetworkManager *HttpRequestQueue::ensureNetworkManager()
{
  if (!networkManager_) {
    networkManager_ = new NetworkManager(true, this);
  }
  return networkManager_;
}

void HttpRequestQueue::enqueue(const QString &host,
                               std::function<void()> onDispatch,
                               std::function<void()> onCancel)
{
  if (shuttingDown_) return;

  ensureNetworkManager();

  if (!timeoutTimer_->isActive())
    timeoutTimer_->start();

  requestQueue_.enqueue({host, std::move(onDispatch), std::move(onCancel)});

  if (!dispatchTimer_->isActive())
    dispatchTimer_->start();
}

void HttpRequestQueue::scheduleRetry(const QString &host, int delayMs,
                                     std::function<void()> onDispatch,
                                     std::function<void()> onCancel)
{
  if (shuttingDown_) return;

  const quint64 token = ++nextRetryToken_;
  pendingRetries_.append({token, host, std::move(onDispatch), std::move(onCancel)});
  QTimer::singleShot(delayMs, this, [this, token] {
    if (shuttingDown_) return;
    for (int i = 0; i < pendingRetries_.size(); ++i) {
      if (pendingRetries_.at(i).token == token) {
        const PendingRetry retry = pendingRetries_.takeAt(i);
        if (retry.onDispatch) retry.onDispatch();
        return;
      }
    }
  });
}

void HttpRequestQueue::trackReply(const QString &host, QNetworkReply *reply,
                                  std::function<void(const QUrl &)> onTimeout,
                                  std::function<void(int)> onTick)
{
  activeRequests_.append({host, timeoutSeconds_, reply, std::move(onTimeout), std::move(onTick)});
}

int HttpRequestQueue::indexOfReply(QNetworkReply *reply) const
{
  for (int i = 0; i < activeRequests_.size(); ++i) {
    if (activeRequests_.at(i).reply == reply)
      return i;
  }
  return -1;
}

bool HttpRequestQueue::containsReply(QNetworkReply *reply) const
{
  return indexOfReply(reply) >= 0;
}

bool HttpRequestQueue::takeReply(QNetworkReply *reply)
{
  const int index = indexOfReply(reply);
  if (index < 0) return false;
  activeRequests_.removeAt(index);
  return true;
}

bool HttpRequestQueue::markHostIfRateLimited(const QString &host, const QNetworkReply *reply)
{
  if (!reply || !reply->errorString().contains(QLatin1String("Service Temporarily Unavailable")))
    return false;
  if (rateLimitedHosts_.contains(host))
    return false;
  rateLimitedHosts_.insert(host);
  return true;
}

void HttpRequestQueue::stop(bool detachActive)
{
  dispatchTimer_->stop();
  timeoutTimer_->stop();

  QQueue<QueuedEntry> queued = std::exchange(requestQueue_, {});
  while (!queued.isEmpty()) {
    const QueuedEntry entry = queued.dequeue();
    if (entry.onCancel) entry.onCancel();
  }

  const QList<PendingRetry> retries = std::exchange(pendingRetries_, {});
  for (const PendingRetry &retry : retries) {
    if (retry.onCancel) retry.onCancel();
  }

  if (detachActive) {
    const QList<ActiveEntry> active = std::exchange(activeRequests_, {});
    for (const ActiveEntry &entry : active) {
      if (entry.reply) {
        entry.reply->abort();
        entry.reply->deleteLater();
      }
    }
  } else {
    // abort() may synchronously invoke finished() and mutate activeRequests_.
    QList<QNetworkReply *> replies;
    replies.reserve(activeRequests_.size());
    for (const ActiveEntry &entry : std::as_const(activeRequests_)) {
      replies.append(entry.reply);
    }
    for (QNetworkReply *reply : std::as_const(replies)) {
      if (reply) reply->abort();
    }
  }
}

void HttpRequestQueue::disconnectManager(QObject *receiver)
{
  if (!networkManager_) return;
  if (receiver)
    networkManager_->disconnect(receiver);
  else
    networkManager_->disconnect(networkManager_);
}

void HttpRequestQueue::processQueue()
{
  if (shuttingDown_) return;

  const int inFlightCount = activeRequests_.count() + pendingRetries_.count();
  if (inFlightCount >= maxInFlight_) {
    dispatchTimer_->start();
    return;
  }

  if (requestQueue_.isEmpty()) return;

  dispatchTimer_->start();
  const QString &candidateHost = requestQueue_.head().host;
  if (rateLimitedHosts_.contains(candidateHost)) {
    for (const ActiveEntry &entry : std::as_const(activeRequests_)) {
      if (entry.host == candidateHost) return;
    }
    for (const PendingRetry &retry : std::as_const(pendingRetries_)) {
      if (retry.host == candidateHost) return;
    }
  }

  const QueuedEntry entry = requestQueue_.dequeue();
  if (entry.onDispatch) entry.onDispatch();
}

void HttpRequestQueue::onTimeoutTick()
{
  if (shuttingDown_) return;

  for (int i = activeRequests_.count() - 1; i >= 0; --i) {
    const int time = activeRequests_.at(i).timeLeft - 1;
    if (time <= 0) {
      const ActiveEntry active = activeRequests_.takeAt(i);
      QNetworkReply *reply = active.reply;
      const QUrl replyUrl = reply ? reply->url() : QUrl();
      if (active.onTick) active.onTick(0);
      // Remove bookkeeping before abort(): finished() may run synchronously.
      if (reply) {
        reply->abort();
        reply->deleteLater();
      }
      if (active.onTimeout) active.onTimeout(replyUrl);
    } else {
      activeRequests_[i].timeLeft = time;
      if (activeRequests_.at(i).onTick) activeRequests_.at(i).onTick(time);
    }
  }
}

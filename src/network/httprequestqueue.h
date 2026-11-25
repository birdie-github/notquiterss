// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HTTPREQUESTQUEUE_H
#define HTTPREQUESTQUEUE_H

#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>

class NetworkManager;
class QNetworkReply;

// Shared per-host rate-limiting, queue dispatch, retry-token scheduling,
// and timeout tracking for asynchronous HTTP worker objects.
class HttpRequestQueue : public QObject
{
  Q_OBJECT
public:
  explicit HttpRequestQueue(int dispatchIntervalMs, int maxInFlight,
                            int timeoutSeconds, QObject *parent = nullptr);

  NetworkManager *ensureNetworkManager();
  NetworkManager *networkManager() const { return networkManager_; }

  void setShuttingDown(bool shuttingDown) { shuttingDown_ = shuttingDown; }
  bool isShuttingDown() const { return shuttingDown_; }

  int queuedCount() const { return requestQueue_.count(); }

  void enqueue(const QString &host,
               std::function<void()> onDispatch,
               std::function<void()> onCancel = {});

  void scheduleRetry(const QString &host, int delayMs,
                     std::function<void()> onDispatch,
                     std::function<void()> onCancel = {});

  void trackReply(const QString &host, QNetworkReply *reply,
                  std::function<void(const QUrl &)> onTimeout,
                  std::function<void(int)> onTick = {});

  bool containsReply(QNetworkReply *reply) const;
  bool takeReply(QNetworkReply *reply);

  // Records host for single-in-flight throttling on 503 responses.
  // Returns true if host was newly added.
  bool markHostIfRateLimited(const QString &host, const QNetworkReply *reply);

  void stop(bool detachActive);
  void disconnectManager(QObject *receiver);

private slots:
  void processQueue();
  void onTimeoutTick();

private:
  struct QueuedEntry {
    QString host;
    std::function<void()> onDispatch;
    std::function<void()> onCancel;
  };

  struct PendingRetry {
    quint64 token = 0;
    QString host;
    std::function<void()> onDispatch;
    std::function<void()> onCancel;
  };

  struct ActiveEntry {
    QString host;
    int timeLeft = 0;
    QNetworkReply *reply = nullptr;
    std::function<void(const QUrl &)> onTimeout;
    std::function<void(int)> onTick;
  };

  int indexOfReply(QNetworkReply *reply) const;

  NetworkManager *networkManager_ = nullptr;
  bool shuttingDown_ = false;
  quint64 nextRetryToken_ = 0;
  int maxInFlight_ = 0;
  int timeoutSeconds_ = 0;

  QTimer *timeoutTimer_ = nullptr;
  QTimer *dispatchTimer_ = nullptr;

  QQueue<QueuedEntry> requestQueue_;
  QList<ActiveEntry> activeRequests_;
  QList<PendingRetry> pendingRetries_;
  QSet<QString> rateLimitedHosts_;
};

#endif // HTTPREQUESTQUEUE_H

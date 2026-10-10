#ifndef NETWORKPOLICY_H
#define NETWORKPOLICY_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

namespace NetworkPolicy {
inline bool isHttpUrl(const QUrl &url)
{
  return url.isValid() && !url.host().isEmpty() &&
      (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

inline bool isSafeRedirect(const QUrl &source, const QUrl &target)
{
  return isHttpUrl(target) &&
      !(source.scheme() == QLatin1String("https") && target.scheme() == QLatin1String("http"));
}

// Keep explicit local-file feeds usable, but never redirect network requests to them.
inline bool isRequestUrl(const QUrl &url)
{
  return isHttpUrl(url) || (url.isValid() && url.isLocalFile() && url.host().isEmpty());
}

// These callers leave the body unread until finished(), so bytesAvailable()
// measures the actual buffered response, including Qt's HTTP decompression.
inline bool replyExceedsSizeLimit(QNetworkReply *reply, qint64 limit)
{
  if (reply->property("responseSizeLimitExceeded").toBool() ||
      reply->bytesAvailable() > limit) return true;
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  // Redirect/error bodies may describe a different resource; only use the
  // advertised length as an early hint for a successful response or local feed.
  return ((status >= 200 && status < 300) || reply->url().isLocalFile()) &&
      reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit;
}

inline void limitReplySize(QNetworkReply *reply, qint64 limit)
{
  // One byte beyond the allowance lets readyRead detect overflow even when
  // the server omits Content-Length. Do not drain the body into a second buffer.
  reply->setReadBufferSize(limit + 1);
  auto check = [reply, limit] {
    if (!replyExceedsSizeLimit(reply, limit)) return;
    // Set the reason before abort(): finished() can be emitted synchronously.
    reply->setProperty("responseSizeLimitExceeded", true);
    if (!reply->isFinished()) reply->abort();
  };
  QObject::connect(reply, &QIODevice::readyRead, reply, check);
  QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, check);
  check();
}

class RejectedReply : public QNetworkReply
{
public:
  RejectedReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request,
                QObject *parent) : QNetworkReply(parent)
  {
    setOperation(operation);
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);
    setError(QNetworkReply::ProtocolUnknownError,
             tr("Unsupported URL scheme: %1").arg(request.url().scheme()));
    QTimer::singleShot(0, this, [this]() { complete(); });
  }

  void abort() override
  {
    if (isFinished()) return;
    setError(QNetworkReply::OperationCanceledError, tr("Operation canceled"));
    complete();
  }

protected:
  qint64 readData(char *, qint64) override { return -1; }

private:
  void complete()
  {
    if (isFinished()) return;
    setFinished(true);
    emit errorOccurred(error());
    emit finished();
  }
};
}

#endif // NETWORKPOLICY_H

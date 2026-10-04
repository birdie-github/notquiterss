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
#include "articleimages.h"
#include "articlecontent.h"
#include "mainapplication.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QNetworkCookieJar>
#include <QBuffer>
#include <QImageReader>
#include <QTimer>
#include <QPointer>
#include <cstring>
#include "logfile.h"
#include <QDebug>
#include <QSslSocket>
#include <QSslError>
#include <QSslConfiguration>
#include <QStringList>

namespace {
QByteArray imageAcceptHeader() {
  const QList<QByteArray> formats = QImageReader::supportedImageFormats();
  QStringList types;
  // Only advertise formats allowed by the article policy and decodable by Qt.
  // CDNs may negotiate WebP even for URLs ending in .jpg.
  if (formats.contains("png")) types.append("image/png");
  if (formats.contains("jpeg") || formats.contains("jpg")) types.append("image/jpeg");
  if (formats.contains("gif")) types.append("image/gif");
  if (formats.contains("webp")) types.append("image/webp");
  if (formats.contains("bmp")) types.append("image/bmp");
  return types.join(",").toLatin1();
}
class NoCookies : public QNetworkCookieJar {
public:
  explicit NoCookies(QObject *parent) : QNetworkCookieJar(parent) {}
  bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
};
class ImageReply : public QNetworkReply {
public:
  ImageReply(const QNetworkRequest &request, QNetworkAccessManager *fetcher, bool allowed, QObject *parent)
    : QNetworkReply(parent), fetcher_(fetcher), pending_(nullptr), offset_(0), redirects_(0), done_(false) {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this, allowed]() {
      if (done_) return;
      if (allowed) fetch(url());
      else finish(QNetworkReply::ContentAccessDenied, "Article resource blocked");
    });
    QTimer::singleShot(30000, this, [this]() { if (!done_) finish(QNetworkReply::TimeoutError, "Article image timed out"); });
  }
  ~ImageReply() override {
    if (pending_) { pending_->disconnect(this); pending_->abort(); pending_->deleteLater(); }
  }
  void abort() override { if (!done_) finish(QNetworkReply::OperationCanceledError, "Article image canceled"); }
  qint64 bytesAvailable() const override { return (done_ ? body_.size() - offset_ : 0) + QNetworkReply::bytesAvailable(); }
  bool isSequential() const override { return true; }
protected:
  qint64 readData(char *data, qint64 maxSize) override {
    if (!done_) return 0;
    if (!offset_) ArticleImages::trace("article reader reads " + ArticleImages::describeUrl(url()) +
                                      " bytes=" + QString::number(body_.size()));
    const qint64 size = qMin(maxSize, qint64(body_.size()) - offset_);
    if (size <= 0) return done_ ? -1 : 0;
    std::memcpy(data, body_.constData() + offset_, size);
    offset_ += size;
    return size;
  }
private:
  void finish(NetworkError error = NoError, const QString &message = QString()) {
    if (done_) return;
    ArticleImages::trace("reply " + ArticleImages::describeUrl(url()) +
                         " error=" + QString::number(int(error)) + " " + message +
                         " bytes=" + QString::number(body_.size()));
    done_ = true;
    if (pending_) {
      pending_->disconnect(this);
      if (pending_->isRunning()) pending_->abort();
      pending_->deleteLater();
      pending_ = nullptr;
    }
    if (error != NoError) { body_.clear(); setError(error, message); }
    setFinished(true);
    if (error != NoError) emit errorOccurred(error);
    else { emit metaDataChanged(); emit readyRead(); }
    emit finished();
  }
  void fetch(const QUrl &target) {
    if (done_) return;
    ArticleImages::trace("fetch " + ArticleImages::describeUrl(target));
    QNetworkRequest request(target);
    if (target.scheme() == QLatin1String("https")) {
      // The article fetcher is independent of the feed NetworkManager and its
      // lazy certificate initialization. Use platform roots explicitly for each
      // HTTPS request, including redirects; never bypass peer verification.
      QSslConfiguration ssl = request.sslConfiguration();
      ssl.setProtocol(QSsl::TlsV1_2OrLater);
      ssl.setCaCertificates(QSslConfiguration::systemCaCertificates());
      ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
      request.setSslConfiguration(ssl);
      ArticleImages::trace("TLS request-ca-count=" + QString::number(ssl.caCertificates().size()) +
                           " verify-mode=" + QString::number(int(ssl.peerVerifyMode())));
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    // Some publishers reject Qt's generic default User-Agent. Identify the reader
    // explicitly without impersonating a browser or sharing feed credentials.
    const QByteArray userAgent = QCoreApplication::applicationName().toUtf8() + QByteArray("/") + QCoreApplication::applicationVersion().toLatin1();
    request.setRawHeader("User-Agent", userAgent);
    ArticleImages::trace("User-Agent=" + QString::fromLatin1(userAgent));
    const QByteArray accept = imageAcceptHeader();
    request.setRawHeader("Accept", accept);
    ArticleImages::trace("Accept=" + QString::fromLatin1(accept));
    // New request: never carry feed credentials, Referer, cookies, or publisher headers.
    pending_ = fetcher_->get(request);
    connect(pending_.data(), &QNetworkReply::sslErrors, this,
            [this](const QList<QSslError> &errors) {
      if (!pending_) return;
      for (const QSslError &error : errors) {
        ArticleImages::trace("TLS validation " + ArticleImages::describeUrl(pending_->url()) +
                             " code=" + QString::number(int(error.error())) +
                             " message=" + error.errorString());
      }
    });
    connect(pending_.data(), &QNetworkReply::readyRead, this, [this]() {
      if (!pending_) return;
      body_ += pending_->readAll();
      if (body_.size() > 16 * 1024 * 1024) finish(QNetworkReply::ContentAccessDenied, "Article image exceeds size limit");
    });
    connect(pending_.data(), &QNetworkReply::finished, this, [this]() {
      if (!pending_ || done_) return;
      ArticleImages::trace("response " + ArticleImages::describeUrl(pending_->url()) +
                           " status=" + pending_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toString() +
                           " content-type=" + pending_->header(QNetworkRequest::ContentTypeHeader).toString() +
                           " error=" + QString::number(int(pending_->error())));
      if (pending_->error() != NoError) { finish(pending_->error(), pending_->errorString()); return; }
      const QUrl redirect = pending_->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (!redirect.isEmpty()) {
        const QUrl target = pending_->url().resolved(redirect);
        if (++redirects_ > 5 || !ArticleContent::isRemoteImage(target) ||
            (pending_->url().scheme() == "https" && target.scheme() != "https")) {
          finish(QNetworkReply::ContentAccessDenied, "Article image redirect blocked"); return;
        }
        pending_->deleteLater(); pending_ = nullptr; body_.clear();
        fetch(target); return;
      }
      body_ += pending_->readAll();
      if (body_.size() > 16 * 1024 * 1024) { finish(QNetworkReply::ContentAccessDenied, "Article image exceeds size limit"); return; }
      QBuffer buffer(&body_); buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer);
      const QByteArray format = reader.format().toLower();
      const QSize size = reader.size();
      ArticleImages::trace("image " + ArticleImages::describeUrl(pending_->url()) +
                           " format=" + QString::fromLatin1(format) +
                           " size=" + QString::number(size.width()) + "x" + QString::number(size.height()) +
                           " bytes=" + QString::number(body_.size()) + " decoder=" + reader.errorString());
      if (!(format == "png" || format == "jpeg" || format == "gif" || format == "webp" || format == "bmp") ||
          !size.isValid() || qint64(size.width()) * size.height() > 64 * 1024 * 1024) {
        finish(QNetworkReply::ContentAccessDenied, "Unsupported article image"); return;
      }
      setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
      setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QStringLiteral("OK"));
      setHeader(QNetworkRequest::ContentTypeHeader, "image/" + QString::fromLatin1(format));
      setHeader(QNetworkRequest::ContentLengthHeader, body_.size());
      finish();
    });
  }
  QNetworkAccessManager *fetcher_;
  QPointer<QNetworkReply> pending_;
  QByteArray body_;
  qint64 offset_;
  int redirects_;
  bool done_;
};
}
ArticleImages::ArticleImages(QObject *parent) : QNetworkAccessManager(parent), fetcher_(new QNetworkAccessManager(this)) {
  if (tracingEnabled()) {
    const QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    trace(QString("TLS default-ca-count=%1 system-ca-count=%2 protocol=%3 verify-mode=%4")
          .arg(ssl.caCertificates().size()).arg(QSslConfiguration::systemCaCertificates().size())
          .arg(int(ssl.protocol())).arg(int(ssl.peerVerifyMode())));
    QStringList formats;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
      formats.append(QString::fromLatin1(format));
    trace(QString("Qt=%1 SSL=%2 SSL-library=%3 image-formats=%4")
          .arg(QString::fromLatin1(qVersion())).arg(QSslSocket::supportsSsl())
          .arg(QSslSocket::sslLibraryVersionString(), formats.join(",")));
  }
  fetcher_->setCookieJar(new NoCookies(fetcher_));
  fetcher_->setProxy(mainApp->networkProxy());
}
void ArticleImages::reset() {
  trace("reset image allowlist and cancel pending image replies");
  allowed_.clear();
  const auto replies = findChildren<QNetworkReply *>(QString(), Qt::FindDirectChildrenOnly);
  for (QNetworkReply *reply : replies) reply->abort();
}
QNetworkReply *ArticleImages::createRequest(Operation operation, const QNetworkRequest &request, QIODevice *) {
  const QUrl url = request.url();
  const bool trustedIcon = url.scheme() == "qrc" && url.path().startsWith("/images/");
  const bool allowed = operation == GetOperation &&
    (trustedIcon || ArticleContent::isInlineImage(url) ||
     (allowed_.contains(url) && ArticleContent::isRemoteImage(url)));
  fetcher_->setProxy(mainApp->networkProxy());
  trace("request " + describeUrl(url) + " operation=" + QString::number(int(operation)) +
        " listed=" + QString::number(allowed_.contains(url)) + " permitted=" + QString::number(allowed));
  return new ImageReply(request, fetcher_, allowed, this);
}

bool ArticleImages::tracingEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("IMAGE_DEBUG");
  return enabled || LogFile::consoleLoggingEnabled();
}
void ArticleImages::trace(const QString &message) {
  if (!tracingEnabled()) return;
  qInfo().noquote() << QStringLiteral("[article-images] %1").arg(message.simplified());
}
QString ArticleImages::describeUrl(const QUrl &url) {
  if (url.scheme() == "data") return QStringLiteral("data:[inline image]");
  return url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment).toDisplayString();
}

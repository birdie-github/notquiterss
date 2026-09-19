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
/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2013  David Rosca <nowrep@gmail.com>
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
#include "downloaditem.h"

#include "mainapplication.h"
#include "networkmanager.h"
#include "networkpolicy.h"

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

DownloadItem::DownloadItem(QListWidgetItem *item,
                           QNetworkReply *reply,
                           const QString &fileName,
                           bool openAfterDownload)
  : QWidget()
  , item_(item)
  , reply_(reply)
  , redirectCount_(0)
  , fileName_(fileName)
  , downloadUrl_(reply->url())
  , downloading_(false)
  , openAfterFinish_(openAfterDownload)
  , downloadStopped_(false)
  , curSpeed_(0)
  , received_(0)
  , total_(0)
{
  downloadTimer_.start();

  outputFile_.setFileName(fileName);

  qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
  if (total > 0) total_ = total;

  fileNameLabel_ = new QLabel();
  fileNameLabel_->setStyleSheet("background: none;");
  QFileInfo info(fileName);
  fileNameLabel_->setText(info.fileName());
  QFont font = fileNameLabel_->font();
  font.setBold(true);
  fileNameLabel_->setFont(font);

  progressBar_ = new QProgressBar();
  progressBar_->setObjectName("progressBar_");
  progressBar_->setTextVisible(false);
  progressBar_->setFixedHeight(10);
  progressBar_->setFixedWidth(300);
  progressBar_->setMinimum(0);
  progressBar_->setMaximum(0);
  progressBar_->setValue(0);

  QHBoxLayout *progressLayout = new QHBoxLayout();
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->addWidget(progressBar_);
  progressLayout->addStretch();

  progressFrame_ = new QFrame();
  progressFrame_->setLayout(progressLayout);

  downloadInfo_ = new QLabel();
  downloadInfo_->setStyleSheet("background: none;");
  downloadInfo_->setText(tr("Remaining time unavailable"));

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(5, 5, 5, 5);
  mainLayout->setSpacing(5);
  mainLayout->addWidget(fileNameLabel_);
  mainLayout->addWidget(downloadInfo_);
  mainLayout->addWidget(progressFrame_);
  setLayout(mainLayout);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(customContextMenuRequested(QPoint)),
          this, SLOT(customContextMenuRequested(QPoint)));
  connect(&updateInfoTimer_, SIGNAL(timeout()), this, SLOT(updateInfo()));
}

DownloadItem::~DownloadItem()
{
  discardReply();
  delete item_;
}

void DownloadItem::discardReply()
{
  if (!reply_) return;
  QNetworkReply *old = reply_;
  reply_ = nullptr;
  disconnect(old, nullptr, this, nullptr);
  if (!old->isFinished()) old->abort();
  old->deleteLater();
}

void DownloadItem::fail(const QString &message)
{
  stop(false);
  downloadInfo_->setText(tr("Error: ") + message);
}

void DownloadItem::startDownloading()
{
  if (!reply_ || downloadStopped_) return;
  downloading_ = true;
  if (!NetworkPolicy::isHttpUrl(reply_->url())) {
    fail(tr("Only HTTP and HTTPS downloads are supported."));
    return;
  }
  reply_->setParent(this);
  reply_->setProperty("downloadReply", true);
  QNetworkReply *connectedReply = reply_;
  connect(reply_, &QNetworkReply::readyRead, this, [this, connectedReply]() {
    if (reply_ == connectedReply) readyRead();
  });
  connect(reply_, &QNetworkReply::downloadProgress, this,
          [this, connectedReply](qint64 received, qint64 total) {
    if (reply_ == connectedReply) downloadProgress(received, total);
  });
  connect(reply_, &QNetworkReply::errorOccurred, this, [this, connectedReply](QNetworkReply::NetworkError) {
    if (reply_ == connectedReply) error();
  });
  connect(reply_, &QNetworkReply::metaDataChanged, this, [this, connectedReply]() {
    if (reply_ == connectedReply) metaDataChanged();
  });
  connect(reply_, &QNetworkReply::finished, this, [this, connectedReply]() {
    if (reply_ == connectedReply) finished();
  });
  updateInfoTimer_.start(1000);
  if (reply_->error() != QNetworkReply::NoError) {
    error();
    return;
  }
  if (followRedirect()) return;
  readyRead();
  // A reply can finish while the save dialog is open, before we connect it.
  QNetworkReply *current = reply_;
  if (current && current->isFinished()) {
    QTimer::singleShot(0, this, [this, current]() {
      if (reply_ == current && downloading_) finished();
    });
  }
}

bool DownloadItem::followRedirect()
{
  if (!reply_ || !downloading_) return false;
  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
    return false;
  const QUrl location = reply_->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
  const QUrl target = reply_->url().resolved(location);
  if (location.isEmpty() || !NetworkPolicy::isHttpUrl(target)) {
    fail(tr("Invalid redirect or unsupported redirect scheme; only HTTP and HTTPS are allowed."));
    return true;
  }
  if (!NetworkPolicy::isSafeRedirect(reply_->url(), target)) {
    fail(tr("Redirect from HTTPS to HTTP is not allowed."));
    return true;
  }
  if (++redirectCount_ > 10) {
    fail(tr("Too many redirects."));
    return true;
  }
  // A fresh request avoids forwarding credentials or sensitive headers across hosts.
  QNetworkRequest request(target);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  discardReply();
  received_ = 0;
  total_ = 0;
  curSpeed_ = 0;
  downloadTimer_.restart();
  reply_ = mainApp->networkManager()->get(request);
  startDownloading();
  return true;
}

void DownloadItem::readyRead()
{
  if (!downloading_ || !reply_ || reply_->error() != QNetworkReply::NoError) return;
  if (followRedirect()) return;
  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  // Never save a redirect/error response body as the requested file.
  if (status < 200 || status >= 300) return;
  if (!outputFile_.isOpen() && !outputFile_.open(QIODevice::WriteOnly)) {
    fail(tr("Cannot write to file!"));
    return;
  }
  const QByteArray data = reply_->readAll();
  if (outputFile_.write(data) != data.size()) fail(tr("Cannot write to file!"));
}

void DownloadItem::downloadProgress(qint64 received, qint64 total)
{
  if (!downloading_ || !reply_) return;
  if (followRedirect()) return;
  progressBar_->setMaximum(total > 0 ? 100 : 0);
  progressBar_->setValue(total > 0 ? received * 100 / total : 0);
  total_ = total;
  curSpeed_ = received * 1000.0 / qMax<qint64>(1, downloadTimer_.elapsed());
  received_ = received;
}

void DownloadItem::metaDataChanged()
{
  if (downloading_) followRedirect();
}

void DownloadItem::error()
{
  if (downloading_ && reply_ && reply_->error() != QNetworkReply::NoError)
    fail(reply_->errorString());
}

void DownloadItem::finished()
{
  if (!downloading_ || !reply_) return;
  if (reply_->error() != QNetworkReply::NoError) {
    error();
    return;
  }
  if (followRedirect()) return;
  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status < 200 || status >= 300) {
    fail(tr("Unexpected HTTP response: %1").arg(status));
    return;
  }
  readyRead();
  if (!downloading_) return;
  if (!outputFile_.flush()) {
    fail(tr("Cannot write to file!"));
    return;
  }
  received_ = outputFile_.size();
  total_ = received_;
  outputFile_.close();
  updateInfoTimer_.stop();
  downloading_ = false;
  discardReply();
  downloadInfo_->setText(QString("%1 - %2 - %3").arg(fileSizeToString(received_), downloadUrl_.host(),
                                                   QTime::currentTime().toString()));
  progressFrame_->hide();
  item_->setSizeHint(sizeHint());
  if (openAfterFinish_) openFile();
  emit downloadFinished(true);
}

QString DownloadItem::remaingTimeToString(QTime time)
{
  if (time < QTime(0, 0, 10)) {
    return tr("a few seconds");
  } else if (time < QTime(0, 1)) {
    return time.toString("s") + " " + tr("seconds");
  } else if (time < QTime(1, 0)) {
    return time.toString("m") + " " + tr("minutes");
  } else {
    return time.toString("h") + " " + tr("hours");
  }
}

QString DownloadItem::fileSizeToString(qint64 size)
{
  if (size < 0) {
    return tr("Unknown size");
  }
  double correctSize = size / 1024.0; // KB
  if (correctSize < 1000) {
    return QString::number(correctSize > 1 ? correctSize : 1, 'f', 0) + " KB";
  }
  correctSize /= 1024; // MB
  if (correctSize < 1000) {
    return QString::number(correctSize, 'f', 1) + " MB";
  }
  correctSize /= 1024; // GB
  return QString::number(correctSize, 'f', 2) + " GB";
}

QString DownloadItem::currentSpeedToString(double speed)
{
  if (speed < 0) {
    return tr("Unknown speed");
  }
  speed /= 1024; // kB
  if (speed < 1000) {
    return QString::number(speed, 'f', 0) + " kB/s";
  }
  speed /= 1024; //MB
  if (speed < 1000) {
    return QString::number(speed, 'f', 2) + " MB/s";
  }
  speed /= 1024; //GB
  return QString::number(speed, 'f', 2) + " GB/s";
}

void DownloadItem::updateInfo()
{
  int estimatedTime = curSpeed_ > 0 && total_ >= received_
      ? qMin(double(24 * 60 * 60 - 1), (total_ - received_) / curSpeed_) : 0;
  QString speed = currentSpeedToString(curSpeed_);

  QTime time(0, 0);
  time = time.addSecs(estimatedTime);
  QString remTime = remaingTimeToString(time);
  remTime_ = time;

  QString curSize = fileSizeToString(received_);
  QString fileSize = fileSizeToString(total_);

  if (fileSize == tr("Unknown size")) {
    downloadInfo_->setText(tr("%2 - size unknown (%3)").arg(curSize, speed));
  } else {
    downloadInfo_->setText(tr("%1 remaining - %2 of %3 (%4)").arg(remTime, curSize, fileSize, speed));
  }
}

void DownloadItem::stop(bool askForDeleteFile)
{
  if (!downloading_ || downloadStopped_)
    return;

  downloadStopped_ = true;
  QString host = downloadUrl_.host();

  openAfterFinish_ = false;
  updateInfoTimer_.stop();
  downloading_ = false;
  discardReply();

  const bool createdFile = outputFile_.isOpen();
  outputFile_.close();
  QString outputfile = QFileInfo(outputFile_).absoluteFilePath();
  downloadInfo_->setText(tr("Cancelled - %1").arg(host));
  progressFrame_->hide();
  item_->setSizeHint(sizeHint());

  emit downloadFinished(false);

  if (askForDeleteFile && createdFile) {
    QMessageBox::StandardButton button =
        QMessageBox::question(item_->listWidget()->parentWidget(),
                              tr("Delete file"),
                              tr("Do you also want to delete the downloaded file?"),
                              QMessageBox::Yes | QMessageBox::No);
    if (button == QMessageBox::Yes) {
      QFile::remove(outputfile);
    }
  }
}

/*virtual*/ void DownloadItem::mouseDoubleClickEvent(QMouseEvent* event)
{
  openFile();
  event->accept();
}

void DownloadItem::customContextMenuRequested(const QPoint &pos)
{
  QMenu menu;
  menu.addAction( tr("Open File"), this, SLOT(openFile()));

  menu.addAction(tr("Open Folder"), this, SLOT(openFolder()));
  menu.addSeparator();
  menu.addAction(tr("Copy Download Link"), this, SLOT(copyDownloadLink()));
  menu.addSeparator();
  menu.addAction(tr("Cancel Download"), this, SLOT(stop()))->setEnabled(downloading_);
  menu.addAction(tr("Remove"), this, SLOT(clear()))->setEnabled(!downloading_);

  if (downloading_ || downloadInfo_->text().startsWith(tr("Cancelled")) || downloadInfo_->text().startsWith(tr("Error"))) {
    menu.actions().at(0)->setEnabled(false);
  }
  menu.exec(mapToGlobal(pos));
}

void DownloadItem::copyDownloadLink()
{
  QApplication::clipboard()->setText(downloadUrl_.toString());
}

void DownloadItem::clear()
{
  emit deleteItem(this);
}

void DownloadItem::openFile()
{
  if (downloading_ || downloadStopped_) {
    return;
  }
  QFileInfo info(fileName_);
  if (info.exists()) {
    qInfo() << "Opening" << info.absoluteFilePath() << "using system file handler";
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
  } else {
    QMessageBox::warning(item_->listWidget()->parentWidget(),
                         tr("Not found"),
                         tr("The file was not found:\n%1").arg(info.absoluteFilePath()));
  }
}

void DownloadItem::openFolder()
{
#ifdef Q_OS_WIN
  QString winFileName = fileName_;
  winFileName.replace(QLatin1Char('/'), "\\");
  QString shExArg = "/e,/select,\"" + winFileName + "\"";
  qInfo() << "Launching" << QStringLiteral("explorer.exe") << "arguments:" << shExArg;
  ShellExecute(NULL, NULL, TEXT("explorer.exe"), (wchar_t*)shExArg.utf16(), NULL, SW_SHOW);
#else
  QFileInfo info(fileName_);
  qInfo() << "Opening" << info.path() << "using system folder handler";
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.path()));
#endif
}

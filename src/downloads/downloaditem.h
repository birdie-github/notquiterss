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
#ifndef DOWNLOADITEM_H
#define DOWNLOADITEM_H

#include <QtWidgets>
#include <QTimer>
#include <QElapsedTimer>
#include <QNetworkReply>

class QListWidgetItem;

class DownloadItem : public QWidget
{
  Q_OBJECT
public:
  explicit DownloadItem(QListWidgetItem *item, QNetworkReply *reply,
                        const QString &fileName, bool openAfterDownload);
  ~DownloadItem();

  void startDownloading();
  bool isDownloading() { return downloading_; }
  QTime remainingTime() { return remTime_; }
  static QString remaingTimeToString(QTime time);
  static QString currentSpeedToString(double speed);

signals:
  void deleteItem(DownloadItem*);
  void downloadFinished(bool success);

protected:
  virtual void mouseDoubleClickEvent(QMouseEvent*);

private slots:
  void finished();
  void metaDataChanged();
  void updateInfo();
  void downloadProgress(qint64 received, qint64 total);
  void stop(bool askForDeleteFile = true);
  void openFile();
  void openFolder();
  void readyRead();
  void error();
  void customContextMenuRequested(const QPoint &pos);
  void clear();

  void copyDownloadLink();

private:
  QString fileSizeToString(qint64 size);
  void discardReply();
  bool followRedirect();
  void fail(const QString &message);

  QListWidgetItem *item_;
  QNetworkReply *reply_;
  int redirectCount_;
  QString fileName_;
  QElapsedTimer downloadTimer_;
  QTime remTime_;
  QTimer updateInfoTimer_;
  QFile outputFile_;
  QUrl downloadUrl_;

  bool downloading_;
  bool openAfterFinish_;
  bool downloadStopped_;
  double curSpeed_;
  qint64 received_;
  qint64 total_;

  QLabel *fileNameLabel_;
  QProgressBar *progressBar_;
  QFrame *progressFrame_;
  QLabel *downloadInfo_;
};

#endif // DOWNLOADITEM_H

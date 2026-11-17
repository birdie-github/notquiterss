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
#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QtWidgets>
#include <QNetworkAccessManager>

class DownloadItem;

class DownloadManager : public QWidget
{
  Q_OBJECT

public:
  explicit DownloadManager(QWidget *parent = 0);
  ~DownloadManager();

  void download(const QNetworkRequest &request);
  void handleUnsupportedContent(QNetworkReply *reply, bool askDownloadLocation);
  void startExternalApp(const QString &executable, const QUrl &url);
  void retranslateStrings();

public slots:

signals:
  void signalItemCreated(QListWidgetItem* item, DownloadItem* downItem);
  void signalShowDownloads(bool activate);
  void signalUpdateInfo(const QString &text);

private slots:
  QString getFileName(QNetworkReply* reply);
  void itemCreated(QListWidgetItem* item, DownloadItem* downItem);
  void clearList();
  void deleteItem(DownloadItem* item);
  void updateInfo();

private:
  QListWidget *listWidget_;
  QAction *listClaerAct_;
  QTimer updateInfoTimer_;

};

#endif // DOWNLOADMANAGER_H

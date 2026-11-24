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
#ifndef UPDATEAPPDIALOG_H
#define UPDATEAPPDIALOG_H

#include <QNetworkReply>

#include "widgets/dialog.h"
#include "network/networkmanagerproxy.h"

class UpdateAppDialog : public Dialog
{
  Q_OBJECT
public:
  explicit UpdateAppDialog(QWidget *parent, bool show = true);
  ~UpdateAppDialog();

  void disconnectObjects();

signals:
  void signalNewVersion(const QString &newVersion = QString());

private slots:
  void finishUpdatesChecking();
  void renderStatistics();

private:
  bool showDialog_;

  NetworkManagerProxy *networkManagerProxy_;
  QNetworkReply *reply_;

  QLabel *infoLabel;
  QTextBrowser *history_;

  QCheckBox *remindAboutVersion_;

};

#endif // UPDATEAPPDIALOG_H

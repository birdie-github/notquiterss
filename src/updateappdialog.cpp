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
#include "updateappdialog.h"
#include "articlecontent.h"
#include <QDesktopServices>

#include "mainapplication.h"
#include "projectmetadata.h"
#include "releaseinfo.h"
#include <QTimer>
#include "settings.h"


UpdateAppDialog::UpdateAppDialog(QWidget *parent, bool show)
  : Dialog(parent)
  , showDialog_(show)
{
  Settings settings;

  networkManagerProxy_ = new NetworkManagerProxy(this);

  if (showDialog_) {
    setWindowTitle(tr("Check for Updates"));
    setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setObjectName("UpdateAppDialog");
    resize(450, 350);

    infoLabel = new QLabel(tr("Checking for updates..."), this);
    infoLabel->setOpenExternalLinks(false);
    connect(infoLabel, &QLabel::linkActivated, this, [](const QString &link) {
      const QUrl url(link);
      if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
    });

    history_ = new QTextBrowser(this);
    history_->setObjectName("history_");
    history_->setText(tr("Loading history..."));
    history_->setOpenExternalLinks(false);
    history_->setOpenLinks(false);
    connect(history_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
      if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
    });

    remindAboutVersion_ = new QCheckBox(tr("Don't remind me about this version"), this);
    remindAboutVersion_->setChecked(settings.value("remindAboutVersion", false).toBool());
    remindAboutVersion_->hide();

    pageLayout->addWidget(infoLabel, 0);
    pageLayout->addWidget(history_, 1);
    pageLayout->addWidget(remindAboutVersion_, 0);

    buttonBox->addButton(QDialogButtonBox::Close);

    connect(this, SIGNAL(finished(int)), this, SLOT(closeDialog()));

    restoreGeometry(settings.value("updateAppDlg/geometry").toByteArray());
  }
  // Let the caller connect the background completion signal before any outcome.
  QTimer::singleShot(0, this, &UpdateAppDialog::renderStatistics);
}

UpdateAppDialog::~UpdateAppDialog()
{

}

void UpdateAppDialog::disconnectObjects()
{
  disconnect(this);
  if (!networkManagerProxy_) return;
  networkManagerProxy_->disconnectObjects();

  delete networkManagerProxy_;
  networkManagerProxy_ = nullptr;
}

void UpdateAppDialog::closeDialog()
{
  Settings settings;
  settings.setValue("remindAboutVersion", remindAboutVersion_->isChecked());
  settings.setValue("updateAppDlg/geometry", saveGeometry());
}

void UpdateAppDialog::finishUpdatesChecking()
{
  reply_->deleteLater();

  QString info;
  QString newVersion;
  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray response = reply_->readAll();
  const ReleaseInfo release = response.size() <= 1024 * 1024
      ? parseReleaseInfo(response, QCoreApplication::applicationVersion()) : ReleaseInfo();
  if (reply_->error() == QNetworkReply::NoError && status == 200 && release.valid) {
    if (release.newer) {
      newVersion = release.version;
      info = tr("A new version of %1 is available!").arg(QGuiApplication::applicationDisplayName().toHtmlEscaped())
          + QString("<p><a href=\"%1\">%2</a></p>")
              .arg(ProjectMetadata::releasesUrl().toHtmlEscaped(), tr("Click here to go to the download page"));
    } else {
      info = tr("You already have the latest version.");
    }
    info.prepend(QString("<p>%1 <b>%2</b><br>%3 <b>%4</b></p>")
                     .arg(tr("You're running version"), QCoreApplication::applicationVersion().toHtmlEscaped(),
                          tr("The latest version is"), release.version.toHtmlEscaped()));
    if (showDialog_) history_->setPlainText(release.notes);
  } else if (status == 404) {
    info = tr("No published release is available at the configured update endpoint.");
    if (showDialog_) history_->clear();
  } else {
    qWarning() << "Error checking updates" << status << reply_->errorString();
    info = tr("Error checking for updates");
    if (showDialog_) history_->clear();
  }

  Settings settings;
  bool remind = settings.value("remindAboutVersion", false).toBool();
  QString currentVersion = settings.value("currentVersionApp", "").toString();
  if (!showDialog_) {
    if (!newVersion.isEmpty()) {
      if (currentVersion != newVersion) {
        settings.setValue("currentVersionApp", newVersion);
        settings.setValue("remindAboutVersion", false);
      } else if (remind) {
          newVersion = "";
      }
    }

    emit signalNewVersion(newVersion);
  } else {
    infoLabel->setText(info);


    if (!newVersion.isEmpty()) {
      if (currentVersion != newVersion) {
        settings.setValue("currentVersionApp", newVersion);
        settings.setValue("remindAboutVersion", false);
        remindAboutVersion_->setChecked(false);
        remindAboutVersion_->show();
      } else if (!remind) {
          remindAboutVersion_->show();
      }
    }
  }
}

void UpdateAppDialog::renderStatistics()
{
  if (!networkManagerProxy_) return; // Shutdown may precede the queued check.
  bool updateCheckEnabled = AppSettings::updateCheckEnabled.get();
  if (updateCheckEnabled || showDialog_) {
    QNetworkRequest request{QUrl(ProjectMetadata::updateEndpoint())};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", (QCoreApplication::applicationName() + "/" +
                                        QCoreApplication::applicationVersion()).toUtf8());
    request.setTransferTimeout(15000);
    reply_ = networkManagerProxy_->get(request);
    connect(reply_, &QIODevice::readyRead, this, [this]() {
      if (reply_->bytesAvailable() > 1024 * 1024) reply_->abort();
    });
    connect(reply_, SIGNAL(finished()), this, SLOT(finishUpdatesChecking()));
  } else {
    emit signalNewVersion();
  }
}

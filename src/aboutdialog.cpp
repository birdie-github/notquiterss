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
#include "common.h"
#include "aboutdialog.h"
#include "articlecontent.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include "mainapplication.h"
#include "settings.h"
#include "projectmetadata.h"
#include "globals.h"

#include <sqlite3.h>

AboutDialog::AboutDialog(QWidget *parent) :
  Dialog(parent, Qt::MSWindowsFixedSizeDialogHint)
{
  setWindowTitle(tr("About"));
  setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setObjectName("AboutDialog");
  setMinimumWidth(480);

  QTabWidget *tabWidget = new QTabWidget();

  QString revisionStr;
  if (!ProjectMetadata::revision().isEmpty()) {
      revisionStr = "<BR>" + tr("Revision") + " " + QString("%1").arg(ProjectMetadata::revision());
  }
  QString appInfo =
      "<html><style>a { color: blue; text-decoration: none; }</style><body>"
      "<CENTER>"
      "<IMG SRC=\":/images/images/logo.png\">"
      "<BR><B>" + QGuiApplication::applicationDisplayName().toHtmlEscaped() + "</B>"
      "<P>"
      + tr("Version") + " " + "<B>" + QString(QCoreApplication::applicationVersion()) + "</B>" + QString(" (%1)").arg(ProjectMetadata::releaseDate())
      + revisionStr
      + "</P>"
      + "<BR>"
      + tr("%1 is an open-source, cross-platform RSS/Atom news feed reader").arg(QGuiApplication::applicationDisplayName().toHtmlEscaped())
      + "<P>" + tr("Includes:")
      + QString(" Qt-%1, SQLite-%2").
      arg(QT_VERSION_STR).arg(SQLITE_VERSION)
      + "</P>"
      + "<P>" + ProjectMetadata::originalCopyright().toHtmlEscaped() + "</P>"
      + "<P>" + ProjectMetadata::copyright().toHtmlEscaped() + "</P>"
      + QString("<P><a href=\"%1\">%2</a> · <a href=\"%3\">%4</a></P>")
          .arg(ProjectMetadata::homepageUrl().toHtmlEscaped(), tr("Homepage"),
               QString("mailto:") + ProjectMetadata::email().toHtmlEscaped(), tr("E-mail"))
      + QString("<P><a href=\"%1\">%2</a> · <a href=\"%3\">%4</a></P>")
          .arg(ProjectMetadata::releasesUrl().toHtmlEscaped(), tr("Releases"),
               ProjectMetadata::originalProjectUrl().toHtmlEscaped(), tr("Original project"))
      + "</CENTER></body></html>";
  QLabel *infoLabel = new QLabel(appInfo);
  infoLabel->setOpenExternalLinks(false);
  connect(infoLabel, &QLabel::linkActivated, this, [](const QString &link) {
    const QUrl url(link);
    if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
  });
  infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);

  QHBoxLayout *mainLayout = new QHBoxLayout();
  mainLayout->addWidget(infoLabel);
  QWidget *mainWidget = new QWidget();
  mainWidget->setLayout(mainLayout);

  QTextEdit *authorsTextEdit = new QTextEdit(this);
  authorsTextEdit->setReadOnly(true);
  authorsTextEdit->setText(Common::readAllFileContents(":/file/AUTHORS"));

  QHBoxLayout *authorsLayout = new QHBoxLayout();
  authorsLayout->addWidget(authorsTextEdit);
  QWidget *authorsWidget = new QWidget();
  authorsWidget->setLayout(authorsLayout);

  QTextEdit *licenseTextEdit = new QTextEdit();
  licenseTextEdit->setReadOnly(true);
  licenseTextEdit->setText(Common::readAllFileContents(":/file/COPYING").section("-----", 1, 1));

  QHBoxLayout *licenseLayout = new QHBoxLayout();
  licenseLayout->addWidget(licenseTextEdit);
  QWidget *licenseWidget = new QWidget();
  licenseWidget->setLayout(licenseLayout);

  QString portable;
  if (mainApp->isPortable()) {
    if (!mainApp->isPortableAppsCom())
      portable = "(Portable)";
    else
      portable = "(PortableApps)";
  }
  Settings settings;
  const QString overridesPath = globals.overrides().path();
  const bool overridesMissing = !QFileInfo::exists(overridesPath);
  QString overridesTooltip;
  if (overridesMissing) {
    const QString sample = QDir::cleanPath(
        QDir(mainApp->resourcesDir()).filePath("overrides.ini.sample"));
    overridesTooltip = tr("File not found. Copy %1 to %2, then edit it and restart the application.")
        .arg(QDir::toNativeSeparators(sample), QDir::toNativeSeparators(overridesPath));
  } else if (!globals.overrides().error().isEmpty()) {
    overridesTooltip = globals.overrides().error();
  } else if (!globals.overrides().loaded()) {
    overridesTooltip = tr("Restart the application to load this file.");
  }
  QString information =
      "<table border=\"0\"><tr>"
      "<td>" + tr("Version") + " </td>"
      "<td>" + QString("%1.%2 %3 %4").arg(QCoreApplication::applicationVersion()).arg(ProjectMetadata::revision()).arg(portable).arg(ProjectMetadata::releaseDate()) + "</td>"
      "</tr><tr></tr><tr>"
      "<td>" + tr("Application directory:") + " </td>"
      "<td>" + QCoreApplication::applicationDirPath() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Resource directory:") + " </td>"
      "<td>" + mainApp->resourcesDir() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Data directory:") + " </td>"
      "<td>" + mainApp->dataDir() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Backup directory:") + " </td>"
      "<td>" + mainApp->dataDir() + ("/" + ProjectMetadata::backup()) + "</td>"
      "</tr><tr></tr><tr>"
      "<td>" + tr("Database file:") + " </td>"
      "<td>" + mainApp->dbFileName() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Settings file:") + " </td>"
      "<td>" + settings.fileName() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Website overrides:") + " </td>"
      "<td>" + overridesPath.toHtmlEscaped() + "</td>"
      "</tr><tr>"
      "<td>" + tr("Log file:") + " </td>"
      "<td>" + mainApp->dataDir() + ("/" + ProjectMetadata::log()) + "</td>"
      "</tr></table>";

  QTextEdit *informationTextEdit = new QTextEdit();
  informationTextEdit->setReadOnly(true);
  informationTextEdit->setText(information);
  // QTextEdit displays character-format tooltips on hover. Keep the path
  // selectable and avoid making it a link merely to provide a tooltip.
  QTextCursor overridesCursor = informationTextEdit->document()->find(overridesPath);
  if (!overridesCursor.isNull()) {
    QTextCharFormat format;
    format.setFontStrikeOut(overridesMissing);
    if (!overridesTooltip.isEmpty())
      format.setToolTip(QStringLiteral("<qt>") +
                        overridesTooltip.toHtmlEscaped() + QStringLiteral("</qt>"));
    overridesCursor.mergeCharFormat(format);
  }

  QHBoxLayout *informationLayout = new QHBoxLayout();
  informationLayout->addWidget(informationTextEdit);

  QWidget *informationWidget = new QWidget();
  informationWidget->setLayout(informationLayout);

  tabWidget->addTab(mainWidget, tr("Version"));
  tabWidget->addTab(authorsWidget, tr("Authors"));
  tabWidget->addTab(licenseWidget, tr("License"));
  tabWidget->addTab(informationWidget, tr("Information"));

  pageLayout->addWidget(tabWidget);

  buttonBox->addButton(QDialogButtonBox::Close);
}

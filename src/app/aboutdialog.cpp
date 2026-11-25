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
#include "common/common.h"
#include "aboutdialog.h"
#include "articles/rendering/articlecontent.h"
#ifdef NQRSS_ENABLE_DONATE_BUTTON
#include "widgets/donatebutton.h"
#endif
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include "mainapplication.h"
#include "preferences/settings.h"
#include "projectmetadata.h"
#include "globals.h"

#include <sqlite3.h>

AboutDialog::AboutDialog(QWidget *parent) :
  Dialog(parent, Qt::MSWindowsFixedSizeDialogHint)
{
  setWindowTitle(tr("About"));
  setObjectName("AboutDialog");
  setMinimumWidth(480);

  QTabWidget *tabWidget = new QTabWidget();

  const auto link = [](const QString &url, const QString &label) {
    return QStringLiteral(R"(<a href="%1">%2</a>)")
        .arg(url.toHtmlEscaped(), label.toHtmlEscaped());
  };
  const QString appName = QGuiApplication::applicationDisplayName().toHtmlEscaped();
  const QString version = QStringLiteral("%1 <b>%2</b> (%3)")
      .arg(tr("Version").toHtmlEscaped(), QCoreApplication::applicationVersion().toHtmlEscaped(),
           ProjectMetadata::releaseDate().toHtmlEscaped());
  const QString revision = ProjectMetadata::revision();
  const QString revisionLine = revision.isEmpty() ? QString()
      : QStringLiteral("<br>%1 %2").arg(tr("Revision").toHtmlEscaped(), revision.toHtmlEscaped());
  const QString dependencies = tr("Uses ").toHtmlEscaped()
      + QStringLiteral("Qt-%1, SQLite-%2").arg(QT_VERSION_STR, SQLITE_VERSION);
  const QString originalProjectName = QStringLiteral("QuiteRSS Project");
  QString originalCredit = ProjectMetadata::originalCopyright().toHtmlEscaped();
  originalCredit.replace(originalProjectName,
                         link(ProjectMetadata::originalProjectUrl(), originalProjectName));
  const QStringList links{
    link(ProjectMetadata::homepageUrl(), tr("Homepage")),
    link(QStringLiteral("mailto:") + ProjectMetadata::email(), tr("E-mail")),
    link(ProjectMetadata::releasesUrl(), tr("Releases"))
  };

  const QString appInfo = QStringLiteral(R"html(
<html>
<head><style>a { color: blue; text-decoration: none; }</style></head>
<body>
<center>
  <img src=":/images/application256" width="128" height="128">
  <br><b>%1</b>
  <p>%2%3</p>
  <br>%4
  <p>%5</p>
  <p>%6</p>
  <p>%7</p>
  <p>%8</p>
</center>
</body>
</html>
)html").arg(appName, version, revisionLine,
            tr("%1 is an open-source, cross-platform RSS/Atom news feed reader")
                .arg(QGuiApplication::applicationDisplayName()).toHtmlEscaped(),
            dependencies, originalCredit,
            ProjectMetadata::copyright().toHtmlEscaped(), links.join(QStringLiteral(" · ")));
  QLabel *infoLabel = new QLabel(appInfo);
  infoLabel->setOpenExternalLinks(false);
  connect(infoLabel, &QLabel::linkActivated, this, [](const QString &link) {
    const QUrl url(link);
    if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
  });
  infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);

  auto *mainWidget = new QWidget();
  auto *versionLayout = new QVBoxLayout(mainWidget);
  versionLayout->addWidget(infoLabel, 1);

#ifdef NQRSS_ENABLE_DONATE_BUTTON
  auto *donateButton = new DonateButton(mainWidget);
  versionLayout->addWidget(donateButton, 0, Qt::AlignHCenter);
  connect(donateButton, &QPushButton::clicked, this, [this] {
    mainApp->openDonationPage(this);
  });
#endif

  tabWidget->addTab(mainWidget, tr("Version"));

  auto addReadOnlyTextTab = [&tabWidget](const QString &title) {
    auto *tab = new QWidget(tabWidget);
    auto *layout = new QHBoxLayout(tab);
    auto *edit = new QTextEdit(tab);
    edit->setReadOnly(true);
    layout->addWidget(edit);
    tabWidget->addTab(tab, title);
    return edit;
  };

  addReadOnlyTextTab(tr("Authors"))->setText(Common::readAllFileContents(":/file/AUTHORS"));

  QString supportersText = tr("Thank you to everyone who has supported NotQuiteRSS with a donation.");
  supportersText += QStringLiteral("\n");
  const auto supporterNames = Common::readAllFileContents(":/file/SUPPORTERS").split('\n');
  for (const QString &line : supporterNames) {
    const QString name = line.trimmed();
    if (!name.isEmpty())
      supportersText += QStringLiteral("\n• ") + name;
  }
  addReadOnlyTextTab(tr("Supporters"))->setPlainText(supportersText);

  addReadOnlyTextTab(tr("License"))->setPlainText(Common::readAllFileContents(":/file/COPYING"));

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

  QTextEdit *informationTextEdit = addReadOnlyTextTab(tr("Information"));
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

  pageLayout->addWidget(tabWidget);

  buttonBox->addButton(QDialogButtonBox::Close);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "browsersettingspage.h"

#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"
#include "projectmetadata.h"
#include "widgets/lineedit.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

BrowserSettingsPage::BrowserSettingsPage(QWidget *parent,
                                         QVector<OptionSearchEntry> *searchEntries,
                                         int pageIndex)
  : QTabWidget(parent)
{
  OptionsPageBuilder::populateTabs(this, pageIndex, searchEntries, {
    {optTr("Articles"), [this](OptionsPageBuilder &b) {
      b.check(autoLoadImages_, optTr("Load images"), QString(),
              QStringLiteral("pictures graphics media"));
      b.check(highQualityImageScaling_, optTr("Use Lanczos scaling for article images"),
              optTr("Scales article images using a 2-pass Lanczos-3 filter on a background thread"),
              QStringLiteral("pictures resize quality"));
      b.spinRow(preloadNewspaperPages_, optTr("Preload up to"), 0, 10,
                optTr("pages in newspaper mode"), optTr("0 disables image preloading"),
                QStringLiteral("cache prefetch images"));
      OptionsPageBuilder::bindEnabled(autoLoadImages_, {highQualityImageScaling_, preloadNewspaperPages_});

      b.row([this](OptionsRowBuilder &r) {
        const QString zoomLabel = optTr("Default article zoom:");
        r.label(zoomLabel);
        r.spin(defaultZoomPages_, 30, 300, QStringLiteral(" %"));
        r.registerSearch(defaultZoomPages_, zoomLabel, QString(),
                         QStringLiteral("scale magnify text size"));
      });
      b.spacing(12);

      b.label(optTr("Open links in:"));
      QRadioButton *systemBrowser = b.radio(optTr("System default web browser"),
                                            QStringLiteral("url link open"));
      b.radio(customExternalBrowser_, optTr("Custom web browser"),
              QStringLiteral("url link open"));
      auto *browserGroup = new QButtonGroup(this);
      browserGroup->addButton(systemBrowser);
      browserGroup->addButton(customExternalBrowser_);
      systemBrowser->setChecked(true);

      QPushButton *browseBrowser = b.pathRow(externalBrowser_, QString(), optTr("Browse..."));
#ifdef Q_OS_MAC
      externalBrowser_->setPlaceholderText(optTr("Application name or application bundle path"));
#else
      externalBrowser_->setPlaceholderText(optTr("Browser command, including any arguments"));
      externalBrowser_->setToolTip(optTr("Use double quotes around paths containing spaces. The article URL is appended automatically."));
#endif
      OptionsPageBuilder::bindEnabled(customExternalBrowser_, {externalBrowser_, browseBrowser});
      connect(browseBrowser, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, optTr("Select Browser"), QString(), optTr("All files (*)"),
            nullptr, QFileDialog::DontResolveSymlinks);
        if (!file.isEmpty()) externalBrowser_->setText(QDir::toNativeSeparators(file));
      });

      b.check(returnFocusAfterLink_,
              optTr("Return focus to %1 after opening a link").arg(ProjectMetadata::name()),
              optTr("Attempts to bring %1 back to the foreground after opening an article link in your browser. Your desktop may prevent this, particularly on Wayland.").arg(ProjectMetadata::name()),
              QStringLiteral("foreground window"));
      // clicked, rather than toggled: loading saved settings must not show a dialog.
      connect(returnFocusAfterLink_, &QAbstractButton::clicked, this, [this](bool checked) {
        if (!checked || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")) ||
            AppSettings::returnFocusWaylandWarningShown.get()) return;
        QMessageBox::warning(this, optTr("Returning focus"),
                             optTr("On Wayland, returning focus to %1 after opening a link may be unreliable or may not work at all.").arg(ProjectMetadata::name()),
                             QMessageBox::Ok);
        // Remember the acknowledgement even if the user cancels Options afterward.
        AppSettings::returnFocusWaylandWarningShown.set(true);
      });
      b.stretch();
    }},
    {optTr("Downloads"), [this](OptionsPageBuilder &b) {
      QPushButton *browse = b.pathRow(downloadLocationEdit_, optTr("Download location:"), optTr("Browse..."),
                                      QStringLiteral("save folder directory path"));
      connect(browse, &QAbstractButton::clicked, this, &BrowserSettingsPage::selectionDownloadLocation);
      b.check(askDownloadLocation_, optTr("Ask where to save each file before downloading"),
              QString(), QStringLiteral("prompt save as dialog"));
      b.stretch();
    }},
  });
}

bool BrowserSettingsPage::validate()
{
  if (customExternalBrowser_->isChecked() && externalBrowser_->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, optTr("External Browser"),
                         optTr("Enter a custom browser or select the system default browser."));
    return false;
  }
  return true;
}

void BrowserSettingsPage::selectionDownloadLocation()
{
  const QString dirStr = QFileDialog::getExistingDirectory(
      this, optTr("Open Directory..."),
      downloadLocationEdit_->text(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dirStr.isEmpty()) downloadLocationEdit_->setText(dirStr);
}

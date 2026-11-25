// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationssettingspage.h"

#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"
#include "widgets/lineedit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QGuiApplication>
#include <QPushButton>
#include <QSpinBox>

NotificationsSettingsPage::NotificationsSettingsPage(QWidget *parent,
                                                     QVector<OptionSearchEntry> *searchEntries,
                                                     int pageIndex)
  : QTabWidget(parent)
{
  OptionsPageBuilder::populateTabs(this, pageIndex, searchEntries, {
    {OptionsDialog::tr("Notification"), [this](OptionsPageBuilder &b) {
      b.group(showNotifyOn_, OptionsDialog::tr("Show notifications for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          const QString screenLabel = OptionsDialog::tr("Screen");
          r.label(screenLabel);
          QStringList screens{QStringLiteral("-1")};
          for (int i = 0; i < QGuiApplication::screens().size(); ++i) {
            screens << QString::number(i);
          }
          r.combo(screenNotify_, screens);
          screenNotify_->setCurrentIndex(1);
          r.registerSearch(screenNotify_, screenLabel, QString(), QStringLiteral("monitor display"));

          r.spacing(10);
          const QString positionLabel = OptionsDialog::tr("Position");
          r.label(positionLabel);
          r.combo(positionNotify_, {OptionsDialog::tr("Top Left"), OptionsDialog::tr("Top Right"),
                                    OptionsDialog::tr("Bottom Left"), OptionsDialog::tr("Bottom Right")});
          r.registerSearch(positionNotify_, positionLabel, QString(), QStringLiteral("corner location"));

          r.stretch(1);
          QPushButton *showNotifier = r.button(OptionsDialog::tr("Preview"));
          connect(showNotifier, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::previewNotificationRequested);
        }, std::nullopt, false);

#if defined(Q_OS_WIN)
        grp.check(fullscreenModeNotify_, OptionsDialog::tr("Do not show notification in fullscreen mode"));
#else
        fullscreenModeNotify_ = new QCheckBox(this);
        fullscreenModeNotify_->hide();
#endif
        grp.check(showNotifyInactiveApp_, OptionsDialog::tr("Show when the main window is inactive"));
        grp.check(closeNotify_, OptionsDialog::tr("Close notification after opening an article"));
        grp.spacing(10);

        grp.grid(std::nullopt, 2, [this](OptionsGridBuilder &g) {
          g.addSpinRow(0, OptionsDialog::tr("Transparency"), transparencyNotify_, 0, 100, QStringLiteral("%"),
                       QString(), QStringLiteral("opacity alpha"));
          g.addSpinRow(1, OptionsDialog::tr("Show at most"), countShowNewsNotify_, 1, 30,
                       OptionsDialog::tr("items per notification page"), QString(), QStringLiteral("limit alerts"));
          g.addSpinRow(2, OptionsDialog::tr("List item width"), widthTitleNewsNotify_, 50, 500, OptionsDialog::tr("pixels"),
                       QString(), QStringLiteral("size"));
          g.addSpinRow(3, OptionsDialog::tr("Close notification after"), timeShowNewsNotify_, 0, 99999,
                       OptionsDialog::tr("seconds"), QString(), QStringLiteral("duration timeout delay"));
        });
        grp.spacing(10);

        grp.check(showTitlesFeedsNotify_, OptionsDialog::tr("Show feed titles"));
        grp.check(showIconFeedNotify_, OptionsDialog::tr("Show feed icons"));
        grp.check(showButtonMarkAllNotify_, OptionsDialog::tr("Show button 'Mark All News Read'"));
        grp.check(showButtonMarkReadNotify_, OptionsDialog::tr("Show button 'Mark Read/Unread'"));
        grp.check(showButtonExBrowserNotify_, OptionsDialog::tr("Show button 'Open in External Browser'"));
        grp.check(showButtonDeleteNotify_, OptionsDialog::tr("Show button 'Delete News'"));
        grp.spacing(10);

        grp.row([this](OptionsRowBuilder &r) {
          r.check(onlySelectedFeeds_, OptionsDialog::tr("Only show selected feeds:"), QString(),
                  QStringLiteral("filter feeds alert"));
          QPushButton *feedsNotifierButton = r.button(OptionsDialog::tr("Feeds"));
          OptionsPageBuilder::bindEnabled(onlySelectedFeeds_, {feedsNotifierButton});
          connect(feedsNotifierButton, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::selectFeedsRequested);
          r.stretch(1);
        }, std::nullopt, false);
        grp.stretch(1);
      }, Qt::Vertical, QMargins(10, 10, 10, 10), QStringLiteral("popup toast alert banner"));
      showNotifyOn_->setChecked(false);
    }},
    {OptionsDialog::tr("Sound"), [this](OptionsPageBuilder &b) {
      b.group(soundNotifyBox_, OptionsDialog::tr("Play a sound for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          editSoundNotifier_ = new LineEdit();
          r.addWidget(editSoundNotifier_, 1);
          selectionSoundNotifier_ = r.button(OptionsDialog::tr("Browse..."));
          playSoundNotifier_ = r.button(OptionsDialog::tr("Play"));
          connect(selectionSoundNotifier_, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::selectionSoundNotifier);
          connect(playSoundNotifier_, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::slotPlaySoundNotifier);
        }, std::nullopt, false);
        grp.stretch(1);
      }, Qt::Vertical, QMargins(10, 10, 10, 10), QStringLiteral("audio wav mp3 flac alert chime"));
      soundNotifyBox_->setChecked(false);
    }},
  });
}

void NotificationsSettingsPage::selectionSoundNotifier()
{
  const QFileInfo file(editSoundNotifier_->text());
  const QString path = file.isFile() ? editSoundNotifier_->text() : file.path();

  const QString fileName = QFileDialog::getOpenFileName(
      this, OptionsDialog::tr("Open File..."), path,
      OptionsDialog::tr("Audio Files (*.wav *.flac *.mp3);;All Files (*)"));
  if (!fileName.isEmpty()) editSoundNotifier_->setText(fileName);
}

void NotificationsSettingsPage::slotPlaySoundNotifier()
{
  if (!editSoundNotifier_->text().isEmpty()) {
    emit playSoundRequested(editSoundNotifier_->text());
  }
}

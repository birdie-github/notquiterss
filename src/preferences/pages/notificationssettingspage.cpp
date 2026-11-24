// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationssettingspage.h"

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
    {optTr("Notification"), [this](OptionsPageBuilder &b) {
      b.group(showNotifyOn_, optTr("Show notifications for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          const QString screenLabel = optTr("Screen");
          r.label(screenLabel);
          QStringList screens{QStringLiteral("-1")};
          for (int i = 0; i < QGuiApplication::screens().size(); ++i) {
            screens << QString::number(i);
          }
          r.combo(screenNotify_, screens);
          screenNotify_->setCurrentIndex(1);
          r.registerSearch(screenNotify_, screenLabel, QString(), QStringLiteral("monitor display"));

          r.spacing(10);
          const QString positionLabel = optTr("Position");
          r.label(positionLabel);
          r.combo(positionNotify_, {optTr("Top Left"), optTr("Top Right"),
                                    optTr("Bottom Left"), optTr("Bottom Right")});
          r.registerSearch(positionNotify_, positionLabel, QString(), QStringLiteral("corner location"));

          r.stretch(1);
          QPushButton *showNotifier = r.button(optTr("Preview"));
          connect(showNotifier, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::previewNotificationRequested);
        }, std::nullopt, false);

#if defined(Q_OS_WIN)
        grp.check(fullscreenModeNotify_, optTr("Do not show notification in fullscreen mode"));
#else
        fullscreenModeNotify_ = new QCheckBox(this);
        fullscreenModeNotify_->hide();
#endif
        grp.check(showNotifyInactiveApp_, optTr("Show when the main window is inactive"));
        grp.check(closeNotify_, optTr("Close notification after opening an article"));
        grp.spacing(10);

        grp.grid(std::nullopt, 2, [this](OptionsGridBuilder &g) {
          g.addSpinRow(0, optTr("Transparency"), transparencyNotify_, 0, 100, QStringLiteral("%"),
                       QString(), QStringLiteral("opacity alpha"));
          g.addSpinRow(1, optTr("Show at most"), countShowNewsNotify_, 1, 30,
                       optTr("items per notification page"), QString(), QStringLiteral("limit alerts"));
          g.addSpinRow(2, optTr("List item width"), widthTitleNewsNotify_, 50, 500, optTr("pixels"),
                       QString(), QStringLiteral("size"));
          g.addSpinRow(3, optTr("Close notification after"), timeShowNewsNotify_, 0, 99999,
                       optTr("seconds"), QString(), QStringLiteral("duration timeout delay"));
        });
        grp.spacing(10);

        grp.check(showTitlesFeedsNotify_, optTr("Show feed titles"));
        grp.check(showIconFeedNotify_, optTr("Show feed icons"));
        grp.check(showButtonMarkAllNotify_, optTr("Show button 'Mark All News Read'"));
        grp.check(showButtonMarkReadNotify_, optTr("Show button 'Mark Read/Unread'"));
        grp.check(showButtonExBrowserNotify_, optTr("Show button 'Open in External Browser'"));
        grp.check(showButtonDeleteNotify_, optTr("Show button 'Delete News'"));
        grp.spacing(10);

        grp.row([this](OptionsRowBuilder &r) {
          r.check(onlySelectedFeeds_, optTr("Only show selected feeds:"), QString(),
                  QStringLiteral("filter feeds alert"));
          QPushButton *feedsNotifierButton = r.button(optTr("Feeds"));
          OptionsPageBuilder::bindEnabled(onlySelectedFeeds_, {feedsNotifierButton});
          connect(feedsNotifierButton, &QAbstractButton::clicked,
                  this, &NotificationsSettingsPage::selectFeedsRequested);
          r.stretch(1);
        }, std::nullopt, false);
        grp.stretch(1);
      }, Qt::Vertical, QMargins(10, 10, 10, 10), QStringLiteral("popup toast alert banner"));
      showNotifyOn_->setChecked(false);
    }},
    {optTr("Sound"), [this](OptionsPageBuilder &b) {
      b.group(soundNotifyBox_, optTr("Play a sound for incoming articles"), true, [this](OptionsPageBuilder &grp) {
        grp.row([this](OptionsRowBuilder &r) {
          editSoundNotifier_ = new LineEdit();
          r.addWidget(editSoundNotifier_, 1);
          selectionSoundNotifier_ = r.button(optTr("Browse..."));
          playSoundNotifier_ = r.button(optTr("Play"));
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
      this, optTr("Open File..."), path,
      optTr("Audio Files (*.wav *.flac *.mp3);;All Files (*)"));
  if (!fileName.isEmpty()) editSoundNotifier_->setText(fileName);
}

void NotificationsSettingsPage::slotPlaySoundNotifier()
{
  if (!editSoundNotifier_->text().isEmpty()) {
    emit playSoundRequested(editSoundNotifier_->text());
  }
}

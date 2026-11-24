// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NOTIFICATIONSSETTINGSPAGE_H
#define NOTIFICATIONSSETTINGSPAGE_H

#include <QString>
#include <QTabWidget>
#include <QVector>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
struct OptionSearchEntry;

/**
 * Desktop and sound notification settings page in OptionsDialog.
 */
class NotificationsSettingsPage : public QTabWidget
{
  Q_OBJECT
public:
  explicit NotificationsSettingsPage(QWidget *parent = nullptr,
                                     QVector<OptionSearchEntry> *searchEntries = nullptr,
                                     int pageIndex = -1);

  QGroupBox *soundNotifyBox_ = nullptr;
  QLineEdit *editSoundNotifier_ = nullptr;
  QPushButton *selectionSoundNotifier_ = nullptr;
  QPushButton *playSoundNotifier_ = nullptr;

  QGroupBox *showNotifyOn_ = nullptr;
  QComboBox *screenNotify_ = nullptr;
  QComboBox *positionNotify_ = nullptr;
  QSpinBox *transparencyNotify_ = nullptr;
  QSpinBox *countShowNewsNotify_ = nullptr;
  QSpinBox *timeShowNewsNotify_ = nullptr;
  QSpinBox *widthTitleNewsNotify_ = nullptr;

  QCheckBox *showTitlesFeedsNotify_ = nullptr;
  QCheckBox *showIconFeedNotify_ = nullptr;
  QCheckBox *showButtonMarkAllNotify_ = nullptr;
  QCheckBox *showButtonMarkReadNotify_ = nullptr;
  QCheckBox *showButtonExBrowserNotify_ = nullptr;
  QCheckBox *showButtonDeleteNotify_ = nullptr;
  QCheckBox *closeNotify_ = nullptr;

  QCheckBox *fullscreenModeNotify_ = nullptr;
  QCheckBox *showNotifyInactiveApp_ = nullptr;
  QCheckBox *onlySelectedFeeds_ = nullptr;

signals:
  void playSoundRequested(const QString &soundPath);
  void previewNotificationRequested();
  void selectFeedsRequested();

private slots:
  void selectionSoundNotifier();
  void slotPlaySoundNotifier();
};

#endif // NOTIFICATIONSSETTINGSPAGE_H

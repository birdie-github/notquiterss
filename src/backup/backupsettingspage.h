// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BACKUPSETTINGSPAGE_H
#define BACKUPSETTINGSPAGE_H
#include <QWidget>
class QCheckBox;
class QSpinBox;
class QButtonGroup;
class BackupSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit BackupSettingsPage(QWidget *parent = nullptr);
  void save();
private:
  QCheckBox *enabled_, *onExit_, *subscriptions_, *scheduled_, *clean_;
  QSpinBox *keep_;
  QButtonGroup *frequency_;
};
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BACKUPSETTINGSPAGE_H
#define BACKUPSETTINGSPAGE_H
#include <QVector>
#include <QWidget>
class QCheckBox;
class QSpinBox;
class QButtonGroup;
struct OptionSearchEntry;
class BackupSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit BackupSettingsPage(QWidget *parent = nullptr,
                              QVector<OptionSearchEntry> *searchEntries = nullptr,
                              int pageIndex = -1);
  void save();
private:
  QCheckBox *enabled_, *onExit_, *subscriptions_, *scheduled_, *clean_;
  QSpinBox *keep_;
  QButtonGroup *frequency_;
};
#endif

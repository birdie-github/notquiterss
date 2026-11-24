// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYSTEMTRAYSETTINGSPAGE_H
#define SYSTEMTRAYSETTINGSPAGE_H

#include <QFrame>
#include <QVector>

class QCheckBox;
class QGroupBox;
class QRadioButton;
struct OptionSearchEntry;

/**
 * System tray integration settings page in OptionsDialog.
 */
class SystemTraySettingsPage : public QFrame
{
  Q_OBJECT
public:
  explicit SystemTraySettingsPage(QWidget *parent = nullptr,
                                  QVector<OptionSearchEntry> *searchEntries = nullptr,
                                  int pageIndex = -1);

  void setBehaviorIconTray(int behavior);
  int behaviorIconTray() const;

  QGroupBox *showTrayIconBox_ = nullptr;
  QCheckBox *startingTray_ = nullptr;
  QCheckBox *minimizingTray_ = nullptr;
  QCheckBox *closingTray_ = nullptr;
  QCheckBox *singleClickTray_ = nullptr;
  QCheckBox *clearStatusNew_ = nullptr;
  QCheckBox *emptyWorking_ = nullptr;

private:
  QRadioButton *staticIconTray_ = nullptr;
  QRadioButton *changeIconTray_ = nullptr;
  QRadioButton *newCountTray_ = nullptr;
  QRadioButton *unreadCountTray_ = nullptr;
};

#endif // SYSTEMTRAYSETTINGSPAGE_H

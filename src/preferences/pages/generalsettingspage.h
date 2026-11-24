// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GENERALSETTINGSPAGE_H
#define GENERALSETTINGSPAGE_H

#include <QFrame>
#include <QVector>

class QCheckBox;
class QSettings;
class QSpinBox;
struct OptionSearchEntry;

/**
 * General application settings page in OptionsDialog.
 */
class GeneralSettingsPage : public QFrame
{
  Q_OBJECT
public:
  explicit GeneralSettingsPage(QWidget *parent = nullptr,
                               QVector<OptionSearchEntry> *searchEntries = nullptr,
                               int pageIndex = -1);

  void save();

  QCheckBox *showSplashScreen_ = nullptr;
  QCheckBox *reopenFeedStartup_ = nullptr;
  QCheckBox *openNewTabNextToActive_ = nullptr;
  QCheckBox *hideFeedsOpenTab_ = nullptr;
  QCheckBox *showCloseButtonTab_ = nullptr;
#ifdef NQRSS_ENABLE_DONATE_BUTTON
  QCheckBox *hideDonateButton_ = nullptr;
#endif
  QCheckBox *showToggleFeedsTree_ = nullptr;
  QCheckBox *defaultIconFeeds_ = nullptr;
  QCheckBox *autocollapseFolder_ = nullptr;
  QCheckBox *updateCheckEnabled_ = nullptr;
  QCheckBox *storeDBMemory_ = nullptr;
  QSpinBox *saveDBMemFileInterval_ = nullptr;

private:
#if defined(Q_OS_WIN)
  QCheckBox *autoRunEnabled_ = nullptr;
  QSettings *autoRunSettings_ = nullptr;
#endif
};

#endif // GENERALSETTINGSPAGE_H

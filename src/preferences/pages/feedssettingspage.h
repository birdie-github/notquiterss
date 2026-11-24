// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDSSETTINGSPAGE_H
#define FEEDSSETTINGSPAGE_H

#include "articles/cleanup/cleanuppreviewdialog.h"

#include <QSqlDatabase>
#include <QTabWidget>
#include <QVector>
#include <functional>

class LineEdit;
class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QToolButton;
struct OptionSearchEntry;

/**
 * Feeds update, display, reading, and shutdown cleanup settings page.
 */
class FeedsSettingsPage : public QTabWidget
{
  Q_OBJECT
public:
  FeedsSettingsPage(QSqlDatabase db,
                    std::function<bool()> isPageActive,
                    QWidget *parent = nullptr,
                    QVector<OptionSearchEntry> *searchEntries = nullptr,
                    int pageIndex = -1);

  void initCleanupTracking();
  void onPageEntered();
  void setOpeningFeed(int action);
  int getOpeningFeed() const;

  QCheckBox *updateFeedsStartUp_ = nullptr;
  QCheckBox *updateFeedsEnable_ = nullptr;
  QSpinBox *updateFeedsInterval_ = nullptr;
  QComboBox *updateIntervalType_ = nullptr;

  QRadioButton *positionLastNews_ = nullptr;
  QRadioButton *positionFirstNews_ = nullptr;
  QRadioButton *positionUnreadNews_ = nullptr;
  QCheckBox *openNewsWebViewOn_ = nullptr;
  QRadioButton *nottoOpenNews_ = nullptr;

  QGroupBox *markNewsReadOn_ = nullptr;
  QRadioButton *markCurNewsRead_ = nullptr;
  QRadioButton *markPrevNewsRead_ = nullptr;
  QSpinBox *markNewsReadTime_ = nullptr;
  QCheckBox *markReadSwitchingFeed_ = nullptr;
  QCheckBox *markReadClosingTab_ = nullptr;
  QCheckBox *markReadMinimize_ = nullptr;

  QComboBox *formatDate_ = nullptr;
  QComboBox *formatTime_ = nullptr;
  QCheckBox *alternatingRowColorsNews_ = nullptr;
  QCheckBox *simplifiedDateTime_ = nullptr;
  QComboBox *mainNewsFilter_ = nullptr;
  LineEdit *styleSheetNewsEdit_ = nullptr;

  QCheckBox *changeBehaviorActionNUN_ = nullptr;
  QCheckBox *notDeleteStarred_ = nullptr;
  QCheckBox *notDeleteLabeled_ = nullptr;
  QCheckBox *markIdenticalNewsRead_ = nullptr;

  QGroupBox *cleanupOnShutdownBox_ = nullptr;
  QCheckBox *dayCleanUpOn_ = nullptr;
  QSpinBox *maxDayCleanUp_ = nullptr;
  QCheckBox *newsCleanUpOn_ = nullptr;
  QSpinBox *maxNewsCleanUp_ = nullptr;
  QCheckBox *readCleanUp_ = nullptr;
  QCheckBox *neverUnreadCleanUp_ = nullptr;
  QCheckBox *neverStarCleanUp_ = nullptr;
  QCheckBox *neverLabelCleanUp_ = nullptr;
  QCheckBox *cleanUpDeleted_ = nullptr;
  QCheckBox *optimizeDB_ = nullptr;

  QGroupBox *avoidedOldNewsDateOn_ = nullptr;
  QCalendarWidget *avoidedOldNewsDate_ = nullptr;

private slots:
  void selectionUserStyleNews();
  void updateCleanUpPreview();
  void showCleanUpPreview();

private:
  void updateMaxDayCleanUpWarning();
  ShutdownCleanUpConfig currentCleanUpConfig() const;

  QSqlDatabase db_;
  std::function<bool()> isPageActive_;
  QToolButton *maxDayCleanUpWarning_ = nullptr;
  QLabel *cleanUpPreviewLabel_ = nullptr;
  QPushButton *cleanUpPreviewButton_ = nullptr;
  ShutdownCleanUpPlan cleanUpPlan_;
  bool cleanUpPlanValid_ = false;
  int initialMaxDayCleanUp_ = -1;
};

#endif // FEEDSSETTINGSPAGE_H

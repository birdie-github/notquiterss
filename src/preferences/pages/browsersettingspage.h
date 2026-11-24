// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BROWSERSETTINGSPAGE_H
#define BROWSERSETTINGSPAGE_H

#include <QTabWidget>
#include <QVector>

class LineEdit;
class QCheckBox;
class QRadioButton;
class QSpinBox;
struct OptionSearchEntry;

/**
 * Article View (images, zoom, external browser, downloads) settings page.
 */
class BrowserSettingsPage : public QTabWidget
{
  Q_OBJECT
public:
  explicit BrowserSettingsPage(QWidget *parent = nullptr,
                               QVector<OptionSearchEntry> *searchEntries = nullptr,
                               int pageIndex = -1);

  bool validate();

  QCheckBox *autoLoadImages_ = nullptr;
  QCheckBox *highQualityImageScaling_ = nullptr;
  QSpinBox *preloadNewspaperPages_ = nullptr;
  QSpinBox *defaultZoomPages_ = nullptr;
  QRadioButton *customExternalBrowser_ = nullptr;
  LineEdit *externalBrowser_ = nullptr;
  QCheckBox *returnFocusAfterLink_ = nullptr;
  LineEdit *downloadLocationEdit_ = nullptr;
  QCheckBox *askDownloadLocation_ = nullptr;

private slots:
  void selectionDownloadLocation();
};

#endif // BROWSERSETTINGSPAGE_H

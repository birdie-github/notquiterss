// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FONTSCOLORSSETTINGSPAGE_H
#define FONTSCOLORSSETTINGSPAGE_H

#include <QFont>
#include <QString>
#include <QTabWidget>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
struct OptionSearchEntry;

/**
 * Fonts and Colors configuration tabs in OptionsDialog.
 */
class FontsColorsSettingsPage : public QTabWidget
{
  Q_OBJECT
public:
  explicit FontsColorsSettingsPage(QWidget *parent = nullptr,
                                   QVector<OptionSearchEntry> *searchEntries = nullptr,
                                   int pageIndex = -1);

  QFont fontOptionValue(int optionId) const;
  void setFontOptionValue(int optionId, const QFont &font);
  QString colorOptionValue(int optionId) const;
  void setColorOptionValue(int optionId, const QString &colorName);

  void selectFontItem(int row);
  void selectColorOption(int optionId);

private slots:
  void slotFontChange();
  void slotFontReset();
  void slotFontResetAll();
  void slotColorChange();
  void slotColorReset();
  void slotColorResetAll();

private:
  void resetFontOption(QTreeWidgetItem *item);

  QTreeWidget *fontsTree_ = nullptr;
  QTreeWidget *colorsTree_ = nullptr;
  QVector<QTreeWidgetItem *> colorOptionItems_;
};

#endif // FONTSCOLORSSETTINGSPAGE_H

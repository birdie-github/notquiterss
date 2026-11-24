// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LANGUAGESETTINGSPAGE_H
#define LANGUAGESETTINGSPAGE_H

#include <QString>
#include <QVector>
#include <QWidget>

class QTreeWidget;
struct OptionSearchEntry;

/**
 * UI translation selection page in OptionsDialog.
 */
class LanguageSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit LanguageSettingsPage(QWidget *parent = nullptr,
                                QVector<OptionSearchEntry> *searchEntries = nullptr,
                                int pageIndex = -1);

  void load();
  QString language() const;
  void setLanguage(const QString &langFileName);

private:
  QTreeWidget *languageFileList_ = nullptr;
  QString selectedLanguage_;
  bool loadLanguagesOk_ = false;
};

#endif // LANGUAGESETTINGSPAGE_H

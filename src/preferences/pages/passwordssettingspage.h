// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PASSWORDSSETTINGSPAGE_H
#define PASSWORDSSETTINGSPAGE_H

#include <QVector>
#include <QWidget>

class QSqlDatabase;
class QTreeWidget;
struct OptionSearchEntry;

/**
 * Saved feed/HTTP credentials page in OptionsDialog.
 */
class PasswordsSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit PasswordsSettingsPage(QWidget *parent = nullptr,
                                 QVector<OptionSearchEntry> *searchEntries = nullptr,
                                 int pageIndex = -1);

  void load();
  void apply(QSqlDatabase &db);

private slots:
  void deleteSelected();
  void deleteAll();
  void showPasswords();

private:
  QTreeWidget *passTree_ = nullptr;
  bool loadPasswordsOk_ = false;
};

#endif // PASSWORDSSETTINGSPAGE_H

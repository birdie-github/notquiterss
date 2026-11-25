// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHORTCUTSSETTINGSPAGE_H
#define SHORTCUTSSETTINGSPAGE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class LineEdit;
class QAction;
class QActionGroup;
class QGroupBox;
class QIcon;
class QLabel;
class QModelIndex;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTreeView;
struct OptionSearchEntry;

/**
 * Keyboard shortcut configuration page in OptionsDialog.
 */
class ShortcutsSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit ShortcutsSettingsPage(const QList<QAction *> &actions,
                                 const QStringList *defaultShortcuts,
                                 QWidget *parent = nullptr,
                                 QVector<OptionSearchEntry> *searchEntries = nullptr,
                                 int pageIndex = -1);

  void load();
  bool isLoaded() const { return loadShortcutsOk_; }
  void save(QList<QAction *> actions, QActionGroup *labelGroup, QObject *actionParent);

  void selectAction(const QString &objectName, const QString &fallbackTitle);
  void addLabelAction(const QString &id, const QString &name, const QIcon &icon);
  void updateLabelAction(const QString &id, const QString &name, const QIcon &icon);
  void removeLabelAction(const QString &id);
  void swapLabelActions(const QString &idA, const QString &idB);
  void syncSearchEntries();

signals:
  void shortcutTreeUpDownPressed();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
  void shortcutTreeClicked(const QModelIndex &index);
  void slotShortcutTreeUpDownPressed();
  void slotClearShortcut();
  void slotResetShortcut();
  void filterShortcutChanged(const QString &text);

private:
  int labelShortcutRow(const QString &labelId) const;
  void clearDuplicateHighlight(const QString &shortcutStr, int skipRow);

  QList<QAction *> shortcutActions_;
  const QStringList *listDefaultShortcut_ = nullptr;
  QVector<OptionSearchEntry> *searchEntries_ = nullptr;
  int pageIndex_ = -1;
  bool loadShortcutsOk_ = false;
  LineEdit *filterShortcut_ = nullptr;
  QTreeView *shortcutTree_ = nullptr;
  QStandardItemModel *shortcutModel_ = nullptr;
  QSortFilterProxyModel *shortcutProxyModel_ = nullptr;
  LineEdit *editShortcut_ = nullptr;
  QGroupBox *editShortcutBox_ = nullptr;
  QLabel *warningShortcut_ = nullptr;
};

#endif // SHORTCUTSSETTINGSPAGE_H

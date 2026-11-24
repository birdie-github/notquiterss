// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LABELSSETTINGSPAGE_H
#define LABELSSETTINGSPAGE_H

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPushButton;
class QSqlDatabase;
class QTreeWidget;
class QTreeWidgetItem;
struct OptionSearchEntry;

/**
 * Custom article labels management page in OptionsDialog.
 */
class LabelsSettingsPage : public QWidget
{
  Q_OBJECT
public:
  explicit LabelsSettingsPage(QWidget *parent = nullptr,
                              QVector<OptionSearchEntry> *searchEntries = nullptr,
                              int pageIndex = -1);

  void load();
  void apply(QSqlDatabase &db);
  const QStringList &modifiedIds() const { return idLabels_; }

signals:
  void labelAdded(const QString &id, const QString &name, const QIcon &icon);
  void labelEdited(const QString &id, const QString &name, const QIcon &icon);
  void labelDeleted(const QString &id);
  void labelsSwapped(const QString &idA, const QString &idB);

private slots:
  void newLabel();
  void editLabel();
  void deleteLabel();
  void moveUpLabel();
  void moveDownLabel();
  void currentLabelChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
  void markModified(const QString &idLabel);

  QTreeWidget *labelsTree_ = nullptr;
  QPushButton *newLabelButton_ = nullptr;
  QPushButton *editLabelButton_ = nullptr;
  QPushButton *deleteLabelButton_ = nullptr;
  QPushButton *moveUpLabelButton_ = nullptr;
  QPushButton *moveDownLabelButton_ = nullptr;
  QStringList idLabels_;
  bool loadLabelsOk_ = false;
};

#endif // LABELSSETTINGSPAGE_H

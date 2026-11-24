// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REORDERABLETREECONTROLLER_H
#define REORDERABLETREECONTROLLER_H

#include <QList>
#include <QObject>
#include <functional>

class QAbstractButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Coordinates Move up / Move down / Remove actions and selection-dependent
 * button states for a flat top-level QTreeWidget.
 */
class ReorderableTreeController : public QObject
{
  Q_OBJECT
public:
  using SwapCallback = std::function<void(QTreeWidgetItem *moved, QTreeWidgetItem *other)>;

  ReorderableTreeController(QTreeWidget *tree,
                            QAbstractButton *moveUpButton,
                            QAbstractButton *moveDownButton,
                            QAbstractButton *removeButton = nullptr,
                            const QList<QAbstractButton *> &selectionButtons = {},
                            SwapCallback onSwapped = {});

  void updateButtons();
  void moveCurrent(int delta);
  void removeCurrent();

private:
  QTreeWidget *tree_ = nullptr;
  QAbstractButton *moveUpButton_ = nullptr;
  QAbstractButton *moveDownButton_ = nullptr;
  QAbstractButton *removeButton_ = nullptr;
  QList<QAbstractButton *> selectionButtons_;
  SwapCallback onSwapped_;
};

#endif // REORDERABLETREECONTROLLER_H

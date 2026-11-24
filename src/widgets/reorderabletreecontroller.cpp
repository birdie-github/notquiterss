// SPDX-License-Identifier: GPL-3.0-or-later
#include "reorderabletreecontroller.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QTreeWidget>
#include <utility>

ReorderableTreeController::ReorderableTreeController(
    QTreeWidget *tree,
    QAbstractButton *moveUpButton,
    QAbstractButton *moveDownButton,
    QAbstractButton *removeButton,
    const QList<QAbstractButton *> &selectionButtons,
    SwapCallback onSwapped)
  : QObject(tree)
  , tree_(tree)
  , moveUpButton_(moveUpButton)
  , moveDownButton_(moveDownButton)
  , removeButton_(removeButton)
  , selectionButtons_(selectionButtons)
  , onSwapped_(std::move(onSwapped))
{
  if (moveUpButton_) {
    connect(moveUpButton_, &QAbstractButton::clicked, this, [this] { moveCurrent(-1); });
  }
  if (moveDownButton_) {
    connect(moveDownButton_, &QAbstractButton::clicked, this, [this] { moveCurrent(1); });
  }
  if (removeButton_) {
    connect(removeButton_, &QAbstractButton::clicked,
            this, &ReorderableTreeController::removeCurrent);
  }
  if (tree_) {
    connect(tree_, &QTreeWidget::currentItemChanged,
            this, &ReorderableTreeController::updateButtons);
    if (QAbstractItemModel *model = tree_->model()) {
      connect(model, &QAbstractItemModel::rowsInserted,
              this, &ReorderableTreeController::updateButtons);
      connect(model, &QAbstractItemModel::rowsRemoved,
              this, &ReorderableTreeController::updateButtons);
      connect(model, &QAbstractItemModel::modelReset,
              this, &ReorderableTreeController::updateButtons);
    }
  }
  updateButtons();
}

void ReorderableTreeController::updateButtons()
{
  const int row = tree_ ? tree_->indexOfTopLevelItem(tree_->currentItem()) : -1;
  const int count = tree_ ? tree_->topLevelItemCount() : 0;
  const bool hasSelection = (row >= 0);

  if (moveUpButton_) moveUpButton_->setEnabled(hasSelection && row > 0);
  if (moveDownButton_) moveDownButton_->setEnabled(hasSelection && row < count - 1);
  if (removeButton_) removeButton_->setEnabled(hasSelection);
  for (QAbstractButton *button : std::as_const(selectionButtons_)) {
    if (button) button->setEnabled(hasSelection);
  }
}

void ReorderableTreeController::moveCurrent(int delta)
{
  if (!tree_ || (delta != -1 && delta != 1)) return;
  const int row = tree_->indexOfTopLevelItem(tree_->currentItem());
  const int target = row + delta;
  if (row < 0 || target < 0 || target >= tree_->topLevelItemCount()) return;

  if (onSwapped_) {
    onSwapped_(tree_->topLevelItem(row), tree_->topLevelItem(target));
  }
  // Removing and reinserting the neighbor preserves currentItem() and focus.
  QTreeWidgetItem *neighbor = tree_->takeTopLevelItem(target);
  tree_->insertTopLevelItem(row, neighbor);
  updateButtons();
}

void ReorderableTreeController::removeCurrent()
{
  if (!tree_) return;
  const int row = tree_->indexOfTopLevelItem(tree_->currentItem());
  if (row < 0) return;
  delete tree_->takeTopLevelItem(row);
  updateButtons();
}

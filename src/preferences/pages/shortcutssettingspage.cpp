// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcutssettingspage.h"

#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"
#include "widgets/lineedit.h"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <utility>

ShortcutsSettingsPage::ShortcutsSettingsPage(const QList<QAction *> &actions,
                                             const QStringList *defaultShortcuts,
                                             QWidget *parent,
                                             QVector<OptionSearchEntry> *searchEntries,
                                             int pageIndex)
  : QWidget(parent)
  , shortcutActions_(actions)
  , listDefaultShortcut_(defaultShortcuts)
{
  filterShortcut_ = new LineEdit(this, OptionsDialog::tr("Filter"));

  shortcutTree_ = new QTreeView(this);
  shortcutTree_->setObjectName(QStringLiteral("shortcutTree"));
  shortcutTree_->setSortingEnabled(false);
  shortcutTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  shortcutTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  shortcutTree_->setRootIsDecorated(false);

  shortcutModel_ = new QStandardItemModel(this);
  shortcutModel_->setColumnCount(5);
  shortcutProxyModel_ = new QSortFilterProxyModel(this);
  shortcutProxyModel_->setSourceModel(shortcutModel_);
  shortcutProxyModel_->setFilterKeyColumn(-1);
  shortcutProxyModel_->setFilterRole(Qt::UserRole + 1);
  shortcutProxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  shortcutTree_->setModel(shortcutProxyModel_);

  shortcutTree_->hideColumn(3);
  shortcutTree_->hideColumn(4);
  shortcutTree_->header()->setStretchLastSection(true);
  shortcutTree_->setColumnWidth(0, 215);
  shortcutTree_->setColumnWidth(1, 165);
  shortcutTree_->setColumnWidth(2, 120);
  shortcutModel_->setHorizontalHeaderLabels({
      OptionsDialog::tr("Action"), OptionsDialog::tr("Description"), OptionsDialog::tr("Shortcut"),
      QStringLiteral("ObjectName"), QStringLiteral("Data")});

  editShortcut_ = new LineEdit(this);
  auto *resetShortcutButton = new QPushButton(OptionsDialog::tr("Reset"), this);

  auto *editShortcutLayout = new QHBoxLayout();
  editShortcutLayout->addWidget(new QLabel(OptionsDialog::tr("Shortcut:"), this));
  editShortcutLayout->addWidget(editShortcut_, 1);
  editShortcutLayout->addWidget(resetShortcutButton);

  editShortcutBox_ = new QGroupBox(this);
  editShortcutBox_->setEnabled(false);
  editShortcutBox_->setLayout(editShortcutLayout);

  warningShortcut_ = new QLabel(this);

  auto *shortcutLayout = new QVBoxLayout(this);
  shortcutLayout->setContentsMargins(0, 0, 0, 0);
  shortcutLayout->addWidget(filterShortcut_);
  shortcutLayout->addWidget(shortcutTree_, 1);
  shortcutLayout->addWidget(warningShortcut_);
  shortcutLayout->addWidget(editShortcutBox_);

  connect(shortcutTree_, &QAbstractItemView::clicked,
          this, &ShortcutsSettingsPage::shortcutTreeClicked);
  connect(this, &ShortcutsSettingsPage::shortcutTreeUpDownPressed,
          this, &ShortcutsSettingsPage::slotShortcutTreeUpDownPressed, Qt::QueuedConnection);
  connect(editShortcut_, &LineEdit::signalClear,
          this, &ShortcutsSettingsPage::slotClearShortcut);
  connect(resetShortcutButton, &QAbstractButton::clicked,
          this, &ShortcutsSettingsPage::slotResetShortcut);
  connect(filterShortcut_, &QLineEdit::textChanged,
          this, &ShortcutsSettingsPage::filterShortcutChanged);

  shortcutTree_->installEventFilter(this);
  editShortcut_->installEventFilter(this);

  if (searchEntries && pageIndex >= 0) {
    searchEntries->append({
        pageIndex, -1, OptionsDialog::tr("Keyboard Shortcuts"), QString(),
        QStringLiteral("hotkeys keybindings"), this});
    for (QAction *pAction : std::as_const(shortcutActions_)) {
      if (!pAction || pAction->isSeparator()) continue;
      const bool isAutoLoad = pAction->objectName() == QLatin1String("autoLoadImagesToggle");
      const QString actionTitle = isAutoLoad
          ? OptionsDialog::tr("Load images")
          : pAction->text().remove(QLatin1Char('&'));
      if (actionTitle.isEmpty()) continue;
      const QString actionDesc = isAutoLoad
          ? OptionsDialog::tr("Automatically load images in article view")
          : pAction->toolTip();
      searchEntries->append({
          pageIndex, -1, actionTitle, actionDesc,
          pAction->shortcut().toString(),
          this, -1, pAction->objectName(), true});
    }
  }
}

bool ShortcutsSettingsPage::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(obj, event);
  }
  auto *keyEvent = static_cast<QKeyEvent *>(event);
  if (obj == shortcutTree_) {
    if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) {
      emit shortcutTreeUpDownPressed();
    }
    return false;
  }
  if (obj == editShortcut_) {
    if (keyEvent->key() < Qt::Key_Shift || keyEvent->key() > Qt::Key_Alt) {
      QString str;
      if ((keyEvent->modifiers() & Qt::ShiftModifier) ||
          (keyEvent->modifiers() & Qt::ControlModifier) ||
          (keyEvent->modifiers() & Qt::AltModifier)) {
        str.append(QKeySequence(keyEvent->modifiers()).toString());
      }
      if (keyEvent->key() == Qt::Key_Backtab) {
        str.append(QKeySequence(Qt::Key_Tab).toString());
      } else {
        str.append(QKeySequence(keyEvent->key()).toString());
      }
      editShortcut_->setText(str);

      const QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
      const int row = index.row();
      const QString oldShortcut = shortcutModel_->item(row, 2)->text();
      clearDuplicateHighlight(oldShortcut, row);

      shortcutModel_->item(row, 2)->setText(str);
      shortcutModel_->item(row, 2)->setData(str);

      if (!str.isEmpty()) {
        const QList<QStandardItem *> treeItems =
            shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
        if (treeItems.count() > 1) {
          for (QStandardItem *item : treeItems) {
            if (item->row() != row) {
              warningShortcut_->setText(OptionsDialog::tr("Warning: key is already assigned to") +
                                        QStringLiteral(" '") +
                                        shortcutModel_->item(item->row(), 0)->text() +
                                        QStringLiteral("'"));
            }
            item->setData(QColor(Qt::red), Qt::ForegroundRole);
          }
        } else {
          warningShortcut_->clear();
        }
      }
    }
    return true;
  }
  return QWidget::eventFilter(obj, event);
}

void ShortcutsSettingsPage::load()
{
  if (loadShortcutsOk_) return;
  loadShortcutsOk_ = true;

  for (QAction *pAction : std::as_const(shortcutActions_)) {
    const QStringList treeItem{
        pAction->text().remove(QLatin1Char('&')),
        pAction->toolTip(),
        pAction->shortcut().toString(),
        pAction->objectName(),
        pAction->data().toString()};

    QList<QStandardItem *> treeItems;
    for (int i = 0; i < treeItem.count(); ++i) {
      auto *item = new QStandardItem(treeItem.at(i));
      if (i == 0) {
        if (pAction->icon().isNull()) {
          item->setIcon(QIcon(QStringLiteral(":/images/images/noicon.png")));
        } else if (pAction->objectName() == QLatin1String("autoLoadImagesToggle")) {
          item->setIcon(QIcon(QStringLiteral(":/images/imagesOn")));
          item->setText(OptionsDialog::tr("Load images"));
        } else {
          item->setIcon(pAction->icon());
        }
      } else if (i == 1 && pAction->objectName() == QLatin1String("autoLoadImagesToggle")) {
        item->setText(OptionsDialog::tr("Automatically load images in article view"));
      }
      if (i >= 0 && i <= 2) {
        item->setData(treeItem.at(i));
      }
      treeItems.append(item);
    }
    shortcutModel_->appendRow(treeItems);

    const QString str = pAction->shortcut().toString();
    if (!str.isEmpty()) {
      const QList<QStandardItem *> dupes = shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
      if (dupes.count() > 1) {
        for (QStandardItem *dupe : dupes) {
          dupe->setData(QColor(Qt::red), Qt::ForegroundRole);
        }
      }
    }
  }
}

void ShortcutsSettingsPage::save(QList<QAction *> actions, QActionGroup *labelGroup,
                                 QObject *actionParent)
{
  if (!loadShortcutsOk_) return;
  for (int i = 0; i < shortcutModel_->rowCount(); ++i) {
    const QString objectName = shortcutModel_->item(i, 3)->text();
    if (objectName.contains(QStringLiteral("labelAction_"))) {
      auto *action = new QAction(actionParent);
      action->setIcon(shortcutModel_->item(i, 0)->icon());
      action->setText(shortcutModel_->item(i, 0)->text());
      action->setShortcut(QKeySequence(shortcutModel_->item(i, 2)->text()));
      action->setObjectName(objectName);
      action->setCheckable(true);
      action->setData(shortcutModel_->item(i, 4)->text());
      labelGroup->addAction(action);
      actions.append(action);
    } else {
      actions.at(i)->setShortcut(QKeySequence(shortcutModel_->item(i, 2)->text()));
    }
  }
}

void ShortcutsSettingsPage::selectAction(const QString &objectName, const QString &fallbackTitle)
{
  load();
  if (!filterShortcut_->text().isEmpty()) filterShortcut_->clear();
  QList<QStandardItem *> items;
  if (!objectName.isEmpty()) {
    items = shortcutModel_->findItems(objectName, Qt::MatchExactly, 3);
  }
  if (items.isEmpty() && !fallbackTitle.isEmpty()) {
    items = shortcutModel_->findItems(fallbackTitle, Qt::MatchExactly, 0);
  }
  if (!items.isEmpty()) {
    const QModelIndex proxyIdx = shortcutProxyModel_->mapFromSource(items.first()->index());
    if (proxyIdx.isValid()) {
      shortcutTree_->setCurrentIndex(proxyIdx);
      shortcutTree_->scrollTo(proxyIdx);
      shortcutTreeClicked(proxyIdx);
    }
  }
  shortcutTree_->setFocus();
}

void ShortcutsSettingsPage::addLabelAction(const QString &id, const QString &name, const QIcon &icon)
{
  load();
  const QStringList itemStr{
      name, name, QString(), QStringLiteral("labelAction_%1").arg(id), id};
  QList<QStandardItem *> treeItems;
  for (int i = 0; i < itemStr.count(); ++i) {
    auto *item = new QStandardItem(itemStr.at(i));
    if (i == 0) item->setIcon(icon);
    if (i >= 0 && i <= 2) item->setData(itemStr.at(i));
    treeItems.append(item);
  }
  shortcutModel_->appendRow(treeItems);
}

void ShortcutsSettingsPage::updateLabelAction(const QString &id, const QString &name, const QIcon &icon)
{
  load();
  const int shortcutRow = labelShortcutRow(id);
  if (shortcutRow < 0) return;
  auto *item = shortcutModel_->item(shortcutRow, 0);
  item->setIcon(icon);
  item->setText(name);
  item->setData(name);
}

void ShortcutsSettingsPage::removeLabelAction(const QString &id)
{
  load();
  const int shortcutRow = labelShortcutRow(id);
  if (shortcutRow >= 0) {
    shortcutModel_->removeRow(shortcutRow);
  }
}

void ShortcutsSettingsPage::swapLabelActions(const QString &idA, const QString &idB)
{
  load();
  const int rowA = labelShortcutRow(idA);
  const int rowB = labelShortcutRow(idB);
  if (rowA >= 0 && rowB >= 0) {
    shortcutModel_->insertRow(rowB, shortcutModel_->takeRow(rowA));
  }
}

int ShortcutsSettingsPage::labelShortcutRow(const QString &labelId) const
{
  const auto items = shortcutModel_->findItems(
      QStringLiteral("labelAction_%1").arg(labelId), Qt::MatchExactly, 3);
  return items.isEmpty() ? -1 : items.first()->row();
}

void ShortcutsSettingsPage::clearDuplicateHighlight(const QString &shortcutStr, int skipRow)
{
  if (shortcutStr.isEmpty()) return;
  const QList<QStandardItem *> treeItems =
      shortcutModel_->findItems(shortcutStr, Qt::MatchFixedString, 2);
  for (QStandardItem *item : treeItems) {
    if (treeItems.count() == 2 || item->row() == skipRow) {
      item->setData(shortcutModel_->item(0, 1)->data(Qt::ForegroundRole), Qt::ForegroundRole);
    }
  }
}

void ShortcutsSettingsPage::slotShortcutTreeUpDownPressed()
{
  shortcutTreeClicked(shortcutTree_->currentIndex());
}

void ShortcutsSettingsPage::shortcutTreeClicked(const QModelIndex &index)
{
  const QModelIndex indexCur = shortcutProxyModel_->mapToSource(index);
  editShortcut_->setText(shortcutModel_->item(indexCur.row(), 2)->text());
  editShortcutBox_->setEnabled(true);
  editShortcut_->setFocus();
  warningShortcut_->clear();
}

void ShortcutsSettingsPage::slotClearShortcut()
{
  const QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
  const int row = index.row();
  clearDuplicateHighlight(shortcutModel_->item(row, 2)->text(), row);
  editShortcut_->clear();
  shortcutModel_->item(row, 2)->setText(QString());
  shortcutModel_->item(row, 2)->setData(QString());
  warningShortcut_->clear();
}

void ShortcutsSettingsPage::slotResetShortcut()
{
  const QModelIndex index = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
  const int row = index.row();
  if (shortcutModel_->item(row, 3)->text().contains(QStringLiteral("labelAction_"))) return;

  clearDuplicateHighlight(shortcutModel_->item(row, 2)->text(), row);

  const QString str = listDefaultShortcut_->at(row);
  editShortcut_->setText(str);
  shortcutModel_->item(row, 2)->setText(str);
  shortcutModel_->item(row, 2)->setData(str);
  warningShortcut_->clear();

  if (!str.isEmpty()) {
    const QList<QStandardItem *> treeItems =
        shortcutModel_->findItems(str, Qt::MatchFixedString, 2);
    for (QStandardItem *item : treeItems) {
      if (item->row() != row) {
        warningShortcut_->setText(OptionsDialog::tr("Warning: key is already assigned to ") +
                                  QStringLiteral(" '") +
                                  shortcutModel_->item(item->row(), 0)->text() +
                                  QStringLiteral("'"));
      }
      if (treeItems.count() > 1) {
        item->setData(QColor(Qt::red), Qt::ForegroundRole);
      }
    }
  }
}

void ShortcutsSettingsPage::filterShortcutChanged(const QString &text)
{
  shortcutProxyModel_->setFilterFixedString(text);

  if (shortcutTree_->currentIndex().isValid()) {
    const QModelIndex indexCur = shortcutProxyModel_->mapToSource(shortcutTree_->currentIndex());
    editShortcut_->setText(shortcutModel_->item(indexCur.row(), 2)->text());
    editShortcutBox_->setEnabled(true);
  } else {
    editShortcut_->clear();
    editShortcutBox_->setEnabled(false);
  }
  warningShortcut_->clear();
}

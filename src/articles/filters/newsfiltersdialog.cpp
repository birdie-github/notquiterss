/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#include "newsfiltersdialog.h"

#include "app/mainapplication.h"
#include <QPointer>
#include "filterrulesdialog.h"
#include "feeds/update/parseobject.h"
#include "preferences/settings.h"
#include "widgets/reorderabletreecontroller.h"

NewsFiltersDialog::NewsFiltersDialog(QWidget *parent)
  : Dialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
           Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint |
           Qt::WindowCloseButtonHint)
{
  setWindowTitle(tr("News Filters"));
  setMinimumWidth(500);
  setMinimumHeight(300);

  filtersTree_ = new QTreeWidget(this);
  filtersTree_->setObjectName("filtersTree");
  filtersTree_->setColumnCount(4);
  filtersTree_->setColumnHidden(0, true);
  filtersTree_->setColumnHidden(3, true);
  filtersTree_->setSortingEnabled(false);
  filtersTree_->header()->resizeSection(1, 150);
  filtersTree_->header()->setSectionsMovable(false);

  QStringList treeItem;
  treeItem << "Id" << tr("Filter Name") << tr("Feeds") << "Num";
  filtersTree_->setHeaderLabels(treeItem);

  QSqlQuery q;
  QString qStr = QString("SELECT id, name, feeds, enable, num FROM filters ORDER BY num");
  q.exec(qStr);
  while (q.next()) {
    QSqlQuery q1;
    const QString strNameFeeds = feedScopeText(q.value(2).toString());

    treeItem.clear();
    treeItem << q.value(0).toString()
             << q.value(1).toString()
             << strNameFeeds
             << q.value(4).toString();
    QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
    if (q.value(3).toInt() == 1)
      treeWidgetItem->setCheckState(1, Qt::Checked);
    else
      treeWidgetItem->setCheckState(1, Qt::Unchecked);
    treeWidgetItem->setToolTip(2, strNameFeeds);
    filtersTree_->addTopLevelItem(treeWidgetItem);

    if (q.value(4).toInt() == 0) {
      qStr = QString("UPDATE filters SET num='%1' WHERE id=='%1'").
          arg(q.value(0).toInt());
      q1.exec(qStr);
      treeWidgetItem->setText(3, q.value(0).toString());
    }
  }

  QPushButton *newButton = new QPushButton(tr("Create..."), this);
  connect(newButton, &QAbstractButton::clicked, this, &NewsFiltersDialog::newFilter);
  QPushButton *editButton = new QPushButton(tr("Edit..."), this);
  connect(editButton, &QAbstractButton::clicked, this, &NewsFiltersDialog::editFilter);
  QPushButton *deleteButton = new QPushButton(tr("Delete..."), this);
  connect(deleteButton, &QAbstractButton::clicked, this, &NewsFiltersDialog::deleteFilter);

  QPushButton *moveUpButton = new QPushButton(tr("Move up"), this);
  QPushButton *moveDownButton = new QPushButton(tr("Move down"), this);

  QPushButton *runFilterButton = new QPushButton(tr("Apply Filter"), this);
  buttonsLayout->insertWidget(0, runFilterButton);
  connect(runFilterButton, &QAbstractButton::clicked, this, &NewsFiltersDialog::applyFilter);

  QVBoxLayout *buttonsVLayout = new QVBoxLayout();
  buttonsVLayout->addWidget(newButton);
  buttonsVLayout->addWidget(editButton);
  buttonsVLayout->addWidget(deleteButton);
  buttonsVLayout->addSpacing(10);
  buttonsVLayout->addWidget(moveUpButton);
  buttonsVLayout->addWidget(moveDownButton);
  buttonsVLayout->addStretch();
  buttonsVLayout->addWidget(runFilterButton);

  QHBoxLayout *mainlayout = new QHBoxLayout();
  mainlayout->setContentsMargins(0, 0, 0, 0);
  mainlayout->addWidget(filtersTree_);
  mainlayout->addLayout(buttonsVLayout);

  pageLayout->addLayout(mainlayout);

  buttonBox->addButton(QDialogButtonBox::Close);

  filtersTree_->setCurrentIndex(QModelIndex());

  new ReorderableTreeController(
      filtersTree_, moveUpButton, moveDownButton, nullptr,
      {editButton, deleteButton, runFilterButton},
      [](QTreeWidgetItem *moved, QTreeWidgetItem *other) {
        const QString numMoved = moved->text(3);
        moved->setText(3, other->text(3));
        other->setText(3, numMoved);
        QSqlQuery q;
        q.prepare(QStringLiteral("UPDATE filters SET num=? WHERE id=?"));
        for (QTreeWidgetItem *item : {moved, other}) {
          q.bindValue(0, item->text(3).toInt());
          q.bindValue(1, item->text(0).toInt());
          q.exec();
        }
      });
  connect(filtersTree_, &QAbstractItemView::doubleClicked, this, &NewsFiltersDialog::editFilter);
  connect(filtersTree_, &QTreeWidget::itemChanged, this, &NewsFiltersDialog::slotItemChanged);
  bindGeometrySetting(QStringLiteral("newsFiltersDlg/geometry"));
}

void NewsFiltersDialog::newFilter()
{
  QPointer<FilterRulesDialog> filterRulesDialog = new FilterRulesDialog(
        parentWidget(), -1);

  int result = filterRulesDialog->exec();
  // The parent may have been destroyed by tray Exit during the modal loop.
  if (!filterRulesDialog) return;
  if (result == QDialog::Rejected) {
    delete filterRulesDialog.data();
    return;
  }

  int filterId = filterRulesDialog->filterId_;
  delete filterRulesDialog.data();

  QSqlQuery q;
  QString qStr = QString("SELECT name, feeds, enable FROM filters WHERE id=='%1'").
      arg(filterId);
  q.exec(qStr);
  if (q.next()) {
    const QString strNameFeeds = feedScopeText(q.value(1).toString());

    QStringList treeItem;
    treeItem << QString::number(filterId)
             << q.value(0).toString()
             << strNameFeeds;
    QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
    if (q.value(2).toInt() == 1)
      treeWidgetItem->setCheckState(1, Qt::Checked);
    else
      treeWidgetItem->setCheckState(1, Qt::Unchecked);
    treeWidgetItem->setToolTip(2, strNameFeeds);
    filtersTree_->addTopLevelItem(treeWidgetItem);

    treeWidgetItem->setText(3, q.value(0).toString());
  }

  filtersTree_->setCurrentItem(
        filtersTree_->topLevelItem(filtersTree_->topLevelItemCount()-1));
}

void NewsFiltersDialog::editFilter()
{
  int filterRow = filtersTree_->currentIndex().row();
  int filterId = filtersTree_->topLevelItem(filterRow)->text(0).toInt();

  QPointer<FilterRulesDialog> filterRulesDialog = new FilterRulesDialog(
        parentWidget(), filterId);

  int result = filterRulesDialog->exec();
  // The parent may have been destroyed by tray Exit during the modal loop.
  if (!filterRulesDialog) return;
  if (result == QDialog::Rejected) {
    delete filterRulesDialog.data();
    return;
  }

  delete filterRulesDialog.data();

  QSqlQuery q;
  QString qStr = QString("SELECT name, feeds FROM filters WHERE id=='%1'").
      arg(filterId);
  q.exec(qStr);
  if (q.next()) {
    const QString strNameFeeds = feedScopeText(q.value(1).toString());

    filtersTree_->topLevelItem(filterRow)->setText(0, QString::number(filterId));
    filtersTree_->topLevelItem(filterRow)->setText(1, q.value(0).toString());
    filtersTree_->topLevelItem(filterRow)->setText(2, strNameFeeds);
    filtersTree_->topLevelItem(filterRow)->setToolTip(2, strNameFeeds);
  }
}

void NewsFiltersDialog::deleteFilter()
{
  QPointer<QMessageBox> msgBox = new QMessageBox(this);
  msgBox->setIcon(QMessageBox::Question);
  msgBox->setWindowTitle(tr("Delete Filter"));
  msgBox->setText(QString(tr("Are you sure you want to delete the filter '%1'?")).
                 arg(filtersTree_->currentItem()->text(1)));
  msgBox->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox->setDefaultButton(QMessageBox::No);

  const int result = msgBox->exec();
  if (!msgBox) return;
  delete msgBox.data();
  if (result != QMessageBox::Yes) return;

  int filterRow = filtersTree_->currentIndex().row();
  int filterId = filtersTree_->topLevelItem(filterRow)->text(0).toInt();

  QSqlQuery q;
  q.exec(QString("DELETE FROM filters WHERE id='%1'").arg(filterId));
  q.exec(QString("DELETE FROM filterConditions WHERE idFilter='%1'").arg(filterId));
  q.exec(QString("DELETE FROM filterActions WHERE idFilter='%1'").arg(filterId));
  q.finish();

  delete filtersTree_->takeTopLevelItem(filterRow);
}

void NewsFiltersDialog::applyFilter()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  int filterRow = filtersTree_->currentIndex().row();
  int filterId = filtersTree_->topLevelItem(filterRow)->text(0).toInt();
  QSqlQuery q;
  QString qStr = QString("SELECT feeds FROM filters WHERE id='%1'").
      arg(filterId);
  q.exec(qStr);
  if (q.first()) {
    const QString scope = q.value(0).toString();
    q.finish();
    // Resolve a global rule against the subscriptions that exist at execution.
    // Folder IDs in older selections are harmless and are not execution targets.
    if (!q.prepare("SELECT id FROM feeds WHERE xmlUrl!='' "
                   "AND (?='*' OR instr(?, ',' || id || ',')>0)")) {
      QApplication::restoreOverrideCursor();
      QMessageBox::warning(this, tr("Could not load feeds"), q.lastError().text());
      return;
    }
    q.addBindValue(scope);
    q.addBindValue(scope);
    if (!q.exec()) {
      QApplication::restoreOverrideCursor();
      QMessageBox::warning(this, tr("Could not load feeds"), q.lastError().text());
      return;
    }
    QList<int> feedIds;
    while (q.next()) feedIds.append(q.value(0).toInt());
    if (q.lastError().isValid()) {
      QApplication::restoreOverrideCursor();
      QMessageBox::warning(this, tr("Could not load feeds"), q.lastError().text());
      return;
    }
    q.finish();
    for (int id : feedIds)
      mainApp->runUserFilter(id, filterId);
  }

  QApplication::restoreOverrideCursor();
}

void NewsFiltersDialog::slotItemChanged(QTreeWidgetItem *item, int column)
{
  if (column == 1) {
    int enable = 0;
    if (item->checkState(1) == Qt::Checked) enable = 1;

    QSqlQuery q;
    QString qStr = QString("UPDATE filters SET enable='%1' WHERE id=='%2'").
        arg(enable).arg(item->text(0).toInt());
    q.exec(qStr);
  }
}

QString NewsFiltersDialog::feedScopeText(const QString &scope) const
{
  if (scope == "*") return tr("All feeds (including future feeds)");
  QStringList names;
  QSqlQuery q;
  q.prepare("SELECT text FROM feeds WHERE id=? AND xmlUrl!=''");
  for (const QString &id : scope.split(",", Qt::SkipEmptyParts)) {
    q.bindValue(0, id.toInt());
    if (q.exec() && q.next()) names.append(q.value(0).toString());
    q.finish();
  }
  return names.isEmpty() ? tr("No feeds") : names.join("; ");
}

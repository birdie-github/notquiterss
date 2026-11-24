// SPDX-License-Identifier: GPL-3.0-or-later
#include "labelssettingspage.h"

#include "articles/labeldialog.h"
#include "mainwindow/mainwindow.h"
#include "preferences/optionspagebuilder.h"
#include "widgets/reorderabletreecontroller.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

LabelsSettingsPage::LabelsSettingsPage(QWidget *parent,
                                       QVector<OptionSearchEntry> *searchEntries,
                                       int pageIndex)
  : QWidget(parent)
{
  labelsTree_ = new QTreeWidget(this);
  labelsTree_->setObjectName(QStringLiteral("labelsTree_"));
  labelsTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  labelsTree_->setColumnCount(5);
  labelsTree_->setColumnHidden(0, true);
  labelsTree_->setColumnHidden(2, true);
  labelsTree_->setColumnHidden(3, true);
  labelsTree_->setColumnHidden(4, true);
  labelsTree_->header()->hide();

  auto *newLabelButton = new QPushButton(optTr("Create..."), this);
  connect(newLabelButton, &QAbstractButton::clicked, this, &LabelsSettingsPage::newLabel);
  auto *editLabelButton = new QPushButton(optTr("Edit..."), this);
  connect(editLabelButton, &QAbstractButton::clicked, this, &LabelsSettingsPage::editLabel);
  auto *deleteLabelButton = new QPushButton(optTr("Delete..."), this);
  connect(deleteLabelButton, &QAbstractButton::clicked, this, &LabelsSettingsPage::deleteLabel);

  auto *moveUpLabelButton = new QPushButton(optTr("Move up"), this);
  auto *moveDownLabelButton = new QPushButton(optTr("Move down"), this);

  auto *buttonsLayout = new QVBoxLayout();
  buttonsLayout->addWidget(newLabelButton);
  buttonsLayout->addWidget(editLabelButton);
  buttonsLayout->addWidget(deleteLabelButton);
  buttonsLayout->addSpacing(10);
  buttonsLayout->addWidget(moveUpLabelButton);
  buttonsLayout->addWidget(moveDownLabelButton);
  buttonsLayout->addStretch();

  auto *labelsLayout = new QHBoxLayout(this);
  labelsLayout->setContentsMargins(0, 0, 0, 0);
  labelsLayout->addWidget(labelsTree_);
  labelsLayout->addLayout(buttonsLayout);

  new ReorderableTreeController(
      labelsTree_, moveUpLabelButton, moveDownLabelButton, nullptr,
      {editLabelButton, deleteLabelButton},
      [this](QTreeWidgetItem *moved, QTreeWidgetItem *other) {
        const QString movedId = moved->text(0);
        const QString otherId = other->text(0);
        markModified(movedId);
        markModified(otherId);
        emit labelsSwapped(movedId, otherId);
        const QString numMoved = moved->text(4);
        moved->setText(4, other->text(4));
        other->setText(4, numMoved);
      });
  connect(labelsTree_, &QAbstractItemView::doubleClicked,
          this, &LabelsSettingsPage::editLabel);

  if (searchEntries && pageIndex >= 0) {
    searchEntries->append({
        pageIndex, -1, optTr("Labels"), QString(),
        optTr("Create...") + QLatin1Char(' ') + optTr("Edit...") + QStringLiteral(" tags categories"),
        labelsTree_});
  }
}

void LabelsSettingsPage::load()
{
  if (loadLabelsOk_) return;
  QElapsedTimer timer;
  timer.start();
  loadLabelsOk_ = true;
  idLabels_.clear();

  QSqlQuery q;
  q.exec(QStringLiteral("SELECT id, name, image, color_text, color_bg, num FROM labels ORDER BY num"));
  while (q.next()) {
    const int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    if (idLabel <= 6 && MainWindow::nameLabels().at(idLabel - 1) == nameLabel) {
      nameLabel = MainWindow::trNameLabels().at(idLabel - 1);
    }
    const QByteArray byteArray = q.value(2).toByteArray();
    const QString colorText = q.value(3).toString();
    const QString colorBg = q.value(4).toString();
    const int numLabel = q.value(5).toInt();
    QPixmap imageLabel;
    if (!byteArray.isNull()) imageLabel.loadFromData(byteArray);

    auto *treeWidgetItem = new QTreeWidgetItem(QStringList{
        QString::number(idLabel), nameLabel, colorText, colorBg, QString::number(numLabel)});
    treeWidgetItem->setIcon(1, QIcon(imageLabel));
    if (!colorText.isEmpty()) treeWidgetItem->setForeground(1, QColor(colorText));
    if (!colorBg.isEmpty()) treeWidgetItem->setBackground(1, QColor(colorBg));
    labelsTree_->addTopLevelItem(treeWidgetItem);
  }
  labelsTree_->setCurrentIndex(QModelIndex());
  qInfo().noquote() << QStringLiteral("[perf] OptionsDialog::loadLabels: %1 ms").arg(timer.elapsed());
}

void LabelsSettingsPage::newLabel()
{
  LabelDialog labelDialog(this);
  if (labelDialog.exec() == QDialog::Rejected) return;

  const QString nameLabel = labelDialog.nameEdit_->text();
  const QString colorText = labelDialog.colorTextStr_;
  const QString colorBg = labelDialog.colorBgStr_;

  int idLabel = 0;
  for (int i = 0; i < labelsTree_->topLevelItemCount(); ++i) {
    idLabel = qMax(idLabel, labelsTree_->topLevelItem(i)->text(0).toInt());
  }
  // Keep IDs reserved until pending deletions and their article updates are saved.
  for (const QString &pendingId : std::as_const(idLabels_)) {
    idLabel = qMax(idLabel, pendingId.toInt());
  }
  ++idLabel;

  const QString idStr = QString::number(idLabel);
  auto *treeWidgetItem = new QTreeWidgetItem(QStringList{
      idStr, nameLabel, colorText, colorBg, idStr});
  treeWidgetItem->setIcon(1, labelDialog.icon_);
  if (!colorText.isEmpty()) treeWidgetItem->setForeground(1, QColor(colorText));
  if (!colorBg.isEmpty()) treeWidgetItem->setBackground(1, QColor(colorBg));
  labelsTree_->addTopLevelItem(treeWidgetItem);
  markModified(idStr);

  emit labelAdded(idStr, nameLabel, labelDialog.icon_);
}

void LabelsSettingsPage::editLabel()
{
  QTreeWidgetItem *treeWidgetItem = labelsTree_->currentItem();
  if (!treeWidgetItem) return;

  const QString idLabelStr = treeWidgetItem->text(0);
  LabelDialog labelDialog(this);
  labelDialog.nameEdit_->setText(treeWidgetItem->text(1));
  labelDialog.icon_ = treeWidgetItem->icon(1);
  labelDialog.colorTextStr_ = treeWidgetItem->text(2);
  labelDialog.colorBgStr_ = treeWidgetItem->text(3);
  labelDialog.loadData();

  if (labelDialog.exec() == QDialog::Rejected) return;

  const QString nameLabel = labelDialog.nameEdit_->text();
  const QString colorText = labelDialog.colorTextStr_;
  const QString colorBg = labelDialog.colorBgStr_;

  treeWidgetItem->setText(1, nameLabel);
  treeWidgetItem->setText(2, colorText);
  treeWidgetItem->setText(3, colorBg);
  treeWidgetItem->setIcon(1, labelDialog.icon_);
  if (!colorText.isEmpty()) treeWidgetItem->setForeground(1, QColor(colorText));
  if (!colorBg.isEmpty()) treeWidgetItem->setBackground(1, QColor(colorBg));
  markModified(idLabelStr);

  emit labelEdited(idLabelStr, nameLabel, labelDialog.icon_);
}

void LabelsSettingsPage::deleteLabel()
{
  const int labelRow = labelsTree_->currentIndex().row();
  if (labelRow < 0) return;
  const QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  markModified(idLabelStr);
  emit labelDeleted(idLabelStr);
  delete labelsTree_->takeTopLevelItem(labelRow);
}

void LabelsSettingsPage::apply(QSqlDatabase &db)
{
  if (!loadLabelsOk_ || idLabels_.isEmpty()) return;
  db.transaction();
  QSqlQuery q(db);

  for (const QString &idLabel : std::as_const(idLabels_)) {
    const QList<QTreeWidgetItem *> treeItems =
        labelsTree_->findItems(idLabel, Qt::MatchFixedString, 0);
    if (treeItems.isEmpty()) {
      q.exec(QStringLiteral("DELETE FROM labels WHERE id=='%1'").arg(idLabel));
      q.exec(QStringLiteral("SELECT id, label FROM news WHERE label LIKE '%,%1,%'").arg(idLabel));
      while (q.next()) {
        QString strIdLabels = q.value(1).toString();
        strIdLabels.replace(QStringLiteral(",%1,").arg(idLabel), QStringLiteral(","));
        QSqlQuery q1(db);
        q1.exec(QStringLiteral("UPDATE news SET label='%1' WHERE id=='%2'")
                    .arg(strIdLabels).arg(q.value(0).toInt()));
      }
    } else {
      QString nameLabel = treeItems.at(0)->text(1);
      if (idLabel.toInt() <= 6 && MainWindow::trNameLabels().at(idLabel.toInt() - 1) == nameLabel) {
        nameLabel = MainWindow::nameLabels().at(idLabel.toInt() - 1);
      }
      const QPixmap icon = treeItems.at(0)->icon(1).pixmap(16, 16);
      QByteArray iconData;
      QBuffer buffer(&iconData);
      buffer.open(QIODevice::WriteOnly);
      icon.save(&buffer, "PNG");
      buffer.close();
      const QString colorText = treeItems.at(0)->text(2);
      const QString colorBg = treeItems.at(0)->text(3);
      const int numLabel = treeItems.at(0)->text(4).toInt();

      q.exec(QStringLiteral("SELECT * FROM labels WHERE id=='%1'").arg(idLabel));
      if (q.next()) {
        q.prepare(QStringLiteral("UPDATE labels SET name=?, image=?, color_text=?, color_bg=?, num=? WHERE id=?"));
        q.addBindValue(nameLabel);
        q.addBindValue(iconData);
        q.addBindValue(colorText);
        q.addBindValue(colorBg);
        q.addBindValue(numLabel);
        q.addBindValue(idLabel);
        q.exec();
      } else {
        q.prepare(QStringLiteral("INSERT INTO labels(id, name, image, color_text, color_bg, num) "
                                 "VALUES (:id, :name, :image, :color_text, :color_bg, :num)"));
        q.bindValue(QStringLiteral(":id"), idLabel.toInt());
        q.bindValue(QStringLiteral(":name"), nameLabel);
        q.bindValue(QStringLiteral(":image"), iconData);
        q.bindValue(QStringLiteral(":color_text"), colorText);
        q.bindValue(QStringLiteral(":color_bg"), colorBg);
        q.bindValue(QStringLiteral(":num"), numLabel);
        q.exec();
      }
    }
  }

  db.commit();
}

void LabelsSettingsPage::markModified(const QString &idLabel)
{
  if (!idLabels_.contains(idLabel)) idLabels_.append(idLabel);
}

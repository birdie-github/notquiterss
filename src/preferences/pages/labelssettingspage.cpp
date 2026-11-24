// SPDX-License-Identifier: GPL-3.0-or-later
#include "labelssettingspage.h"

#include "articles/labeldialog.h"
#include "mainwindow/mainwindow.h"
#include "preferences/optionspagebuilder.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace {
QString optTr(const char *text)
{
  return QCoreApplication::translate("OptionsDialog", text);
}
} // namespace

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

  newLabelButton_ = new QPushButton(optTr("Create..."), this);
  connect(newLabelButton_, &QAbstractButton::clicked, this, &LabelsSettingsPage::newLabel);
  editLabelButton_ = new QPushButton(optTr("Edit..."), this);
  editLabelButton_->setEnabled(false);
  connect(editLabelButton_, &QAbstractButton::clicked, this, &LabelsSettingsPage::editLabel);
  deleteLabelButton_ = new QPushButton(optTr("Delete..."), this);
  deleteLabelButton_->setEnabled(false);
  connect(deleteLabelButton_, &QAbstractButton::clicked, this, &LabelsSettingsPage::deleteLabel);

  moveUpLabelButton_ = new QPushButton(optTr("Move up"), this);
  moveUpLabelButton_->setEnabled(false);
  connect(moveUpLabelButton_, &QAbstractButton::clicked, this, &LabelsSettingsPage::moveUpLabel);
  moveDownLabelButton_ = new QPushButton(optTr("Move down"), this);
  moveDownLabelButton_->setEnabled(false);
  connect(moveDownLabelButton_, &QAbstractButton::clicked, this, &LabelsSettingsPage::moveDownLabel);

  auto *buttonsLayout = new QVBoxLayout();
  buttonsLayout->addWidget(newLabelButton_);
  buttonsLayout->addWidget(editLabelButton_);
  buttonsLayout->addWidget(deleteLabelButton_);
  buttonsLayout->addSpacing(10);
  buttonsLayout->addWidget(moveUpLabelButton_);
  buttonsLayout->addWidget(moveDownLabelButton_);
  buttonsLayout->addStretch();

  auto *labelsLayout = new QHBoxLayout(this);
  labelsLayout->setContentsMargins(0, 0, 0, 0);
  labelsLayout->addWidget(labelsTree_);
  labelsLayout->addLayout(buttonsLayout);

  connect(labelsTree_, &QTreeWidget::currentItemChanged,
          this, &LabelsSettingsPage::currentLabelChanged);
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

void LabelsSettingsPage::moveUpLabel()
{
  const int labelRow = labelsTree_->currentIndex().row();
  if (labelRow <= 0) return;

  const QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  const QString neighborId = labelsTree_->topLevelItem(labelRow - 1)->text(0);
  markModified(idLabelStr);
  markModified(neighborId);
  emit labelsSwapped(idLabelStr, neighborId);

  const int num1 = labelsTree_->topLevelItem(labelRow)->text(4).toInt();
  const int num2 = labelsTree_->topLevelItem(labelRow - 1)->text(4).toInt();
  labelsTree_->topLevelItem(labelRow - 1)->setText(4, QString::number(num1));
  labelsTree_->topLevelItem(labelRow)->setText(4, QString::number(num2));

  QTreeWidgetItem *treeItem = labelsTree_->takeTopLevelItem(labelRow - 1);
  labelsTree_->insertTopLevelItem(labelRow, treeItem);

  if (labelsTree_->currentIndex().row() == 0) {
    moveUpLabelButton_->setEnabled(false);
  }
  if (labelsTree_->currentIndex().row() != labelsTree_->topLevelItemCount() - 1) {
    moveDownLabelButton_->setEnabled(true);
  }
}

void LabelsSettingsPage::moveDownLabel()
{
  const int labelRow = labelsTree_->currentIndex().row();
  if (labelRow < 0 || labelRow + 1 >= labelsTree_->topLevelItemCount()) return;

  const QString idLabelStr = labelsTree_->topLevelItem(labelRow)->text(0);
  const QString neighborId = labelsTree_->topLevelItem(labelRow + 1)->text(0);
  markModified(idLabelStr);
  markModified(neighborId);
  emit labelsSwapped(idLabelStr, neighborId);

  const int num1 = labelsTree_->topLevelItem(labelRow)->text(4).toInt();
  const int num2 = labelsTree_->topLevelItem(labelRow + 1)->text(4).toInt();
  labelsTree_->topLevelItem(labelRow + 1)->setText(4, QString::number(num1));
  labelsTree_->topLevelItem(labelRow)->setText(4, QString::number(num2));

  QTreeWidgetItem *treeItem = labelsTree_->takeTopLevelItem(labelRow + 1);
  labelsTree_->insertTopLevelItem(labelRow, treeItem);

  if (labelsTree_->currentIndex().row() == labelsTree_->topLevelItemCount() - 1) {
    moveDownLabelButton_->setEnabled(false);
  }
  if (labelsTree_->currentIndex().row() != 0) {
    moveUpLabelButton_->setEnabled(true);
  }
}

void LabelsSettingsPage::currentLabelChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
  const int idx = labelsTree_->indexOfTopLevelItem(current);
  moveUpLabelButton_->setEnabled(idx > 0);
  moveDownLabelButton_->setEnabled(idx >= 0 && idx < labelsTree_->topLevelItemCount() - 1);
  editLabelButton_->setEnabled(idx >= 0);
  deleteLabelButton_->setEnabled(idx >= 0);
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

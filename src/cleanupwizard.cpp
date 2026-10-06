/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
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
#include "cleanupwizard.h"

#include "mainapplication.h"
#include "database.h"
#include "settings.h"
#include "updatefeeds.h"

CleanUpWizard::CleanUpWizard(QWidget *parent)
  : QWizard(parent)
  , selectedPage_(true)
  , itemNotChecked_(false)
{
  setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Clean Up"));
  setWizardStyle(QWizard::ModernStyle);
  setOptions(QWizard::NoBackButtonOnStartPage);
  setMinimumWidth(440);
  setMinimumHeight(380);

  addPage(createChooseFeedsPage());
  addPage(createCleanUpOptionsPage());
  finishButtonText_ = buttonText(QWizard::FinishButton);

  Settings settings;
  restoreGeometry(settings.value("CleanUpWizard/geometry").toByteArray());

  connect(button(QWizard::FinishButton), SIGNAL(clicked()),
          this, SLOT(finishButtonClicked()));
  connect(this, SIGNAL(currentIdChanged(int)), SLOT(currentIdChanged(int)));
  UpdateObject *updater = mainApp->updateFeeds()->updateObject_;
  connect(this, &CleanUpWizard::signalStartCleanUp, updater, &UpdateObject::startCleanUp);
  connect(updater, &UpdateObject::signalFinishCleanUp, this, &CleanUpWizard::finishCleanUp);
  connect(updater, &UpdateObject::signalCleanUpFailed, this, &CleanUpWizard::failCleanUp);
  connect(updater, &UpdateObject::signalCleanUpRollbackFailed, this, &CleanUpWizard::waitForRollback);
  // The worker is deliberately waiting, so a queued slot could never wake it.
  // retryCleanUpRollback touches only its thread-safe semaphore.
  connect(this, &CleanUpWizard::signalRetryCleanUpRollback,
          updater, &UpdateObject::retryCleanUpRollback, Qt::DirectConnection);
}

CleanUpWizard::~CleanUpWizard()
{
  Settings settings;
  settings.setValue("CleanUpWizard/geometry", saveGeometry());
}

/*virtual*/ void CleanUpWizard::closeEvent(QCloseEvent* event)
{
  if (cleanupRunning_)
    event->ignore();
}

void CleanUpWizard::reject()
{
  // Also cover Escape and programmatic rejection while the SQL worker waits.
  if (!cleanupRunning_) QWizard::reject();
}

QWizardPage *CleanUpWizard::createChooseFeedsPage()
{
  QWizardPage *page = new QWizardPage;
  page->setTitle(tr("Choose Feeds"));

  Settings settings;
  QStringList feedsIdList = settings.value("CleanUpWizard/feedsIdList").toStringList();

  feedsTree_ = new QTreeWidget(this);
  feedsTree_->setObjectName("feedsTreeFR");
  feedsTree_->setColumnCount(3);
  feedsTree_->setColumnHidden(1, true);
  feedsTree_->setColumnHidden(2, true);
  feedsTree_->setHeaderHidden(true);

  QStringList treeItem;
  treeItem << tr("All Feeds") << "0"<< "-1";
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
  if (feedsIdList.isEmpty())
    treeWidgetItem->setCheckState(0, Qt::Checked);
  else
    treeWidgetItem->setCheckState(0, Qt::Unchecked);
  feedsTree_->addTopLevelItem(treeWidgetItem);

  QSqlQuery q;
  QQueue<int> parentIds;
  parentIds.enqueue(0);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    QString qStr = QString("SELECT text, id, image, xmlUrl FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
        arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      QString feedText = q.value(0).toString();
      QString feedIdStr = q.value(1).toString();
      QByteArray byteArray = q.value(2).toByteArray();
      QString xmlUrl = q.value(3).toString();

      treeItem.clear();
      treeItem << feedText << feedIdStr << (xmlUrl.isEmpty() ? "0" : "1");
      treeWidgetItem = new QTreeWidgetItem(treeItem);

      if (feedsIdList.contains(feedIdStr) || feedsIdList.isEmpty())
        treeWidgetItem->setCheckState(0, Qt::Checked);
      else
        treeWidgetItem->setCheckState(0, Qt::Unchecked);

      QPixmap iconItem;
      if (xmlUrl.isEmpty()) {
        iconItem.load(":/images/folder");
      }
      else {
        MainWindow *mainWindow = mainApp->mainWindow();
        if (byteArray.isNull() || mainWindow->defaultIconFeeds_) {
          iconItem.load(":/images/feed");
        }
        else {
          iconItem.loadFromData(QByteArray::fromBase64(byteArray));
        }
      }
      treeWidgetItem->setIcon(0, iconItem);

      QList<QTreeWidgetItem *> treeItems =
          feedsTree_->findItems(QString::number(parentId),
                                Qt::MatchFixedString | Qt::MatchRecursive,
                                1);
      treeItems.at(0)->addChild(treeWidgetItem);
      if (xmlUrl.isEmpty())
        parentIds.enqueue(feedIdStr.toInt());
    }
  }
  feedsTree_->expandAll();

  connect(feedsTree_, SIGNAL(itemChanged(QTreeWidgetItem*,int)),
          this, SLOT(feedItemChanged(QTreeWidgetItem*,int)));

  QVBoxLayout *layout = new QVBoxLayout;
  layout->addWidget(feedsTree_);
  page->setLayout(layout);

  return page;
}

QWizardPage *CleanUpWizard::createCleanUpOptionsPage()
{
  QWizardPage *page = new QWizardPage;
  page->setTitle(tr("Clean Up Options"));

  dayCleanUpOn_ = new QCheckBox(tr("Maximum article age to keep (days):"));
  maxDayCleanUp_ = new QSpinBox();
  maxDayCleanUp_->setEnabled(false);
  maxDayCleanUp_->setRange(0, 9999);
  connect(dayCleanUpOn_, SIGNAL(toggled(bool)),
          maxDayCleanUp_, SLOT(setEnabled(bool)));

  newsCleanUpOn_ = new QCheckBox(tr("Maximum number of articles to keep:"));
  maxNewsCleanUp_ = new QSpinBox();
  maxNewsCleanUp_->setEnabled(false);
  maxNewsCleanUp_->setRange(0, 9999);
  connect(newsCleanUpOn_, SIGNAL(toggled(bool)),
          maxNewsCleanUp_, SLOT(setEnabled(bool)));

  readCleanUp_ = new QCheckBox(tr("Delete read articles"));
  neverUnreadCleanUp_ = new QCheckBox(tr("Never delete unread articles"));
  neverStarCleanUp_ = new QCheckBox(tr("Never delete starred articles"));
  neverLabelCleanUp_ = new QCheckBox(tr("Never delete labeled articles"));

  QGridLayout *cleanUpFeedsLayout = new QGridLayout();
  cleanUpFeedsLayout->setColumnStretch(1, 1);
  cleanUpFeedsLayout->addWidget(dayCleanUpOn_, 0, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(maxDayCleanUp_, 0, 1, 1, 1, Qt::AlignLeft);
  cleanUpFeedsLayout->addWidget(newsCleanUpOn_, 1, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(maxNewsCleanUp_, 1, 1, 1, 1, Qt::AlignLeft);
  cleanUpFeedsLayout->addWidget(readCleanUp_, 2, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverUnreadCleanUp_, 3, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverStarCleanUp_, 4, 0, 1, 1);
  cleanUpFeedsLayout->addWidget(neverLabelCleanUp_, 5, 0, 1, 1);

  cleanUpDeleted_ = new QCheckBox(tr("Clean up 'Deleted'"));
  fullCleanUp_ = new QCheckBox(tr("Purge database"));
  QVBoxLayout *fullCleanUpDescriptionLayout = new QVBoxLayout;
  fullCleanUpDescriptionLayout->setContentsMargins(15, 0, 0, 0);
  fullCleanUpDescriptionLayout->addWidget(
        new QLabel(tr("Permanently remove records marked as deleted from the database.\n"
                      "Deleted articles may be downloaded again.")));

  progressBar_ = new QProgressBar(this);
  progressBar_->setObjectName("progressBar_");
  progressBar_->setTextVisible(false);
  progressBar_->setFixedHeight(15);
  progressBar_->setMinimum(0);
  progressBar_->setMaximum(0);
  progressBar_->setVisible(false);
  rollbackErrorLabel_ = new QLabel(this);
  rollbackErrorLabel_->setTextFormat(Qt::PlainText);
  rollbackErrorLabel_->setWordWrap(true);
  rollbackErrorLabel_->hide();

  Settings settings("CleanUpWizard");
  maxDayCleanUp_->setValue(settings.value("maxDayClearUp", 30).toInt());
  maxNewsCleanUp_->setValue(settings.value("maxNewsClearUp", 200).toInt());
  dayCleanUpOn_->setChecked(settings.value("dayClearUpOn", true).toBool());
  newsCleanUpOn_->setChecked(settings.value("newsClearUpOn", true).toBool());
  readCleanUp_->setChecked(settings.value("readClearUp", false).toBool());
  neverUnreadCleanUp_->setChecked(settings.value("neverUnreadClearUp", true).toBool());
  neverStarCleanUp_->setChecked(settings.value("neverStarClearUp", true).toBool());
  neverLabelCleanUp_->setChecked(settings.value("neverLabelClearUp", true).toBool());
  cleanUpDeleted_->setChecked(settings.value("cleanUpDeleted", true).toBool());
  fullCleanUp_->setChecked(settings.value("fullCleanUp", false).toBool());

  QVBoxLayout *layout = new QVBoxLayout(page);
  layout->addLayout(cleanUpFeedsLayout);
  layout->addSpacing(10);
  layout->addWidget(cleanUpDeleted_);
  layout->addSpacing(10);
  layout->addWidget(fullCleanUp_);
  layout->addLayout(fullCleanUpDescriptionLayout);
  layout->addStretch(1);
  layout->addWidget(rollbackErrorLabel_);
  layout->addWidget(progressBar_);

  return page;
}

/*virtual*/ bool CleanUpWizard::validateCurrentPage()
{
  if (!selectedPage_) {
    return false;
  }
  return true;
}

void CleanUpWizard::feedItemChanged(QTreeWidgetItem *item, int column)
{
  if ((column != 0) || itemNotChecked_) return;

  itemNotChecked_ = true;
  if (item->checkState(0) == Qt::Unchecked) {
    setCheckStateItem(item, Qt::Unchecked);

    QTreeWidgetItem *parentItem = item->parent();
    while (parentItem) {
      parentItem->setCheckState(0, Qt::Unchecked);
      parentItem = parentItem->parent();
    }
  } else {
    setCheckStateItem(item, Qt::Checked);
  }
  itemNotChecked_ = false;
}

void CleanUpWizard::setCheckStateItem(QTreeWidgetItem *item, Qt::CheckState state)
{
  for (int i = 0; i < item->childCount(); ++i) {
    QTreeWidgetItem *childItem = item->child(i);
    childItem->setCheckState(0, state);
    setCheckStateItem(childItem, state);
  }
}

void CleanUpWizard::currentIdChanged(int idPage)
{
  if (idPage == 1)
    selectedPage_ = false;
  else
    selectedPage_ = true;
}

void CleanUpWizard::finishButtonClicked()
{
  if (rollbackRecovery_) {
    rollbackRecovery_ = false;
    button(QWizard::FinishButton)->setEnabled(false);
    progressBar_->show();
    emit signalRetryCleanUpRollback();
    return;
  }
  if (cleanupRunning_) return;
  cleanupRunning_ = true;
  button(QWizard::BackButton)->setEnabled(false);
  button(QWizard::CancelButton)->setEnabled(false);
  button(QWizard::FinishButton)->setEnabled(false);
  page(1)->setEnabled(false);
  progressBar_->show();
  qApp->processEvents();

  feedsTree_->expandAll();

  QStringList feedsIdList;
  QList<int> foldersIdList;
  QTreeWidgetItem *treeItem = feedsTree_->itemBelow(feedsTree_->topLevelItem(0));
  while (treeItem) {
    if (treeItem->checkState(0) == Qt::Checked)
      feedsIdList << treeItem->text(1);
    if (treeItem->text(2).toInt() == 0)
      foldersIdList << treeItem->text(1).toInt();
    treeItem = feedsTree_->itemBelow(treeItem);
  }

  Settings settings("CleanUpWizard");
  settings.setValue("feedsIdList", feedsIdList);
  settings.setValue("maxDayClearUp", maxDayCleanUp_->value());
  settings.setValue("maxNewsClearUp", maxNewsCleanUp_->value());
  settings.setValue("dayClearUpOn", dayCleanUpOn_->isChecked());
  settings.setValue("newsClearUpOn", newsCleanUpOn_->isChecked());
  settings.setValue("readClearUp", readCleanUp_->isChecked());
  settings.setValue("neverUnreadClearUp", neverUnreadCleanUp_->isChecked());
  settings.setValue("neverStarClearUp", neverStarCleanUp_->isChecked());
  settings.setValue("neverLabelClearUp", neverLabelCleanUp_->isChecked());
  settings.setValue("cleanUpDeleted", cleanUpDeleted_->isChecked());
  settings.setValue("fullCleanUp", fullCleanUp_->isChecked());

  emit signalStartCleanUp(false, feedsIdList, foldersIdList);
}

void CleanUpWizard::failCleanUp(const QString &error)
{
  cleanupRunning_ = false;
  rollbackRecovery_ = false;
  rollbackErrorLabel_->hide();
  setButtonText(QWizard::FinishButton, finishButtonText_);
  progressBar_->hide();
  page(1)->setEnabled(true);
  button(QWizard::BackButton)->setEnabled(true);
  button(QWizard::CancelButton)->setEnabled(true);
  button(QWizard::FinishButton)->setEnabled(true);
  selectedPage_ = false;
  QMessageBox::critical(this, tr("Cleanup failed"), error);
}

void CleanUpWizard::waitForRollback(const QString &error)
{
  rollbackRecovery_ = true;
  progressBar_->hide();
  rollbackErrorLabel_->setText(tr("Cleanup could not be rolled back. Database work is paused. "
                                 "Fix the problem and choose Retry rollback.\n\n%1").arg(error));
  rollbackErrorLabel_->show();
  setButtonText(QWizard::FinishButton, tr("Retry rollback"));
  button(QWizard::FinishButton)->setEnabled(true);
}

void CleanUpWizard::finishCleanUp(int countDeleted, const QString &warning)
{
  int feedId = -1;
  MainWindow *mainWindow = mainApp->mainWindow();

  feedsTree_->expandAll();
  QTreeWidgetItem *treeItem = feedsTree_->itemBelow(feedsTree_->topLevelItem(0));
  while (treeItem) {
    if (treeItem->checkState(0) == Qt::Checked) {
      if (mainWindow->currentNewsTab->feedId_ == treeItem->text(1).toInt())
        feedId = treeItem->text(1).toInt();
    }
    treeItem = feedsTree_->itemBelow(treeItem);
  }
  if (feedId != -1)
    mainWindow->slotUpdateNews(NewsTabWidget::RefreshAll);
  mainWindow->slotUpdateStatus(feedId, false);

  mainWindow->feedsModelReload();
  mainWindow->recountCategoryCounts();

  selectedPage_ = true;
  cleanupRunning_ = false;

  accept();

  QString message = tr("Cleanup wizard deleted %1 articles").arg(countDeleted);
  if (warning.isEmpty()) {
    QMessageBox::information(this, tr("Information"), message);
  } else {
    QMessageBox::warning(this, tr("Cleanup"), message + "\n\n" + warning);
  }
}

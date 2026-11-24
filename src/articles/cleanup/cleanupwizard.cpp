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
#include "cleanupwizard.h"

#include "app/mainapplication.h"
#include "database/database.h"
#include "preferences/optionspagebuilder.h"
#include "preferences/settings.h"
#include "feeds/feedselectiontree.h"
#include "feeds/update/updatefeeds.h"

#include <QDebug>
#include <QSet>
#include <QSignalBlocker>
#include <utility>

CleanUpWizard::CleanUpWizard(QWidget *parent)
  : QWizard(parent)
  , selectedPage_(true)
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

  connect(button(QWizard::FinishButton), &QAbstractButton::clicked,
          this, &CleanUpWizard::finishButtonClicked);
  connect(this, &QWizard::currentIdChanged, this, &CleanUpWizard::currentIdChanged);
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
  FeedSelectionTree::configure(feedsTree_, QStringLiteral("feedsTreeFR"));

  QString treeError;
  if (!FeedSelectionTree::populate(feedsTree_, QSqlDatabase::database(),
                                   mainApp->mainWindow()->defaultIconFeeds_,
                                   tr("All Feeds"), -1, treeError)) {
    qWarning().noquote() << treeError;
  }

  {
    const QSignalBlocker blocker(feedsTree_);
    QSet<QString> savedIds;
    for (const QString &id : std::as_const(feedsIdList)) savedIds.insert(id);
    const bool checkAll = savedIds.isEmpty();
    QTreeWidgetItem *root = feedsTree_->topLevelItem(0);
    for (QTreeWidgetItemIterator it(feedsTree_); *it; ++it) {
      QTreeWidgetItem *item = *it;
      const bool checked = (item == root) ? checkAll : (checkAll || savedIds.contains(item->text(1)));
      item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
  }

  connect(feedsTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
    if (column == 0) FeedSelectionTree::cascadeChecks(feedsTree_, item);
  });

  QVBoxLayout *layout = new QVBoxLayout;
  layout->addWidget(feedsTree_);
  page->setLayout(layout);

  return page;
}

QWizardPage *CleanUpWizard::createCleanUpOptionsPage()
{
  auto *page = new QWizardPage;
  page->setTitle(tr("Clean Up Options"));

  auto *layout = new QVBoxLayout(page);
  OptionsPageBuilder b(page, layout, -1, -1, nullptr);

  b.grid(std::nullopt, 1, [this, page](OptionsGridBuilder &g) {
    const auto addToggleSpin = [&](int row, QCheckBox *&box, const QString &label, QSpinBox *&spin) {
      g.check(row, 0, box, label);
      spin = new QSpinBox(page);
      spin->setRange(0, 9999);
      g.addWidget(row, 1, spin, Qt::AlignLeft);
      OptionsPageBuilder::bindEnabled(box, {spin});
    };
    addToggleSpin(0, dayCleanUpOn_, tr("Maximum article age to keep (days):"), maxDayCleanUp_);
    addToggleSpin(1, newsCleanUpOn_, tr("Maximum number of articles to keep:"), maxNewsCleanUp_);
    g.check(2, 0, readCleanUp_, tr("Delete read articles"));
    g.check(3, 0, neverUnreadCleanUp_, tr("Never delete unread articles"));
    g.check(4, 0, neverStarCleanUp_, tr("Never delete starred articles"));
    g.check(5, 0, neverLabelCleanUp_, tr("Never delete labeled articles"));
  });

  b.spacing(10);
  b.check(cleanUpDeleted_, tr("Clean up 'Deleted'"));
  b.spacing(10);
  b.check(fullCleanUp_, tr("Purge database"));
  b.indented(QMargins(15, 0, 0, 0), [](OptionsPageBuilder &sub) {
    sub.label(tr("Permanently remove records marked as deleted from the database.\n"
                 "Deleted articles may be downloaded again."));
  });

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

  b.stretch(1);
  b.addWidget(rollbackErrorLabel_);
  b.addWidget(progressBar_);

  return page;
}

/*virtual*/ bool CleanUpWizard::validateCurrentPage()
{
  if (!selectedPage_) {
    return false;
  }
  return true;
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
    if (!treeItem->data(0, FeedSelectionTree::IsFeedRole).toBool())
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

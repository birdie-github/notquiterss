// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "feedpropertiesdialog.h"
#include "feedbulksettings.h"
#include "databasebackup.h"
#include "settings.h"

void MainWindow::applyBulkFeedSettings(FeedPropertiesDialog *dialog)
{
  const auto changes = dialog->bulkChanges();
  if (changes.values().isEmpty()) return;

  const QList<int> ids = dialog->bulkFeedIds();
  QString error;
  if (ids.isEmpty()) return;

  if (!bulkFeedSettingsConfirmed_) {
    QMessageBox confirmation(QMessageBox::Warning,
        tr("Apply settings to %1 feeds?").arg(ids.size()),
        tr("Apply %1 to %2 selected feeds? Only this setting will be replaced. Other feed settings and folder settings will remain unchanged.").arg(dialog->bulkActionName()).arg(ids.size()),
        QMessageBox::Cancel, dialog);
    confirmation.setTextFormat(Qt::PlainText);
    auto *apply = confirmation.addButton(tr("Apply"), QMessageBox::AcceptRole);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != apply) return;
    bulkFeedSettingsConfirmed_ = true;
  }

  if (!FeedBulkSettings::apply(db_, ids, changes, error)) {
    QMessageBox::warning(dialog, tr("Could not apply feed settings"), error);
    return;
  }

  const auto values = changes.values();
  for (int id : ids) {
    const QPersistentModelIndex index = feedsModel_->indexById(id);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      feedsModel_->setData(feedsModel_->indexSibling(index, it.key()), it.value());
    if (changes.schedule) {
      const auto &schedule = *changes.schedule;
      if (schedule.mode == 1) {
        const int multiplier = schedule.unit == -1 ? 1 : (schedule.unit == 0 ? 60 : 3600);
        updateFeedsIntervalSec_.insert(id, schedule.interval * multiplier);
        updateFeedsTimeCount_.insert(id, 0);
      } else {
        updateFeedsIntervalSec_.remove(id);
        updateFeedsTimeCount_.remove(id);
      }
    }
  }
  refreshFeedSettings(ids, changes.columns.has_value(), changes.images.has_value(),
                      changes.rightToLeft.has_value());
  feedsView_->viewport()->update();
  DatabaseBackup::subscriptionsChanged();
  dialog->bulkApplySucceeded(ids.size());
}

void MainWindow::saveFolderProperties(FeedPropertiesDialog *dialog, int folderId)
{
  const auto properties = dialog->getFeedProperties();
  const auto columns = dialog->columnSettings();
  const bool changeDisabled = dialog->folderDisableChanged();
  if (changeDisabled && !db_.transaction()) {
    QMessageBox::warning(this, tr("Could not save folder properties"), db_.lastError().text());
    return;
  }
  QSqlQuery q(db_);
  auto fail = [&](QString error) {
    q.finish();
    if (changeDisabled && !db_.rollback()) error += "\n" + db_.lastError().text();
    QMessageBox::warning(this, tr("Could not save folder properties"), error);
  };
  q.prepare("UPDATE feeds SET text = ?, displayEmbeddedImages = ?, layoutDirection = ?, "
            "columns = ?, sort = ?, sortType = ? WHERE id = ? AND COALESCE(xmlUrl, '') = ''");
  q.addBindValue(properties.general.text);
  q.addBindValue(properties.display.displayEmbeddedImages);
  q.addBindValue(properties.display.layoutDirection);
  q.addBindValue(columns.columns);
  q.addBindValue(columns.sortBy);
  q.addBindValue(columns.sortOrder);
  q.addBindValue(folderId);
  if (!q.exec() || q.numRowsAffected() != 1) {
    fail(q.lastError().isValid() ? q.lastError().text() : tr("The folder no longer exists."));
    return;
  }
  q.finish();

  QList<int> affectedFeeds;
  if (changeDisabled) {
    const QString subtree =
        "WITH RECURSIVE subtree(id) AS ("
        "SELECT id FROM feeds WHERE parentId = ? "
        "UNION SELECT f.id FROM feeds f JOIN subtree s ON f.parentId = s.id) ";
    if (!q.prepare(subtree + "SELECT id FROM feeds WHERE id IN (SELECT id FROM subtree) AND xmlUrl != ''")) {
      fail(q.lastError().text());
      return;
    }
    q.addBindValue(folderId);
    if (!q.exec()) { fail(q.lastError().text()); return; }
    while (q.next()) affectedFeeds.append(q.value(0).toInt());
    if (q.lastError().isValid()) { fail(q.lastError().text()); return; }
    q.finish();
    if (!q.prepare(subtree + "UPDATE feeds SET disableUpdate = ? WHERE id IN (SELECT id FROM subtree) AND xmlUrl != ''")) {
      fail(q.lastError().text());
      return;
    }
    q.addBindValue(folderId);
    q.addBindValue(properties.general.disableUpdate ? 1 : 0);
    if (!q.exec()) { fail(q.lastError().text()); return; }
    q.finish();
    if (!db_.commit()) { fail(db_.lastError().text()); return; }
  }

  // Update the cached model only after all database writes succeed. Schedules
  // and their timer counters are deliberately untouched by this bulk toggle.
  for (int id : affectedFeeds) {
    const auto feed = feedsModel_->indexById(id);
    feedsModel_->setData(feedsModel_->indexSibling(feed, "disableUpdate"),
                        properties.general.disableUpdate ? 1 : 0);
  }
  const QPersistentModelIndex index = feedsModel_->indexById(folderId);
  const bool imagesChanged = feedsModel_->dataField(index, "displayEmbeddedImages").toInt()
      != properties.display.displayEmbeddedImages;
  const bool directionChanged = feedsModel_->dataField(index, "layoutDirection").toInt()
      != properties.display.layoutDirection;
  const bool columnsChanged = feedsModel_->dataField(index, "columns").toString() != columns.columns ||
      feedsModel_->dataField(index, "sort").toInt() != columns.sortBy ||
      feedsModel_->dataField(index, "sortType").toInt() != columns.sortOrder;
  feedsModel_->setData(feedsModel_->indexSibling(index, "text"), properties.general.text);
  feedsModel_->setData(feedsModel_->indexSibling(index, "displayEmbeddedImages"), properties.display.displayEmbeddedImages);
  feedsModel_->setData(feedsModel_->indexSibling(index, "layoutDirection"), properties.display.layoutDirection);
  feedsModel_->setData(feedsModel_->indexSibling(index, "columns"), columns.columns);
  feedsModel_->setData(feedsModel_->indexSibling(index, "sort"), columns.sortBy);
  feedsModel_->setData(feedsModel_->indexSibling(index, "sortType"), columns.sortOrder);
  for (int i = 0; i < stackedWidget_->count(); ++i) {
    auto *tab = qobject_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (tab && tab->type_ == NewsTabWidget::TabTypeFeed && tab->feedId_ == folderId)
      tab->setTextTab(properties.general.text);
  }
  refreshFeedSettings({folderId}, columnsChanged, imagesChanged, directionChanged);
  feedsView_->viewport()->update();
  DatabaseBackup::subscriptionsChanged();
}

void MainWindow::refreshFeedSettings(const QList<int> &ids, bool columns, bool images, bool direction)
{
  for (int i = 0; i < stackedWidget_->count(); ++i) {
    auto *tab = qobject_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (!tab || tab->type_ == NewsTabWidget::TabTypeDownloads) continue;
    const bool ownView = tab->type_ == NewsTabWidget::TabTypeFeed && ids.contains(tab->feedId_);
    if (ownView && columns)
      tab->newsHeader_->setColumns(feedsModel_->indexById(tab->feedId_));
    // Category article views use the source feed's direction. Folder views use
    // their own settings, so bulk feed edits must not change their presentation.
    bool sourceDirection = false;
    if (direction && tab->type_ != NewsTabWidget::TabTypeFeed &&
        tab->newsView_->currentIndex().isValid()) {
      const int row = tab->newsView_->currentIndex().row();
      sourceDirection = ids.contains(tab->newsModel_->dataField(row, "feedId").toInt());
    }
    if ((ownView && (images || direction)) || sourceDirection)
      tab->refreshFeedDisplay(ownView && images);
  }
}

QString MainWindow::updateScheduleDescription() const
{
  if (!updateFeedsEnable_) return tr("automatic updates disabled");
  if (updateFeedsIntervalType_ == -1)
    return updateFeedsInterval_ == 1 ? tr("every second") : tr("every %1 seconds").arg(updateFeedsInterval_);
  if (updateFeedsIntervalType_ == 0)
    return updateFeedsInterval_ == 1 ? tr("every minute") : tr("every %1 minutes").arg(updateFeedsInterval_);
  return updateFeedsInterval_ == 1 ? tr("every hour") : tr("every %1 hours").arg(updateFeedsInterval_);
}

void MainWindow::showBulkFeedSettings()
{
  FeedPropertiesDialog dialog(false, this, true);
  FEED_PROPERTIES properties{};
  properties.general.useGlobalUpdate = true;
  properties.general.globalUpdateDescription = updateScheduleDescription();
  properties.general.updateInterval = updateFeedsInterval_;
  properties.general.intervalType = updateFeedsIntervalType_;
  properties.general.avoidedOldSingleNewsDate = QDate::currentDate();
  properties.display.displayEmbeddedImages = 1;

  Settings settings("NewsHeader");
  for (const QString &column : settings.value("columns").toString().split(",", Qt::SkipEmptyParts))
    properties.columnDefault.columns.append(column.toInt());
  auto *tab = qobject_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
  properties.columnDefault.sortBy = settings.value("sortBy", tab->newsModel_->fieldIndex("published")).toInt();
  properties.columnDefault.sortType = settings.value("sortOrder", Qt::DescendingOrder).toInt();
  for (QAction *action : tab->newsHeader_->viewMenu_->actions()) {
    properties.column.indexList.append(action->data().toInt());
    properties.column.nameList.append(action->text());
    if (action->isChecked()) properties.column.columns.append(action->data().toInt());
  }
  if (!properties.columnDefault.columns.isEmpty())
    properties.column.columns = properties.columnDefault.columns;
  properties.column.sortBy = properties.columnDefault.sortBy;
  properties.column.sortType = properties.columnDefault.sortType;
  dialog.setFeedProperties(properties);
  QString error;
  if (!dialog.loadBulkTargets(db_, defaultIconFeeds_, error)) {
    QMessageBox::warning(this, tr("Could not load feeds"), error);
    return;
  }
  connect(&dialog, &FeedPropertiesDialog::applyRequested, &dialog, [this, &dialog] {
    applyBulkFeedSettings(&dialog);
  });
  dialog.exec();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "credentialscope.h"
#include "databasebackup.h"
#include "feedbulksettings.h"
#include "feedpropertiesdialog.h"
#include "mainapplication.h"
#include "settings.h"
#include <QTimeZone>
#include <utility>

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
  }
  refreshFeedSettings(ids, changes.columns.has_value(),
                      changes.images.has_value() || changes.preloadImages.has_value(),
                      changes.rightToLeft.has_value());
  feedsView_->viewport()->update();
  DatabaseBackup::subscriptionsChanged("apply bulk feed settings");
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
  const int preloadMode = properties.display.useGlobalPreload ? -1 : properties.display.preloadPages;
  q.prepare("UPDATE feeds SET text = ?, displayEmbeddedImages = ?, preloadImages = ?, layoutDirection = ?, "
            "columns = ?, sort = ?, sortType = ? WHERE id = ? AND COALESCE(xmlUrl, '') = ''");
  q.addBindValue(properties.general.text);
  q.addBindValue(properties.display.displayEmbeddedImages);
  q.addBindValue(preloadMode);
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
  const QVariant oldPreloadVar = feedsModel_->dataField(index, "preloadImages");
  const int oldPreloadMode = oldPreloadVar.isNull() ? -1 : oldPreloadVar.toInt();
  const bool imagesChanged = feedsModel_->dataField(index, "displayEmbeddedImages").toInt()
      != properties.display.displayEmbeddedImages || oldPreloadMode != preloadMode;
  const bool directionChanged = feedsModel_->dataField(index, "layoutDirection").toInt()
      != properties.display.layoutDirection;
  const bool columnsChanged = feedsModel_->dataField(index, "columns").toString() != columns.columns ||
      feedsModel_->dataField(index, "sort").toInt() != columns.sortBy ||
      feedsModel_->dataField(index, "sortType").toInt() != columns.sortOrder;
  feedsModel_->setData(feedsModel_->indexSibling(index, "text"), properties.general.text);
  feedsModel_->setData(feedsModel_->indexSibling(index, "displayEmbeddedImages"), properties.display.displayEmbeddedImages);
  feedsModel_->setData(feedsModel_->indexSibling(index, "preloadImages"), preloadMode);
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
  DatabaseBackup::subscriptionsChanged("save folder properties");
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

QString MainWindow::imagePreloadDescription() const
{
  if (!autoLoadImages_ || preloadNewspaperPages_ <= 0)
    return tr("disabled");
  return preloadNewspaperPages_ == 1
      ? tr("up to 1 page")
      : tr("up to %1 pages").arg(preloadNewspaperPages_);
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
  properties.display.useGlobalPreload = true;
  properties.display.globalPreloadDescription = imagePreloadDescription();
  properties.display.preloadPages = preloadNewspaperPages_;

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

void MainWindow::showFeedPropertiesDlg()
{
  if (!feedsView_->selectIndex().isValid()) {
    feedProperties_->setEnabled(false);
    return;
  }

  QPersistentModelIndex index = feedsView_->selectIndex();
  int feedId = feedsModel_->idByIndex(index);
  bool isFeed = (index.isValid() && feedsModel_->isFolder(index)) ? false : true;

  FeedPropertiesDialog *feedPropertiesDialog = new FeedPropertiesDialog(isFeed, this);

  FEED_PROPERTIES properties{};
  FEED_PROPERTIES properties_tmp{};

  QByteArray byteArray = feedsModel_->dataField(index, "image").toByteArray();
  if (!byteArray.isNull()) {
    QPixmap icon;
    icon.loadFromData(QByteArray::fromBase64(byteArray));
    feedPropertiesDialog->setWindowIcon(icon);
  } else if (isFeed) {
    feedPropertiesDialog->setWindowIcon(QPixmap(":/images/feed"));
  } else {
    feedPropertiesDialog->setWindowIcon(QPixmap(":/images/folder"));
  }
  properties.general.image = QByteArray::fromBase64(byteArray);

  QString str(feedPropertiesDialog->windowTitle() +
              " '" +
              feedsModel_->dataField(index, "text").toString() +
              "'");
  feedPropertiesDialog->setWindowTitle(str);

  properties.general.text =
      feedsModel_->dataField(index, "text").toString();
  properties.general.title =
      feedsModel_->dataField(index, "title").toString();
  properties.general.url =
      feedsModel_->dataField(index, "xmlUrl").toString();
  properties.general.homepage =
      feedsModel_->dataField(index, "htmlUrl").toString();
  properties.general.displayOnStartup =
      feedsModel_->dataField(index, "displayOnStartup").toInt();
  properties.display.displayEmbeddedImages =
      feedsModel_->dataField(index, "displayEmbeddedImages").toInt();
  const QVariant preloadVar = feedsModel_->dataField(index, "preloadImages");
  const int preloadVal = preloadVar.isNull() ? -1 : preloadVar.toInt();
  properties.display.useGlobalPreload = (preloadVal < 0);
  properties.display.globalPreloadDescription = imagePreloadDescription();
  properties.display.preloadPages = preloadVal < 0 ? preloadNewspaperPages_ : qBound(0, preloadVal, 10);
  properties.display.javaScriptEnable =
      feedsModel_->dataField(index, "javaScriptEnable").toInt();
  properties.display.displayNews = feedsModel_->dataField(index, "displayNews").toInt();
  properties.display.layoutDirection =
      feedsModel_->dataField(index, "layoutDirection").toInt();

  properties.general.disableUpdate =
      feedsModel_->dataField(index, "disableUpdate").toBool();

  properties.general.useGlobalUpdate =
      feedsModel_->dataField(index, "updateIntervalEnable").isNull() ||
      feedsModel_->dataField(index, "updateIntervalEnable").toInt() == -1;
  properties.general.globalUpdateDescription = updateScheduleDescription();
  if (properties.general.useGlobalUpdate) {
    properties.general.updateEnable = updateFeedsEnable_;
    properties.general.updateInterval = updateFeedsInterval_;
    properties.general.intervalType = updateFeedsIntervalType_;
  } else {
    properties.general.updateEnable =
        feedsModel_->dataField(index, "updateIntervalEnable").toBool();
    properties.general.updateInterval =
        feedsModel_->dataField(index, "updateInterval").toInt();
    properties.general.intervalType =
        feedsModel_->dataField(index, "updateIntervalType").toInt();
  }

  if (feedsModel_->dataField(index, "label").toString().contains("starred"))
    properties.general.starred = true;
  else
    properties.general.starred = false;

  properties.general.duplicateNewsMode =
      feedsModel_->dataField(index, "duplicateNewsMode").toBool();

  properties.general.addSingleNewsAnyDateOn =
      feedsModel_->dataField(index, "addSingleNewsAnyDateOn").toBool();

  properties.general.avoidedOldSingleNewsDateOn =
      feedsModel_->dataField(index, "avoidedOldSingleNewsDateOn").toBool();

  properties.general.avoidedOldSingleNewsDate =
      feedsModel_->dataField(index, "avoidedOldSingleNewsDate").toDate();

  Settings settings("NewsHeader");
  QString indexColumnsStr = settings.value("columns").toString();
  QStringList indexColumnsList = indexColumnsStr.split(",", Qt::SkipEmptyParts);
  for (const QString &indexStr : std::as_const(indexColumnsList)) {
    properties.columnDefault.columns.append(indexStr.toInt());
  }
  NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
  int sortBy = settings.value("sortBy", widget->newsModel_->fieldIndex("published")).toInt();
  properties.columnDefault.sortBy = sortBy;
  int sortType = settings.value("sortOrder", Qt::DescendingOrder).toInt();
  properties.columnDefault.sortType = sortType;

  if (feedsModel_->dataField(index, "columns").toString().isEmpty()) {
    widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
    QListIterator<QAction *> iter(widget->newsHeader_->viewMenu_->actions());
    while (iter.hasNext()) {
      QAction *nextAction = iter.next();
      properties.column.indexList.append(nextAction->data().toInt());
      properties.column.nameList.append(nextAction->text());
      if (nextAction->isChecked())
        properties.column.columns.append(nextAction->data().toInt());
    }
    int section = widget->newsHeader_->sortIndicatorSection();
    properties.column.sortBy = section;
    if (widget->newsHeader_->sortIndicatorOrder() == Qt::AscendingOrder) {
      properties.column.sortType = 0;
    } else {
      properties.column.sortType = 1;
    }
  } else {
    widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(TAB_WIDGET_PERMANENT));
    QListIterator<QAction *> iter(widget->newsHeader_->viewMenu_->actions());
    while (iter.hasNext()) {
      QAction *nextAction = iter.next();
      properties.column.indexList.append(nextAction->data().toInt());
      properties.column.nameList.append(nextAction->text());
    }
    indexColumnsStr = feedsModel_->dataField(index, "columns").toString();
    indexColumnsList = indexColumnsStr.split(",", Qt::SkipEmptyParts);
    for (const QString &indexStr : std::as_const(indexColumnsList)) {
      properties.column.columns.append(indexStr.toInt());
    }
    properties.column.sortBy = feedsModel_->dataField(index, "sort").toInt();
    properties.column.sortType = feedsModel_->dataField(index, "sortType").toInt();
  }

  properties.authentication.on = false;
  if (feedsModel_->dataField(index, "authentication").toInt() == 1) {
    properties.authentication.on = true;
  }
  QUrl url(feedsModel_->dataField(index, "xmlUrl").toString());
  QSqlQuery q;
  q.prepare("SELECT username, password FROM passwords WHERE server=?");
  q.addBindValue(CredentialScope::feed(url));
  q.exec();
  if (q.next()) {
    properties.authentication.user = q.value(0).toString();
    properties.authentication.pass = QString::fromUtf8(QByteArray::fromBase64(q.value(1).toByteArray()));
  }

  properties.status.feedStatus = feedsModel_->dataField(index, "status").toString();

  QDateTime dtLocalTime = QDateTime::currentDateTime();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), QTimeZone(QTimeZone::UTC));
#else
  QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
#endif
  int nTimeShift = dtLocalTime.secsTo(dtUTC);

  QDateTime dt = QDateTime::fromString(
        feedsModel_->dataField(index, "created").toString(),
        Qt::ISODate);
  properties.status.createdTime = dt.addSecs(nTimeShift);

  dt = QDateTime::fromString(
        feedsModel_->dataField(index, "updated").toString(),
        Qt::ISODate);
  properties.status.lastUpdate = dt.addSecs(nTimeShift);

  dt = QDateTime::fromString(
        feedsModel_->dataField(index, "lastBuildDate").toString(),
        Qt::ISODate);
  properties.status.lastBuildDate = dt.addSecs(nTimeShift);

  properties.status.undeleteCount = feedsModel_->dataField(index, "undeleteCount").toInt();
  properties.status.newCount      = feedsModel_->dataField(index, "newCount").toInt();
  properties.status.unreadCount   = feedsModel_->dataField(index, "unread").toInt();
  properties.status.description   = feedsModel_->dataField(index, "description").toString();

  properties.status.feedsCount = 0;
  if (!isFeed) {
    QQueue<int> parentIds;
    parentIds.enqueue(feedId);
    while (!parentIds.empty()) {
      int parentId = parentIds.dequeue();
      q.exec(QString("SELECT id, xmlUrl FROM feeds WHERE parentId='%1'").arg(parentId));
      while (q.next()) {
        QString xmlUrl = q.value(1).toString();
        if (xmlUrl.isEmpty())
          parentIds.enqueue(q.value(0).toInt());
        else
          properties.status.feedsCount++;
      }
    }
  }

  feedPropertiesDialog->setFeedProperties(properties);
  if (!isFeed) {
    const auto counts = feedsModel_->folderDisabledCounts(feedId);
    feedPropertiesDialog->setFolderDisabledCounts(counts.first, counts.second);
  }
  properties_tmp = properties;

  int result = feedPropertiesDialog->exec();
  if (result == QDialog::Rejected) {
    delete feedPropertiesDialog;
    return;
  }

  if (!isFeed) {
    saveFolderProperties(feedPropertiesDialog, feedId);
    delete feedPropertiesDialog;
    return;
  }

  if (!mainApp->storeDBMemory())
    db_.transaction();

  const auto columns = feedPropertiesDialog->columnSettings();
  properties = feedPropertiesDialog->getFeedProperties();
  delete feedPropertiesDialog;

  index = feedsModel_->indexById(feedId);

  const int preloadMode = properties.display.useGlobalPreload ? -1 : properties.display.preloadPages;
  q.prepare("UPDATE feeds SET text = ?, xmlUrl = ?, displayOnStartup = ?, "
            "displayEmbeddedImages = ?, preloadImages = ?, displayNews = ?, layoutDirection = ?, "
            "label = ?, duplicateNewsMode = ?, addSingleNewsAnyDateOn = ?, avoidedOldSingleNewsDateOn = ?, avoidedOldSingleNewsDate = ?,"
            " authentication = ?, disableUpdate = ?, "
            "javaScriptEnable = ? WHERE id == ?");
  q.addBindValue(properties.general.text);
  q.addBindValue(properties.general.url);
  q.addBindValue(properties.general.displayOnStartup);
  q.addBindValue(properties.display.displayEmbeddedImages);
  q.addBindValue(preloadMode);
  q.addBindValue(properties.display.displayNews);
  q.addBindValue(properties.display.layoutDirection);
  if (properties.general.starred)
    q.addBindValue("starred");
  else
    q.addBindValue("");
  q.addBindValue(properties.general.duplicateNewsMode ? 1 : 0);
  q.addBindValue(properties.general.addSingleNewsAnyDateOn ? 1 : 0);
  q.addBindValue(properties.general.avoidedOldSingleNewsDateOn ? 1 : 0);
  q.addBindValue(properties.general.avoidedOldSingleNewsDate);
  q.addBindValue(properties.authentication.on ? 1 : 0);
  q.addBindValue(properties.general.disableUpdate ? 1 : 0);
  q.addBindValue(properties.display.javaScriptEnable);
  q.addBindValue(feedId);
  q.exec();

  indexColumnsStr = columns.columns;
  properties.column.sortBy = columns.sortBy;
  properties.column.sortType = columns.sortOrder;

  q.prepare("UPDATE feeds SET columns = ?, sort = ?, sortType = ? WHERE id == ?");
  q.addBindValue(indexColumnsStr);
  q.addBindValue(properties.column.sortBy);
  q.addBindValue(properties.column.sortType);
  q.addBindValue(feedId);
  q.exec();

  QModelIndex indexColumns = feedsModel_->indexSibling(index, "columns");
  QModelIndex indexSort = feedsModel_->indexSibling(index, "sort");
  QModelIndex indexSortType = feedsModel_->indexSibling(index, "sortType");
  feedsModel_->setData(indexColumns, indexColumnsStr);
  feedsModel_->setData(indexSort, properties.column.sortBy);
  feedsModel_->setData(indexSortType, properties.column.sortType);

  if (currentNewsTab->feedId_ == feedId)
    currentNewsTab->newsHeader_->setColumns(index);

  if (!(!feedsModel_->dataField(index, "authentication").toInt() && !properties.authentication.on)) {
    q.prepare("SELECT * FROM passwords WHERE server=?");
    q.addBindValue(CredentialScope::feed(QUrl(properties.general.url)));
    q.exec();
    if (q.next()) {
      q.prepare("UPDATE passwords SET username = ?, password = ? WHERE server=?");
      q.addBindValue(properties.authentication.user);
      q.addBindValue(properties.authentication.pass.toUtf8().toBase64());
      q.addBindValue(CredentialScope::feed(QUrl(properties.general.url)));
      q.exec();
    } else if (properties.authentication.on) {
      q.prepare("INSERT INTO passwords (server, username, password) "
                "VALUES (:server, :username, :password)");
      q.bindValue(":server", CredentialScope::feed(QUrl(properties.general.url)));
      q.bindValue(":username", properties.authentication.user);
      q.bindValue(":password", properties.authentication.pass.toUtf8().toBase64());
      q.exec();
    }
  }

  QPersistentModelIndex indexText    = feedsModel_->indexSibling(index, "text");
  QPersistentModelIndex indexUrl     = feedsModel_->indexSibling(index, "xmlUrl");
  QPersistentModelIndex indexStartup = feedsModel_->indexSibling(index, "displayOnStartup");
  QModelIndex indexImages  = feedsModel_->indexSibling(index, "displayEmbeddedImages");
  QModelIndex indexPreload = feedsModel_->indexSibling(index, "preloadImages");
  QModelIndex indexNews    = feedsModel_->indexSibling(index, "displayNews");
  QModelIndex indexRTL     = feedsModel_->indexSibling(index, "layoutDirection");
  QModelIndex indexLabel   = feedsModel_->indexSibling(index, "label");
  QModelIndex indexDuplicate = feedsModel_->indexSibling(index, "duplicateNewsMode");
  QModelIndex indexaddSingleNewsAnyDateOn = feedsModel_->indexSibling(index, "addSingleNewsAnyDateOn");
  QModelIndex indexavoidedOldSingleNewsDateOn = feedsModel_->indexSibling(index, "avoidedOldSingleNewsDateOn");
  QModelIndex indexavoidedOldSingleNewsDate = feedsModel_->indexSibling(index, "avoidedOldSingleNewsDate");
  QModelIndex indexAuthentication = feedsModel_->indexSibling(index, "authentication");
  QModelIndex indexDisableUpdate = feedsModel_->indexSibling(index, "disableUpdate");
  QModelIndex indexJavaScript = feedsModel_->indexSibling(index, "javaScriptEnable");
  feedsModel_->setData(indexText, properties.general.text);
  feedsModel_->setData(indexUrl, properties.general.url);
  feedsModel_->setData(indexStartup, properties.general.displayOnStartup);
  feedsModel_->setData(indexImages, properties.display.displayEmbeddedImages);
  feedsModel_->setData(indexPreload, preloadMode);
  feedsModel_->setData(indexNews, properties.display.displayNews);
  feedsModel_->setData(indexRTL, properties.display.layoutDirection);
  feedsModel_->setData(indexLabel, properties.general.starred ? "starred" : "");
  feedsModel_->setData(indexDuplicate, properties.general.duplicateNewsMode ? 1 : 0);
  feedsModel_->setData(indexaddSingleNewsAnyDateOn, properties.general.addSingleNewsAnyDateOn ? 1 : 0);
  feedsModel_->setData(indexavoidedOldSingleNewsDateOn, properties.general.avoidedOldSingleNewsDateOn ? 1 : 0);
  feedsModel_->setData(indexavoidedOldSingleNewsDate, properties.general.avoidedOldSingleNewsDate);
  feedsModel_->setData(indexAuthentication, properties.authentication.on ? 1 : 0);
  feedsModel_->setData(indexDisableUpdate, properties.general.disableUpdate ? 1 : 0);
  feedsModel_->setData(indexJavaScript, properties.display.javaScriptEnable);

  // Store inheritance explicitly; matching the global interval is still custom.
  const int updateMode = properties.general.useGlobalUpdate ? -1 :
                         (properties.general.updateEnable ? 1 : 0);
  q.prepare("UPDATE feeds SET updateIntervalEnable = ?, updateInterval = ?, "
            "updateIntervalType = ? WHERE id == ?");
  q.addBindValue(updateMode);
  q.addBindValue(properties.general.updateInterval);
  q.addBindValue(properties.general.intervalType);
  q.addBindValue(feedId);
  q.exec();

  QPersistentModelIndex indexUpdateEnable   = feedsModel_->indexSibling(index, "updateIntervalEnable");
  QPersistentModelIndex indexUpdateInterval = feedsModel_->indexSibling(index, "updateInterval");
  QPersistentModelIndex indexIntervalType   = feedsModel_->indexSibling(index, "updateIntervalType");
  feedsModel_->setData(indexUpdateEnable, updateMode);
  feedsModel_->setData(indexUpdateInterval, properties.general.updateInterval);
  feedsModel_->setData(indexIntervalType, properties.general.intervalType);

  if (properties.general.image != properties_tmp.general.image) {
    q.prepare("UPDATE feeds SET image = ? WHERE id == ?");
    q.addBindValue(properties.general.image.toBase64());
    q.addBindValue(feedId);
    q.exec();
    slotIconFeedUpdate(feedId, properties.general.image);
  }

  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = static_cast<NewsTabWidget *>(stackedWidget_->widget(i));
    if (widget->feedId_ == feedId) {
      if (properties.general.text != properties_tmp.general.text) {
        widget->setTextTab(properties.general.text);
      }
    }
  }

  if (!mainApp->storeDBMemory())
    db_.commit();
  const bool preloadChanged =
      properties.display.useGlobalPreload != properties_tmp.display.useGlobalPreload ||
      (!properties.display.useGlobalPreload &&
       properties.display.preloadPages != properties_tmp.display.preloadPages);
  refreshFeedSettings({feedId}, false,
                      properties.display.displayEmbeddedImages != properties_tmp.display.displayEmbeddedImages || preloadChanged,
                      properties.display.layoutDirection != properties_tmp.display.layoutDirection);
  feedsView_->viewport()->update();
  DatabaseBackup::subscriptionsChanged("save feed properties");
}

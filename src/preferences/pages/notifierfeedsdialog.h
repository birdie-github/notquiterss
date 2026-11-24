// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NOTIFIERFEEDSDIALOG_H
#define NOTIFIERFEEDSDIALOG_H

#include "widgets/dialog.h"

class QSqlDatabase;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Dialog for choosing which feeds trigger desktop notifications.
 */
class NotifierFeedsDialog : public Dialog
{
  Q_OBJECT
public:
  explicit NotifierFeedsDialog(QWidget *parent = nullptr);

  void load(QSqlDatabase &db, bool defaultIconFeeds);
  void apply(QSqlDatabase &db);
  bool isLoaded() const { return loadNotifierOk_; }

private slots:
  void itemChanged(QTreeWidgetItem *item, int column);

private:
  static void setCheckStateRecursive(QTreeWidgetItem *item, Qt::CheckState state);

  QTreeWidget *feedsTreeNotify_ = nullptr;
  bool itemNotChecked_ = false;
  bool loadNotifierOk_ = false;
};

#endif // NOTIFIERFEEDSDIALOG_H

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CLEANUPPREVIEWDIALOG_H
#define CLEANUPPREVIEWDIALOG_H

#include <QHash>
#include <QSqlDatabase>
#include <QVector>

#include "dialog.h"

class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class NewsHeader;
class NewsModel;
class NewsView;

struct ShutdownCleanUpConfig
{
  bool cleanupOn = true;
  bool dayCleanUpOn = true;
  int maxDayCleanUp = 30;
  bool newsCleanUpOn = true;
  int maxNewsCleanUp = 200;
  bool readCleanUp = false;
  bool neverUnreadCleanUp = true;
  bool neverStarCleanUp = true;
  bool neverLabelCleanUp = true;
  bool cleanUpDeleted = false;
};

struct ShutdownCleanUpPlan
{
  QHash<int, QVector<int>> feedArticleIds;
  QVector<int> deletedBinIds;
  int totalFeedArticles = 0;

  int totalArticles() const
  {
    return totalFeedArticles + deletedBinIds.size();
  }
};

namespace ShutdownCleanUp {
bool evaluate(QSqlDatabase db, const ShutdownCleanUpConfig &config,
              ShutdownCleanUpPlan &plan, QString &error);
}

class CleanUpPreviewDialog : public Dialog
{
  Q_OBJECT
public:
  CleanUpPreviewDialog(const ShutdownCleanUpPlan &plan, QWidget *parent = nullptr);
  ~CleanUpPreviewDialog() override;

private slots:
  void slotTreeSelectionChanged();

private:
  void populateFeedsTree();
  void applyFilterForItem(QTreeWidgetItem *item);

  ShutdownCleanUpPlan plan_;
  QSplitter *splitter_;
  QTreeWidget *feedsTree_;
  NewsView *newsView_;
  NewsModel *newsModel_;
  NewsHeader *newsHeader_;
};

#endif // CLEANUPPREVIEWDIALOG_H

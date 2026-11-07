// SPDX-License-Identifier: GPL-3.0-or-later
#include "cleanuppreviewdialog.h"

#include "feedsmodel.h"
#include "mainapplication.h"
#include "mainwindow.h"
#include "newsheader.h"
#include "newsmodel.h"
#include "newsretention.h"
#include "newsview.h"
#include "settings.h"

#include <QDateTime>
#include <QHeaderView>
#include <QPainter>
#include <QSet>
#include <QSplitter>
#include <QSqlError>
#include <QSqlQuery>
#include <QTreeWidget>
#include <algorithm>
#include <utility>

namespace {
constexpr int kItemTypeRole = Qt::UserRole;
constexpr int kFeedIdRole = Qt::UserRole + 1;
constexpr int kVirtualDelimiterRole = Qt::UserRole + 2;

enum PreviewItemType {
  FolderItem = 0,
  FeedItem = 1,
  DeletedBinItem = 2
};

struct PreviewCandidate
{
  int id = 0;
  int feedId = 0;
  QString received;
  QString published;
  int read = 0;
  int starred = 0;
  QString label;
};

class PreviewFeedsDelegate : public QStyledItemDelegate
{
public:
  explicit PreviewFeedsDelegate(QObject *parent = nullptr)
    : QStyledItemDelegate(parent)
  {
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override
  {
    QStyleOptionViewItem opt = option;
    opt.state &= ~QStyle::State_HasFocus;
    QStyledItemDelegate::paint(painter, opt, index);

    if (index.data(kVirtualDelimiterRole).toBool()) {
      painter->save();
      painter->setPen(QPen(qApp->palette().color(QPalette::Dark), 1));
      const int y = option.rect.top();
      painter->drawLine(option.rect.left(), y, option.rect.right(), y);
      painter->restore();
    }
  }

  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override
  {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(kVirtualDelimiterRole).toBool()) {
      size.setHeight(size.height() + 4);
    }
    return size;
  }
};
} // namespace

namespace ShutdownCleanUp {
bool evaluate(QSqlDatabase db, const ShutdownCleanUpConfig &config,
              ShutdownCleanUpPlan &plan, QString &error)
{
  plan = ShutdownCleanUpPlan();
  error.clear();

  if (!config.cleanupOn) {
    return true;
  }

  QSqlQuery q(db);
  q.setForwardOnly(true);

  QSet<int> validFeedIds;
  if (!q.exec(QStringLiteral("SELECT id, xmlUrl FROM feeds"))) {
    error = q.lastError().text();
    return false;
  }
  while (q.next()) {
    if (!q.value(1).toString().isEmpty()) {
      validFeedIds.insert(q.value(0).toInt());
    }
  }
  if (q.lastError().isValid()) {
    error = q.lastError().text();
    return false;
  }
  q.finish();

  if (config.cleanUpDeleted) {
    if (!q.exec(QStringLiteral("SELECT id FROM news WHERE deleted == 1 ORDER BY id"))) {
      error = q.lastError().text();
      return false;
    }
    while (q.next()) {
      plan.deletedBinIds.append(q.value(0).toInt());
    }
    if (q.lastError().isValid()) {
      error = q.lastError().text();
      return false;
    }
    q.finish();
  }

  if (validFeedIds.isEmpty()) {
    return true;
  }

  if (!q.exec(QStringLiteral(
          "SELECT id, feedId, received, published, read, starred, label "
          "FROM news WHERE deleted == 0 ORDER BY published"))) {
    error = q.lastError().text();
    return false;
  }

  QHash<int, QVector<PreviewCandidate>> articlesByFeed;
  while (q.next()) {
    const int feedId = q.value(1).toInt();
    if (!validFeedIds.contains(feedId)) continue;
    PreviewCandidate candidate;
    candidate.id = q.value(0).toInt();
    candidate.feedId = feedId;
    candidate.received = q.value(2).toString();
    candidate.published = q.value(3).toString();
    candidate.read = q.value(4).toInt();
    candidate.starred = q.value(5).toInt();
    candidate.label = q.value(6).toString();
    articlesByFeed[feedId].append(candidate);
  }
  if (q.lastError().isValid()) {
    error = q.lastError().text();
    return false;
  }
  q.finish();

  const QDateTime retentionCutoff = config.dayCleanUpOn
      ? NewsRetention::cutoff(config.maxDayCleanUp)
      : QDateTime();
  const QDateTime currentDateTime = QDateTime::currentDateTime();

  auto isEligible = [&](const PreviewCandidate &article) {
    if (config.neverUnreadCleanUp && article.read == 0) return false;
    if (config.neverStarCleanUp && article.starred != 0) return false;
    if (config.neverLabelCleanUp &&
        !article.label.isEmpty() && article.label != QLatin1String(",")) {
      return false;
    }
    return true;
  };

  for (auto it = articlesByFeed.constBegin(); it != articlesByFeed.constEnd(); ++it) {
    const int feedId = it.key();
    const QVector<PreviewCandidate> &articles = it.value();

    QVector<int> removedForFeed;
    QVector<PreviewCandidate> survivingDatedCleanup;
    survivingDatedCleanup.reserve(articles.size());

    // Step 1: Dated articles older than retentionCutoff are hard-deleted first.
    for (const PreviewCandidate &article : articles) {
      if (retentionCutoff.isValid() && isEligible(article) &&
          NewsRetention::expired(article.published, retentionCutoff)) {
        removedForFeed.append(article.id);
      } else {
        survivingDatedCleanup.append(article);
      }
    }

    // Step 2: Count remaining visible (deleted == 0) articles in this feed,
    // then apply maxNewsCleanUp, undated receipt-age cleanup, and readCleanUp
    // in ascending published order.
    const int countAllNews = survivingDatedCleanup.size();
    int countDelNews = 0;

    for (const PreviewCandidate &article : std::as_const(survivingDatedCleanup)) {
      if (!isEligible(article)) continue;

      if (config.newsCleanUpOn && (countDelNews < (countAllNews - config.maxNewsCleanUp))) {
        removedForFeed.append(article.id);
        countDelNews++;
        continue;
      }

      const QDateTime receivedDateTime = QDateTime::fromString(article.received, Qt::ISODate);
      const bool useReceiptAge = !NewsRetention::publicationDate(article.published).isValid();
      if (config.dayCleanUpOn && useReceiptAge &&
          (receivedDateTime.daysTo(currentDateTime) > config.maxDayCleanUp)) {
        removedForFeed.append(article.id);
        countDelNews++;
        continue;
      }

      if (config.readCleanUp) {
        if (article.read != 0) {
          removedForFeed.append(article.id);
        }
        countDelNews++;
      }
    }

    if (!removedForFeed.isEmpty()) {
      plan.totalFeedArticles += removedForFeed.size();
      plan.feedArticleIds.insert(feedId, std::move(removedForFeed));
    }
  }

  return true;
}
} // namespace ShutdownCleanUp

CleanUpPreviewDialog::CleanUpPreviewDialog(const ShutdownCleanUpPlan &plan, QWidget *parent)
  : Dialog(parent)
  , plan_(plan)
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowTitle(tr("Clean Up Preview"));
  setSizeGripEnabled(true);

  MainWindow *mainWindow = mainApp->mainWindow();

  feedsTree_ = new QTreeWidget(this);
  feedsTree_->setObjectName("feedsView_");
  feedsTree_->setFrameStyle(QFrame::NoFrame);
  feedsTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  feedsTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  feedsTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  feedsTree_->setSelectionMode(QAbstractItemView::SingleSelection);
  feedsTree_->setColumnCount(2);
  feedsTree_->header()->setStretchLastSection(false);
  feedsTree_->header()->setVisible(false);
  feedsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  feedsTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  feedsTree_->setItemDelegate(new PreviewFeedsDelegate(feedsTree_));
  feedsTree_->setMinimumWidth(180);

  if (mainWindow && mainWindow->feedsModel_) {
    feedsTree_->setFont(mainWindow->feedsModel_->font_);
    if (!mainWindow->feedsModel_->backgroundColor_.isEmpty()) {
      feedsTree_->setStyleSheet(
            QStringLiteral("#feedsView_ {background: %1;}").arg(mainWindow->feedsModel_->backgroundColor_));
    }
  }

  newsView_ = new NewsView(this);
  newsView_->setFrameStyle(QFrame::NoFrame);
  newsView_->setContextMenuPolicy(Qt::NoContextMenu);
  newsModel_ = new NewsModel(this, newsView_);
  newsModel_->setTable(QStringLiteral("news"));
  newsModel_->setFilter(QStringLiteral("id=-1"));
  newsHeader_ = new NewsHeader(newsModel_, newsView_);

  newsView_->setModel(newsModel_);
  newsView_->setHeader(newsHeader_);
  newsHeader_->init();
  newsHeader_->retranslateStrings();
  newsHeader_->setColumns(QModelIndex());

  if (mainWindow) {
    newsView_->setFont(QFont(mainWindow->newsListFontFamily_, mainWindow->newsListFontSize_));
    newsModel_->formatDate_ = mainWindow->formatDate_;
    newsModel_->formatTime_ = mainWindow->formatTime_;
    newsModel_->simplifiedDateTime_ = mainWindow->simplifiedDateTime_;
    newsModel_->textColor_ = mainWindow->newsListTextColor_;
    newsView_->setStyleSheet(mainWindow->newsListBackgroundColor_.isEmpty()
        ? QString()
        : QStringLiteral("#newsView_ {background: %1;}").arg(mainWindow->newsListBackgroundColor_));
    newsModel_->newNewsTextColor_ = mainWindow->newNewsTextColor_;
    newsModel_->unreadNewsTextColor_ = mainWindow->unreadNewsTextColor_;
    newsModel_->focusedNewsTextColor_ = mainWindow->focusedNewsTextColor_;
    newsModel_->focusedNewsBGColor_ = mainWindow->focusedNewsBGColor_;
    newsView_->setAlternatingRowColors(mainWindow->alternatingRowColorsNews_);
    QPalette newsPalette = qApp->palette();
    newsPalette.setColor(QPalette::AlternateBase, mainWindow->alternatingRowColors_);
    newsView_->setPalette(newsPalette);
  }

  QFrame *leftFrame = new QFrame(this);
  leftFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  QVBoxLayout *leftLayout = new QVBoxLayout(leftFrame);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->addWidget(feedsTree_);

  QFrame *rightFrame = new QFrame(this);
  rightFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightFrame);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->addWidget(newsView_);

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setChildrenCollapsible(false);
  splitter_->addWidget(leftFrame);
  splitter_->addWidget(rightFrame);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setSizes({240, 620});

  pageLayout->addWidget(splitter_, 1);
  buttonBox->addButton(QDialogButtonBox::Close);

  connect(feedsTree_, &QTreeWidget::currentItemChanged,
          this, &CleanUpPreviewDialog::slotTreeSelectionChanged);

  populateFeedsTree();

  resize(880, 520);
  Settings settings;
  restoreGeometry(settings.value("CleanUpPreviewDialog/geometry").toByteArray());
  const QByteArray splitterState = settings.value("CleanUpPreviewDialog/splitter").toByteArray();
  if (!splitterState.isEmpty()) {
    splitter_->restoreState(splitterState);
  }
}

CleanUpPreviewDialog::~CleanUpPreviewDialog()
{
  Settings settings;
  settings.setValue("CleanUpPreviewDialog/geometry", saveGeometry());
  settings.setValue("CleanUpPreviewDialog/splitter", splitter_->saveState());
}

void CleanUpPreviewDialog::populateFeedsTree()
{
  feedsTree_->clear();

  MainWindow *mainWindow = mainApp->mainWindow();
  FeedsModel *feedsModel = mainWindow ? mainWindow->feedsModel_ : nullptr;
  const QColor textColor = (feedsModel && !feedsModel->textColor_.isEmpty())
      ? QColor(feedsModel->textColor_) : QColor();
  const QColor countColor = (feedsModel && !feedsModel->countNewsUnreadColor_.isEmpty())
      ? QColor(feedsModel->countNewsUnreadColor_) : QColor();

  auto styleRow = [&](QTreeWidgetItem *item, bool italicTitle, bool hasTopDelimiter) {
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    if (textColor.isValid()) {
      item->setForeground(0, textColor);
    }
    if (countColor.isValid()) {
      item->setForeground(1, countColor);
    }
    if (italicTitle) {
      QFont font = feedsTree_->font();
      font.setItalic(true);
      item->setFont(0, font);
    }
    if (hasTopDelimiter) {
      item->setData(0, kVirtualDelimiterRole, true);
      item->setData(1, kVirtualDelimiterRole, true);
    }
  };

  if (feedsModel) {
    auto buildSubtree = [&](auto &self, const QModelIndex &parentIndex,
                            QTreeWidgetItem *parentWidgetItem) -> int {
      int totalInFolder = 0;
      const int rows = feedsModel->rowCount(parentIndex);
      for (int row = 0; row < rows; ++row) {
        const QModelIndex idx = feedsModel->index(row, 0, parentIndex);
        if (!idx.isValid()) continue;
        const int id = feedsModel->idByIndex(idx);
        const QString title = feedsModel->dataField(idx, QStringLiteral("text")).toString();
        const bool isFolder = feedsModel->isFolder(idx);

        if (isFolder) {
          auto *folderItem = new QTreeWidgetItem();
          const int folderCount = self(self, idx, folderItem);
          if (folderCount > 0) {
            folderItem->setText(0, title);
            folderItem->setText(1, QStringLiteral("(%1)").arg(folderCount));
            folderItem->setIcon(0, QIcon(QStringLiteral(":/images/folder")));
            folderItem->setData(0, kItemTypeRole, FolderItem);
            folderItem->setData(0, kFeedIdRole, id);
            styleRow(folderItem, false, false);
            if (parentWidgetItem) {
              parentWidgetItem->addChild(folderItem);
            } else {
              feedsTree_->addTopLevelItem(folderItem);
            }
            totalInFolder += folderCount;
          } else {
            delete folderItem;
          }
        } else {
          const int count = plan_.feedArticleIds.value(id).size();
          if (count > 0) {
            auto *feedItem = new QTreeWidgetItem();
            feedItem->setText(0, title);
            feedItem->setText(1, QStringLiteral("(%1)").arg(count));
            QPixmap icon = feedsModel->feedIcon(idx);
            if (icon.isNull() || mainWindow->defaultIconFeeds_) {
              icon.load(QStringLiteral(":/images/feed"));
            }
            feedItem->setIcon(0, icon);
            feedItem->setData(0, kItemTypeRole, FeedItem);
            feedItem->setData(0, kFeedIdRole, id);
            styleRow(feedItem, false, false);
            if (parentWidgetItem) {
              parentWidgetItem->addChild(feedItem);
            } else {
              feedsTree_->addTopLevelItem(feedItem);
            }
            totalInFolder += count;
          }
        }
      }
      return totalInFolder;
    };

    buildSubtree(buildSubtree, QModelIndex(), nullptr);
  }

  if (!plan_.deletedBinIds.isEmpty()) {
    const bool hasFeedItemsAbove = feedsTree_->topLevelItemCount() > 0;
    auto *deletedItem = new QTreeWidgetItem();
    deletedItem->setText(0, tr("Deleted"));
    deletedItem->setText(1, QStringLiteral("(%1)").arg(plan_.deletedBinIds.size()));
    deletedItem->setIcon(0, QIcon(QStringLiteral(":/images/images/trash.png")));
    deletedItem->setData(0, kItemTypeRole, DeletedBinItem);
    deletedItem->setData(0, kFeedIdRole, -1);
    styleRow(deletedItem, true, hasFeedItemsAbove);
    feedsTree_->addTopLevelItem(deletedItem);
  }

  feedsTree_->expandAll();
  if (feedsTree_->topLevelItemCount() > 0) {
    feedsTree_->setCurrentItem(feedsTree_->topLevelItem(0));
  }
}

void CleanUpPreviewDialog::slotTreeSelectionChanged()
{
  applyFilterForItem(feedsTree_->currentItem());
}

void CleanUpPreviewDialog::applyFilterForItem(QTreeWidgetItem *item)
{
  if (!item) {
    newsModel_->setFilter(QStringLiteral("id=-1"));
    newsModel_->select();
    return;
  }

  QVector<int> ids;
  const int itemType = item->data(0, kItemTypeRole).toInt();

  if (itemType == DeletedBinItem) {
    ids = plan_.deletedBinIds;
  } else if (itemType == FeedItem) {
    const int feedId = item->data(0, kFeedIdRole).toInt();
    ids = plan_.feedArticleIds.value(feedId);
  } else if (itemType == FolderItem) {
    auto collectDescendantFeeds = [&](auto &self, QTreeWidgetItem *node) -> void {
      for (int i = 0; i < node->childCount(); ++i) {
        QTreeWidgetItem *child = node->child(i);
        const int childType = child->data(0, kItemTypeRole).toInt();
        if (childType == FeedItem) {
          ids += plan_.feedArticleIds.value(child->data(0, kFeedIdRole).toInt());
        } else if (childType == FolderItem) {
          self(self, child);
        }
      }
    };
    collectDescendantFeeds(collectDescendantFeeds, item);
  }

  if (ids.isEmpty()) {
    newsModel_->setFilter(QStringLiteral("id=-1"));
    newsModel_->select();
    return;
  }

  QStringList idLiterals;
  idLiterals.reserve(ids.size());
  for (int id : std::as_const(ids)) {
    idLiterals.append(QString::number(id));
  }

  const int sortCol = newsHeader_->sortIndicatorSection();
  const Qt::SortOrder sortOrder = newsHeader_->sortIndicatorOrder();
  newsModel_->setFilter(QStringLiteral("id IN (%1)").arg(idLiterals.join(QLatin1Char(','))));
  newsModel_->setSort(sortCol, sortOrder);
  newsModel_->select();
}

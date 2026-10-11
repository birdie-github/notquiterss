#include "feedsproxymodel.h"
#include "feedsmodel.h"

FeedsProxyModel::FeedsProxyModel(QObject *parent)
  : QSortFilterProxyModel(parent)
  , filterAct_("filterFeedsAll_")
{
  setObjectName("FeedsProxyModel");
}

FeedsProxyModel::~FeedsProxyModel()
{

}

void FeedsProxyModel::reset()
{
  QSortFilterProxyModel::beginResetModel();
  QSortFilterProxyModel::endResetModel();
}

void FeedsProxyModel::setFilter(const QString &filterAct, const QList<int> &idList,
                                const QString &findAct, const QString &findText)
{
  if ((filterAct_ != filterAct) || (filterAct != "filterFeedsAll_") ||
      (findAct_ != findAct) || (findText_ != findText) || (idList_ != idList)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    filterAct_ = filterAct;
    findAct_ = findAct;
    findText_ = findText;
    idList_ = idList;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
  }
}

bool FeedsProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
  bool accept = false;
  QModelIndex index;

  if (filterAct_ == "filterFeedsAll_") {
    accept = true;
  } else if (filterAct_ == "filterFeedsNew_") {
    index = sourceModel()->index(sourceRow, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("newCount"), sourceParent);
    if (sourceModel()->data(index, Qt::EditRole).toInt() > 0)
      accept = true;
  } else if (filterAct_ == "filterFeedsUnread_") {
    index = sourceModel()->index(sourceRow, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("unread"), sourceParent);
    if (sourceModel()->data(index, Qt::EditRole).toInt() > 0)
      accept = true;
  }

  if (idList_.count()) {
    index = sourceModel()->index(sourceRow, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("id"), sourceParent);
    if (idList_.contains(sourceModel()->data(index, Qt::EditRole).toInt()))
      accept = true;
  }

  if (accept && !findText_.isEmpty()) {
    index = sourceModel()->index(sourceRow, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("xmlUrl"), sourceParent);
    if (!sourceModel()->data(index, Qt::EditRole).toString().isEmpty()) {
      if (findAct_ == "findLinkAct") {
        accept = sourceModel()->data(index, Qt::EditRole).toString().contains(findText_, Qt::CaseInsensitive);
      } else {
        index = sourceModel()->index(sourceRow, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("text"), sourceParent);
        accept = sourceModel()->data(index, Qt::EditRole).toString().contains(findText_, Qt::CaseInsensitive);
      }
    } else {
      accept = acceptFolder(index);
    }
  }

  return accept;
}

QModelIndex FeedsProxyModel::mapFromSource(const QModelIndex & sourceIndex) const
{
  return QSortFilterProxyModel::mapFromSource(sourceIndex);
}

QModelIndex FeedsProxyModel::mapFromSource(int id) const
{
  return QSortFilterProxyModel::mapFromSource(static_cast<FeedsModel *>(sourceModel())->indexById(id));
}

QModelIndex FeedsProxyModel::index(int row, int column, const QModelIndex & parent) const
{
  return QSortFilterProxyModel::index(row, column, parent);
}

QModelIndex FeedsProxyModel::index(int row, const QString& fieldName, const QModelIndex & parent) const
{
  int column = static_cast<FeedsModel *>(sourceModel())->indexColumnOf(fieldName);
  return QSortFilterProxyModel::index(row, column, parent);
}

bool FeedsProxyModel::acceptFolder(const QModelIndex &indexFolder) const
{
  QModelIndex index;
  bool accept = false;

  for (int i = 0; i < sourceModel()->rowCount(indexFolder); ++i) {
    index = sourceModel()->index(i, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("xmlUrl"), indexFolder);
    if (!sourceModel()->data(index, Qt::EditRole).toString().isEmpty()) {
      index = sourceModel()->index(i, static_cast<FeedsModel *>(sourceModel())->indexColumnOf("text"), indexFolder);
      accept = sourceModel()->data(index, Qt::EditRole).toString().contains(findText_, Qt::CaseInsensitive);
    } else {
      accept = acceptFolder(index);
    }
    if (accept)
      break;
  }

  return accept;
}

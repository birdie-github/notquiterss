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
    accept = acceptRowSearch(sourceRow, sourceParent);
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

bool FeedsProxyModel::acceptRowSearch(int sourceRow, const QModelIndex &sourceParent) const
{
  const auto *model = static_cast<const FeedsModel *>(sourceModel());
  const QModelIndex urlIdx = model->index(sourceRow, model->indexColumnOf("xmlUrl"), sourceParent);
  const QString xmlUrl = model->data(urlIdx, Qt::EditRole).toString();
  if (!xmlUrl.isEmpty()) {
    if (findAct_ == QLatin1String("findLinkAct")) {
      return xmlUrl.contains(findText_, Qt::CaseInsensitive);
    }
    const QModelIndex textIdx = model->index(sourceRow, model->indexColumnOf("text"), sourceParent);
    return model->data(textIdx, Qt::EditRole).toString().contains(findText_, Qt::CaseInsensitive);
  }
  return acceptFolder(model->index(sourceRow, 0, sourceParent));
}

bool FeedsProxyModel::acceptFolder(const QModelIndex &indexFolder) const
{
  for (int i = 0; i < sourceModel()->rowCount(indexFolder); ++i) {
    if (acceptRowSearch(i, indexFolder))
      return true;
  }
  return false;
}

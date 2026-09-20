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
#include <QTimeZone>
#include "feedsmodel.h"
#include "feedhealth.h"
#include "feedsproxymodel.h"

#include <QtCore>
#include <QPainter>
#include <QSet>

FeedsModel::FeedsModel(QObject *parent)
  : QAbstractItemModel(parent)
  , defaultIconFeeds_(false)
  , view_(0)
  , rootParentId_(0)
{
  setObjectName("FeedsModel");

  refresh();
}

FeedsModel::~FeedsModel()
{
  clear();
}

void FeedsModel::clear()
{
  disabledCounts_.clear();
  disabledCountsDirty_ = true;
  id2RowList_.clear();
  parid2RowList_.clear();
  columnsList_.clear();

  qDeleteAll(userDataList_);
  userDataList_.clear();
}

void FeedsModel::refresh()
{
  beginResetModel();
  clear();
  endResetModel();

  queryModel_.setQuery("SELECT * FROM feeds ORDER BY parentId, rowToParent");
  while (queryModel_.canFetchMore())
    queryModel_.fetchMore();

  indexId_ = queryModel_.record().indexOf("id");
  indexParid_ = queryModel_.record().indexOf("parentId");
  for (int i = 0; i < queryModel_.record().count(); i++) {
    columnsList_[i] = i;
  }
  columnsList_[0] = queryModel_.record().indexOf("text");
  columnsList_[queryModel_.record().indexOf("text")] = 0;

  for (int i = 0; i < queryModel_.rowCount(); i++) {
    int id = queryModel_.record(i).value(indexId_).toInt();
    id2RowList_[id] = i;
    int parid = queryModel_.record(i).value(indexParid_).toInt();
    parid2RowList_[i] = parid;
    userDataList_[id] = new UserData(id, parid, queryModel_.record(i));
  }
}

UserData * FeedsModel::userDataById(int id) const
{
  return userDataList_.value(id, 0);
}

int FeedsModel::rowById(int id) const
{
  return id2RowList_.value(id, -1);
}

int FeedsModel::rowByParid(int parid) const
{
  return parid2RowList_.key(parid, -1);
}

int FeedsModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return parid2RowList_.keys(idByIndex(parent)).count();
  else
    return parid2RowList_.keys(rootParentId_).count();
}

int FeedsModel::columnCount(const QModelIndex&) const
{
  return queryModel_.record().count();
}

QModelIndex FeedsModel::index(int row, int column, const QModelIndex &parent) const
{
  if (row == -1)
    return QModelIndex();

  int id = 0;
  if (parent.isValid())
    id = id2RowList_.key(row + rowByParid(idByIndex(parent)), 0);
  else
    id = id2RowList_.key(row, 0);

  UserData *userData = userDataById(id);
  if (userData)
    return createIndex(row, column, userData);
  else
    return QModelIndex();
}

QModelIndex FeedsModel::parent(const QModelIndex &index) const
{
  if (!index.isValid())
    return QModelIndex();

  int parid = paridByIndex(index);
  if (parid == rootParentId_)
    return QModelIndex();

  UserData *userData = userDataById(parid);
  if (userData) {
    int row = rowById(parid) - rowByParid(userData->parid);
    return createIndex(row, 0, userData);
  } else {
    return QModelIndex();
  }
}

QVariant FeedsModel::data(const QModelIndex &index, int role) const
{
  if (role == Qt::FontRole) {
    QFont font = font_;
    if (indexColumnOf("text") == index.column()) {
      if (0 < indexSibling(index, "unread").data(Qt::EditRole).toInt())
        font.setBold(true);
    }
    return font;
  } else if (role == Qt::DisplayRole){
    if (indexColumnOf("unread") == index.column()) {
      int unread = indexSibling(index, "unread").data(Qt::EditRole).toInt();
      if (0 == unread) {
        return QVariant();
      } else {
        QString qStr = QString("(%1)").arg(unread);
        return qStr;
      }
    } else if (indexColumnOf("undeleteCount") == index.column()) {
      QString qStr = QString("(%1)").
          arg(indexSibling(index, "undeleteCount").data(Qt::EditRole).toInt());
      return qStr;
    } else if (indexColumnOf("updated") == index.column()) {
      QDateTime dtLocal;
      QString strDate = indexSibling(index, "updated").data(Qt::EditRole).toString();

      if (!strDate.isNull()) {
        QDateTime dtLocalTime = QDateTime::currentDateTime();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), QTimeZone(QTimeZone::UTC));
#else
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
#endif
        int nTimeShift = dtLocalTime.secsTo(dtUTC);

        QDateTime dt = QDateTime::fromString(strDate, Qt::ISODate);
        dtLocal = dt.addSecs(nTimeShift);

        QString strResult;
        if (QDateTime::currentDateTime().date() <= dtLocal.date())
          strResult = dtLocal.toString(formatTime_);
        else
          strResult = dtLocal.toString(formatDate_);
        return strResult;
      } else {
        return QVariant();
      }
    }
  } else if (role == Qt::ForegroundRole) {
    if (indexColumnOf("unread") == index.column()) {
      return QColor(countNewsUnreadColor_);
    }

    QModelIndex currentIndex = ((FeedsProxyModel*)view_->model())->mapToSource(view_->currentIndex());
    if ((index.row() == currentIndex.row()) && (index.parent() == currentIndex.parent()) &&
        view_->selectionModel()->selectedRows(0).count()) {
      return QColor(focusedFeedTextColor_);
    }

    if (indexColumnOf("text") == index.column()) {
      const auto counts = isFolder(index) ? folderDisabledCounts(idByIndex(index)) : QPair<int, int>();
      const bool disabled = isFolder(index) ? counts.first > 0 && counts.first == counts.second :
          indexSibling(index, "disableUpdate").data(Qt::EditRole).toBool();
      if (disabled) return QColor(feedDisabledUpdateColor_);
    }

    if (indexColumnOf("text") == index.column()) {
      if (indexSibling(index, "newCount").data(Qt::EditRole).toInt() > 0) {
        return QColor(feedWithNewNewsColor_);
      }
    }


    return QColor(textColor_);
  } else if (role == Qt::BackgroundRole) {
    QModelIndex currentIndex = ((FeedsProxyModel*)view_->model())->mapToSource(view_->currentIndex());
    if ((index.row() == currentIndex.row()) && (index.parent() == currentIndex.parent()) &&
        view_->selectionModel()->selectedRows(0).count()) {
      if (!focusedFeedBGColor_.isEmpty())
        return QColor(focusedFeedBGColor_);
    }
  } else if (role == Qt::DecorationRole) {
    if (indexColumnOf("text") == index.column()) {
      if (isFolder(index)) {
        return QPixmap(":/images/folder");
      } else {
        if (!defaultIconFeeds_) {
          QByteArray byteArray = indexSibling(index, "image").data(Qt::EditRole).toByteArray();
          if (!byteArray.isNull()) {
            QImage resultImage;
            if (resultImage.loadFromData(QByteArray::fromBase64(byteArray))) {
              QString strStatus = indexSibling(index, "status").data(Qt::EditRole).toString();
              if (strStatus.section(" ", 0, 0).toInt() != 0) {
                QImage image;
                if (strStatus.section(" ", 0, 0).toInt() == 1)
                  image.load(":/images/bulletUpdate");
                QPainter resultPainter(&resultImage);
                resultPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                resultPainter.drawImage(0, 0, image);
                resultPainter.end();
              }
              return resultImage;
            }
          }
        }
        QImage resultImage(":/images/feed");
        QString strStatus = indexSibling(index, "status").data(Qt::EditRole).toString();
        if (strStatus.section(" ", 0, 0).toInt() != 0) {
          QImage image;
          if (strStatus.section(" ", 0, 0).toInt() == 1)
            image.load(":/images/bulletUpdate");

          QPainter resultPainter(&resultImage);
          resultPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
          resultPainter.drawImage(0, 0, image);
          resultPainter.end();
        }
        return resultImage;
      }
    }
  } else if (role == Qt::TextAlignmentRole) {
    if (indexColumnOf("id") == index.column()) {
      int flag = Qt::AlignRight|Qt::AlignVCenter;
      return flag;
    }
  } else if (role == FeedHealth::WarningRole) {
    return index.column() == indexColumnOf("text") && !isFolder(index) &&
        !indexSibling(index, "disableUpdate").data(Qt::EditRole).toBool() &&
        FeedHealth::read(indexSibling(index, "status").data(Qt::EditRole).toString()).warning;
  } else if (role == Qt::ToolTipRole) {
    if (indexColumnOf("text") == index.column()) {
      QString title = index.data(Qt::EditRole).toString();
      if (isFolder(index)) {
        const auto counts = folderDisabledCounts(idByIndex(index));
        if (counts.first > 0 && counts.first == counts.second)
          return tr("%1\nAll %2 feeds in this folder are disabled.").arg(title).arg(counts.first);
        return tr("%1\n%2 of %3 feeds disabled.").arg(title).arg(counts.second).arg(counts.first);
      }
      const QString failure = FeedHealth::tooltip(title,
          indexSibling(index, "status").data(Qt::EditRole).toString(),
          indexSibling(index, "updated").data(Qt::EditRole).toDateTime());
      if (!failure.isEmpty() && !isFolder(index) &&
          !indexSibling(index, "disableUpdate").data(Qt::EditRole).toBool()) return failure;
      QRect rectText = view_->visualRect(index);
      int width = rectText.width() - 16 - 12;
      QFont font = font_;
      if (0 < indexSibling(index, "unread").data(Qt::EditRole).toInt())
        font.setBold(true);
      QFontMetrics fontMetrics(font);

      if (width < fontMetrics.horizontalAdvance(title))
        return title;
    }
    return QString("");
  }

  if (!((role == Qt::EditRole) || (role == Qt::DisplayRole)))
    return QVariant();

  QSqlRecord record = static_cast<UserData*>(index.internalPointer())->record;
  return record.value(indexColumnOf(index.column()));
}

bool FeedsModel::setData(const QModelIndex &index, const QVariant &value, int)
{
  if (!index.isValid())
    return false;

  QSqlRecord *record = &static_cast<UserData*>(index.internalPointer())->record;
  const int field = indexColumnOf(index.column());
  const QString name = record->fieldName(field);
  if (name == "disableUpdate" || name == "xmlUrl" || name == "parentId")
    disabledCountsDirty_ = true;
  record->setValue(field, value);
  return true;
}

Qt::ItemFlags FeedsModel::flags(const QModelIndex &index) const
{
  Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);

  if (index.isValid())
    return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | defaultFlags;
  else
    return Qt::ItemIsDropEnabled | defaultFlags;
}

Qt::DropActions FeedsModel::supportedDropActions() const
{
  return Qt::MoveAction;
}

QModelIndex FeedsModel::indexById(int id) const
{
  QModelIndex parentIndex = QModelIndex();
  UserData *userData = userDataById(id);
  if (userData)
    parentIndex = indexById(userData->parid);
  for (int i = 0; i < rowCount(parentIndex); i++) {
    if (idByIndex(index(i, 0, parentIndex)) == id)
      return index(i,0,parentIndex);
  }
  return QModelIndex();
}

int FeedsModel::idByIndex(const QModelIndex &index) const
{
  if (index.isValid())
    return static_cast<UserData*>(index.internalPointer())->id;
  return 0;
}

int FeedsModel::paridByIndex(const QModelIndex &index) const
{
  if (index.isValid())
    return static_cast<UserData*>(index.internalPointer())->parid;
  return 0;
}

int FeedsModel::indexColumnOf(int column) const
{
  return columnsList_.value(column, column);
}

int FeedsModel::indexColumnOf(const QString &name) const
{
  return indexColumnOf(queryModel_.record().indexOf(name));
}

void FeedsModel::setView(QTreeView *view)
{
  view_ = view;
}

QVariant FeedsModel::dataField(const QModelIndex &index, const QString &fieldName) const
{
  return indexSibling(index, fieldName).data(Qt::EditRole);
}

/** @brief Check if item is folder
 *
 *  If xmlUrl field is empty, than item is considered folder
 * @param index Item to check
 * @return Is folder sign
 * @retval true Index item is category
 * @retval false Index item is feed
 *---------------------------------------------------------------------------*/
bool FeedsModel::isFolder(const QModelIndex &index) const
{
  return indexSibling(index, "xmlUrl").data(Qt::EditRole).toString().isEmpty();
}

QModelIndex FeedsModel::indexSibling(const QModelIndex &index, const QString &fieldName) const
{
  return this->index(index.row(), indexColumnOf(fieldName), index.parent());
}

QPair<int, int> FeedsModel::folderDisabledCounts(int folderId) const
{
  if (disabledCountsDirty_) {
    disabledCounts_.clear();
    for (const UserData *feed : userDataList_) {
      if (feed->record.value("xmlUrl").toString().isEmpty()) continue;
      const bool disabled = feed->record.value("disableUpdate").toBool();
      int parent = feed->record.value("parentId").toInt();
      QSet<int> seen;
      while (parent != 0 && !seen.contains(parent)) {
        seen.insert(parent);
        const UserData *folder = userDataList_.value(parent, nullptr);
        if (!folder) break;
        auto &counts = disabledCounts_[parent];
        ++counts.first;
        if (disabled) ++counts.second;
        parent = folder->record.value("parentId").toInt();
      }
    }
    disabledCountsDirty_ = false;
  }
  return disabledCounts_.value(folderId);
}

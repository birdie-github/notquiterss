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
  , indexId_(-1)
  , indexParid_(-1)
  , indexImage_(-1)
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
  childrenByParent_.clear();
  columnsList_.clear();
  columnIndexByName_.clear();

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

  const QSqlRecord baseRecord = queryModel_.record();
  indexId_ = baseRecord.indexOf("id");
  indexParid_ = baseRecord.indexOf("parentId");
  indexImage_ = baseRecord.indexOf("image");
  for (int i = 0; i < baseRecord.count(); i++) {
    columnsList_[i] = i;
  }
  const int textIdx = baseRecord.indexOf("text");
  columnsList_[0] = textIdx;
  columnsList_[textIdx] = 0;
  for (int i = 0; i < baseRecord.count(); i++) {
    columnIndexByName_[baseRecord.fieldName(i)] = columnsList_.value(i, i);
  }

  const int rowTotal = queryModel_.rowCount();
  userDataList_.reserve(rowTotal);
  for (int i = 0; i < rowTotal; i++) {
    const QSqlRecord rowRecord = queryModel_.record(i);
    int id = rowRecord.value(indexId_).toInt();
    int parid = rowRecord.value(indexParid_).toInt();
    QVector<UserData*> &siblings = childrenByParent_[parid];
    UserData *userData = new UserData(id, parid, siblings.size(), rowRecord);
    siblings.append(userData);
    userDataList_[id] = userData;
  }
}

UserData * FeedsModel::userDataById(int id) const
{
  return userDataList_.value(id, nullptr);
}

int FeedsModel::rowCount(const QModelIndex &parent) const
{
  const int parentId = parent.isValid() ? idByIndex(parent) : rootParentId_;
  const auto it = childrenByParent_.constFind(parentId);
  return it != childrenByParent_.constEnd() ? it.value().count() : 0;
}

int FeedsModel::columnCount(const QModelIndex&) const
{
  return columnsList_.count();
}

QModelIndex FeedsModel::index(int row, int column, const QModelIndex &parent) const
{
  if (row < 0 || column < 0)
    return QModelIndex();

  const int parentId = parent.isValid() ? idByIndex(parent) : rootParentId_;
  const auto it = childrenByParent_.constFind(parentId);
  if (it == childrenByParent_.constEnd() || row >= it.value().count())
    return QModelIndex();

  UserData *userData = it.value().at(row);
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
    return createIndex(userData->rowInParent, 0, userData);
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

    QModelIndex currentIndex = static_cast<FeedsProxyModel *>(view_->model())->mapToSource(view_->currentIndex());
    if ((index.row() == currentIndex.row()) && (index.parent() == currentIndex.parent()) &&
        view_->selectionModel()->selectedRows(0).count()) {
      return QColor(focusedFeedTextColor_);
    }

    if (indexColumnOf("text") == index.column()) {
      if (updatesDisabled(index)) return QColor(feedDisabledUpdateColor_);
    }

    if (indexColumnOf("text") == index.column()) {
      if (indexSibling(index, "newCount").data(Qt::EditRole).toInt() > 0) {
        return QColor(feedWithNewNewsColor_);
      }
    }


    return QColor(textColor_);
  } else if (role == Qt::BackgroundRole) {
    QModelIndex currentIndex = static_cast<FeedsProxyModel *>(view_->model())->mapToSource(view_->currentIndex());
    if ((index.row() == currentIndex.row()) && (index.parent() == currentIndex.parent()) &&
        view_->selectionModel()->selectedRows(0).count()) {
      if (!focusedFeedBGColor_.isEmpty())
        return QColor(focusedFeedBGColor_);
    }
  } else if (role == Qt::DecorationRole) {
    if (indexColumnOf("text") == index.column()) {
      if (isFolder(index)) {
        static const QPixmap folderPixmap(QStringLiteral(":/images/folder"));
        return folderPixmap;
      } else {
        const UserData *userData = static_cast<const UserData*>(index.internalPointer());
        const QString strStatus = indexSibling(index, "status").data(Qt::EditRole).toString();
        const int statusCode = strStatus.section(QLatin1Char(' '), 0, 0).toInt();

        if (!defaultIconFeeds_) {
          ensureIconDecoded(userData);
          if (!userData->customIconPixmap.isNull()) {
            if (statusCode == 0)
              return userData->customIconPixmap;

            QImage resultImage = userData->customIconImage;
            QImage image;
            if (statusCode == 1)
              image.load(":/images/bulletUpdate");
            QPainter resultPainter(&resultImage);
            resultPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            resultPainter.drawImage(0, 0, image);
            resultPainter.end();
            return resultImage;
          }
        }
        if (statusCode == 0) {
          static const QPixmap feedPixmap(QStringLiteral(":/images/feed"));
          return feedPixmap;
        }
        QImage resultImage(":/images/feed");
        QImage image;
        if (statusCode == 1)
          image.load(":/images/bulletUpdate");

        QPainter resultPainter(&resultImage);
        resultPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        resultPainter.drawImage(0, 0, image);
        resultPainter.end();
        return resultImage;
      }
    }
  } else if (role == Qt::TextAlignmentRole) {
    if (indexColumnOf("id") == index.column()) {
      int flag = Qt::AlignRight|Qt::AlignVCenter;
      return flag;
    }
  } else if (role == FeedHealth::DisabledRole) {
    return index.column() == indexColumnOf("text") && updatesDisabled(index);
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

  const QSqlRecord &record = static_cast<const UserData*>(index.internalPointer())->record;
  return record.value(indexColumnOf(index.column()));
}

bool FeedsModel::setData(const QModelIndex &index, const QVariant &value, int)
{
  if (!index.isValid())
    return false;

  UserData *userData = static_cast<UserData*>(index.internalPointer());
  QSqlRecord *record = &userData->record;
  const int field = indexColumnOf(index.column());
  const QString name = record->fieldName(field);
  if (name == "disableUpdate" || name == "xmlUrl" || name == "parentId")
    disabledCountsDirty_ = true;
  if (name == "image" || name == "xmlUrl") {
    userData->iconDecoded = false;
    userData->customIconImage = QImage();
    userData->customIconPixmap = QPixmap();
  }
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
  if (id <= 0)
    return QModelIndex();
  UserData *userData = userDataById(id);
  if (!userData)
    return QModelIndex();
  if (userData->parid != rootParentId_ && !userDataById(userData->parid))
    return QModelIndex();
  return createIndex(userData->rowInParent, 0, userData);
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
  const auto it = columnIndexByName_.constFind(name);
  if (it != columnIndexByName_.constEnd())
    return it.value();
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

void FeedsModel::ensureIconDecoded(const UserData *userData) const
{
  if (!userData || userData->iconDecoded)
    return;
  userData->iconDecoded = true;
  if (indexImage_ < 0)
    return;
  const QByteArray byteArray = userData->record.value(indexImage_).toByteArray();
  if (!byteArray.isNull()) {
    if (userData->customIconImage.loadFromData(QByteArray::fromBase64(byteArray))) {
      userData->customIconPixmap = QPixmap::fromImage(userData->customIconImage);
    }
  }
}

QPixmap FeedsModel::feedIcon(const QModelIndex &index) const
{
  if (!index.isValid())
    return QPixmap();
  if (isFolder(index)) {
    static const QPixmap folderPixmap(QStringLiteral(":/images/folder"));
    return folderPixmap;
  }
  const UserData *userData = static_cast<const UserData*>(index.internalPointer());
  ensureIconDecoded(userData);
  if (!userData->customIconPixmap.isNull())
    return userData->customIconPixmap;
  static const QPixmap feedPixmap(QStringLiteral(":/images/feed"));
  return feedPixmap;
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

bool FeedsModel::isFeedInFolder(int feedId, int folderId) const
{
  if (feedId <= 0 || folderId <= 0)
    return false;
  QSet<int> seen;
  const UserData *node = userDataById(feedId);
  while (node && node->parid != 0 && !seen.contains(node->parid)) {
    if (node->parid == folderId)
      return true;
    seen.insert(node->parid);
    node = userDataById(node->parid);
  }
  return false;
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

bool FeedsModel::updatesDisabled(const QModelIndex &index) const
{
  if (!isFolder(index))
    return indexSibling(index, "disableUpdate").data(Qt::EditRole).toBool();
  const auto counts = folderDisabledCounts(idByIndex(index));
  return counts.first > 0 && counts.first == counts.second;
}

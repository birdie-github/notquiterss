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
#ifndef FEEDSVIEW_H
#define FEEDSVIEW_H

#include <QtWidgets>
#include <QTimer>
#include <feedsmodel.h>

class FeedsView : public QTreeView
{
  Q_OBJECT
public:
  explicit FeedsView(QWidget * parent = 0);
  int selectId_;
  bool selectIdEn_;
  bool autocollapseFolder_;

  void setSourceModel(FeedsModel *sourceModel);
  void refresh();
  void setColumnHidden(const QString& column, bool hide);
  int columnIndex(const QString& fieldName) const;
  bool isFolder(const QModelIndex &index) const;
  QModelIndex indexNextUnread(const QModelIndex &indexCur, int nextCondition = 0);
  QModelIndex firstFeedInFolder(const QModelIndex &indexFolder);
  QModelIndex lastFeedInFolder(const QModelIndex &indexFolder);
  QModelIndex indexPrevious(const QModelIndex &indexCur, bool isParent = false);
  QModelIndex indexNext(const QModelIndex &indexCur, bool isParent = false);
  QModelIndex lastFolderInFolder(const QModelIndex &indexFolder);
  QModelIndex indexPreviousFolder(const QModelIndex &indexCur);
  QModelIndex firstFolderInFolder(const QModelIndex &indexFolder);
  QModelIndex indexNextFolder(const QModelIndex &indexCur, bool isParent = false);

public slots:
  void navigateUp();
  void navigateDown();
  void navigatePrevious();
  void navigateNext();
  void navigateHome();
  void navigateEnd();
  void navigatePageUp();
  void navigatePageDown();

  void restoreExpanded();
  void expandAll();
  void collapseAll();
  QPersistentModelIndex selectIndex();
  void updateCurrentIndex(const QModelIndex &index);

signals:
  // Emitted after a navigation command updates the selection.
  void navigationActivated(const QModelIndex &index);
  void signalDoubleClicked();
  void signalMiddleClicked();
  void pressKeyUp();
  void pressKeyDown();
  void pressKeyHome();
  void pressKeyEnd();
  void pressKeyPageUp();
  void pressKeyPageDown();
  void signalDropped(const QModelIndex &where, int how);

protected:
  virtual void mousePressEvent(QMouseEvent*);
  virtual void mouseReleaseEvent(QMouseEvent*event);
  virtual void mouseMoveEvent(QMouseEvent*);
  virtual void mouseDoubleClickEvent(QMouseEvent*);
  virtual void keyPressEvent(QKeyEvent*);
  virtual void currentChanged(const QModelIndex &current,
                              const QModelIndex &previous);
  void dragEnterEvent(QDragEnterEvent *event);
  void dragLeaveEvent(QDragLeaveEvent *event);
  void dragMoveEvent(QDragMoveEvent *event);
  void dropEvent(QDropEvent *event);
  void paintEvent(QPaintEvent *event);

private slots:
  void slotExpanded(const QModelIndex&index);
  void slotCollapsed(const QModelIndex&index);
  void dragAutoScroll();

private:
  FeedsModel *sourceModel_;
  QPoint dragPos_;
  QPoint dragStartPos_;
  QTimer dragAutoScrollTimer_;
  QAbstractItemView::ScrollMode dragScrollMode_;
  QList<int> expandedList;
  int expandedOldId_;
  QModelIndex indexClicked_;

  void handleDrop(QDropEvent *e);
  bool shouldAutoScroll(const QPoint &pos) const;

};

#endif // FEEDSVIEW_H

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDSTATUSDELEGATE_H
#define FEEDSTATUSDELEGATE_H
#include <QStyledItemDelegate>
#include <QElapsedTimer>
#include <QRegion>
class QSvgRenderer;
class QTimer;
class QTreeView;
class FeedStatusDelegate : public QStyledItemDelegate
{
public:
  explicit FeedStatusDelegate(QTreeView *view);
  void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
private:
  QTreeView *view_;
  QTimer *frame_;
  QSvgRenderer *disabledMarker_;
  QElapsedTimer clock_;
  mutable QRegion pendingFrames_;
};
#endif

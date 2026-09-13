// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedstatusdelegate.h"
#include "feedhealth.h"
#include <QPainter>
#include <QTimer>
#include <QTreeView>
#include <QApplication>
#include <cmath>
FeedStatusDelegate::FeedStatusDelegate(QTreeView *view)
  : QStyledItemDelegate(view), view_(view), frame_(new QTimer(this))
{
  clock_.start();
  frame_->setSingleShot(true);
  frame_->setInterval(40);
  connect(frame_, &QTimer::timeout, this, [this] {
    const QRegion dirty = pendingFrames_;
    pendingFrames_ = QRegion();
    if (view_->isVisible()) view_->viewport()->update(dirty);
  });
}
void FeedStatusDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
  QStyleOptionViewItem opt(option);
  initStyleOption(&opt, index);
  opt.state &= ~QStyle::State_HasFocus;
  const bool warning = index.data(FeedHealth::WarningRole).toBool();
  QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
  const int size = qMin(opt.rect.height()-2, qMax(16, opt.fontMetrics.height()));
  if (warning) {
    const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    opt.text = opt.fontMetrics.elidedText(opt.text, opt.textElideMode, qMax(0, text.width()-size-6));
  }
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
  if (!warning || size <= 0) return;
  painter->save();
  painter->setClipRect(opt.rect);
  painter->setRenderHint(QPainter::Antialiasing);
  const bool rtl = opt.direction == Qt::RightToLeft;
  painter->translate(rtl ? opt.rect.left()+size/2.0+2 : opt.rect.right()-size/2.0-2,
                     opt.rect.center().y());
  // A flat sign rotating about its vertical axis: one revolution in 4 seconds.
  const double width = std::cos((clock_.elapsed()%4000) * 6.283185307179586 / 8000.0);
  if (std::abs(width) > 0.015) {
    painter->scale(width * size/20.0, size/20.0);
    painter->setPen(QPen(QColor("#563d00"), 1));
    painter->setBrush(QColor("#ffcc33"));
    QPolygonF triangle;
    triangle << QPointF(0,-9) << QPointF(9,8) << QPointF(-9,8);
    painter->drawPolygon(triangle);
    painter->setPen(QPen(QColor("#241a00"), 2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(0,-3), QPointF(0,2));
    painter->drawPoint(QPointF(0,5));
  }
  painter->restore();
  // Painting a visible warning arms exactly one frame. With no visible warnings
  // (including collapsed folders or a hidden window), the animation goes idle.
  if (view_->isVisible()) {
    pendingFrames_ += opt.rect;
    if (!frame_->isActive()) frame_->start();
  }
}

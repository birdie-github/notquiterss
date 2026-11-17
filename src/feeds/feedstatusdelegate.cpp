// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedstatusdelegate.h"
#include "feedhealth.h"
#include <QPainter>
#include <QImage>
#include <QSvgRenderer>
#include <QTimer>
#include <QTreeView>
#include <QApplication>
#include <cmath>
FeedStatusDelegate::FeedStatusDelegate(QTreeView *view)
  : QStyledItemDelegate(view), view_(view), frame_(new QTimer(this)),
    disabledMarker_(new QSvgRenderer(QStringLiteral(":/images/disabledFeed"), this))
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
  if (index.data(FeedHealth::DisabledRole).toBool() && !opt.icon.isNull()) {
    const QRect icon = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, opt.widget);
    // Logical coordinates: the painter supplies the screen's device-pixel ratio.
    // Eight logical pixels for a 16-pixel icon; keep the physical bottom-left in RTL too.
    const qreal side = qMin(icon.width(), icon.height()) * 0.5;
    if (side > 0) {
      painter->save();
      painter->setClipRect(opt.rect, Qt::IntersectClip);
      painter->setRenderHint(QPainter::Antialiasing);
      // Ask the style to paint just this row's background at the icon centre.
      // This honours QSS selection/hover colours and model background brushes,
      // which need not match QPalette::Base or QPalette::Highlight.
      QStyleOptionViewItem background(opt);
      background.icon = QIcon();
      background.text.clear();
      background.features &= ~(QStyleOptionViewItem::HasDecoration |
                               QStyleOptionViewItem::HasDisplay |
                               QStyleOptionViewItem::HasCheckIndicator);
      QImage sample(1, 1, QImage::Format_ARGB32_Premultiplied);
      sample.fill(opt.palette.color(QPalette::Base));
      {
        QPainter samplePainter(&sample);
        samplePainter.translate(-icon.center());
        style->drawControl(QStyle::CE_ItemViewItem, &background, &samplePainter, opt.widget);
      }
      const QRectF marker(icon.left(), icon.bottom() + 1 - side, side, side);
      // Centre lines and stroke widths follow disabled-feed.svg's 100x100
      // geometry. Add one logical pixel on each side of its red strokes.
      painter->translate(marker.topLeft());
      painter->setPen(QPen(sample.pixelColor(0, 0), side * 0.18052 + 2.0,
                           Qt::SolidLine, Qt::RoundCap));
      painter->drawLine(QPointF(side * 0.063896, side * 0.064196),
                        QPointF(side * 0.935804, side * 0.936104));
      painter->drawLine(QPointF(side * 0.063894, side * 0.936106),
                        QPointF(side * 0.938302, side * 0.064194));
      disabledMarker_->render(painter, QRectF(0, 0, side, side));
      painter->restore();
    }
  }
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

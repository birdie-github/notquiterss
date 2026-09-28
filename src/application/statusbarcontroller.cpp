#include "statusbarcontroller.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QStringList>
#include <QPalette>
#include <QProgressBar>
#include <QStatusBar>
#include <QToolButton>

// Paint the fill and elided label consistently across platform styles.
// Palette roles keep the fill and both text colors consistent with the theme.
class FeedProgressBar final : public QProgressBar
{
public:
  explicit FeedProgressBar(QWidget *parent) : QProgressBar(parent) {}

  void setLabel(const QString &label)
  {
    label_ = label;
    setAccessibleName(label);
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    const QRect area = rect().adjusted(1, 1, -1, -1);
    painter.fillRect(area, palette().brush(QPalette::Base));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    const int span = maximum() - minimum();
    const int width = span > 0
        ? int(qint64(area.width()) * qBound(0, value() - minimum(), span) / span) : 0;
    QRect fill = area;
    fill.setWidth(width);
    if (layoutDirection() == Qt::RightToLeft) fill.moveRight(area.right());
    painter.fillRect(fill, palette().brush(QPalette::Highlight));
    const QString label = fontMetrics().elidedText(label_, Qt::ElideRight,
                                                  qMax(0, area.width() - 12));
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(area, Qt::AlignCenter, label);
    painter.setClipRect(fill);
    painter.setPen(palette().color(QPalette::HighlightedText));
    painter.drawText(area, Qt::AlignCenter, label);
  }

private:
  QString label_;
};

StatusBarController::StatusBarController(QStatusBar *statusBar,
                                         QAction *stopUpdateAction,
                                         QAction *loadImagesAction,
                                         QAction *fullScreenAction,
                                         QObject *parent)
  : QObject(parent)
  , progressWidget_(new QWidget(statusBar))
  , statusBar_(statusBar)
  , progressBar_(new FeedProgressBar(statusBar))
  , statusUnread_(new QLabel(statusBar))
  , statusAll_(new QLabel(statusBar))
{
#if defined(HAVE_X11) || defined(Q_OS_MAC)
  statusBar_->setStyleSheet(QString("QStatusBar::item {border-right: 1px solid %1;"
                                    "margin: 1px;}").
                            arg(qApp->palette().color(QPalette::Dark).name()));
#endif

  progressBar_->setObjectName("progressBar_");
  progressBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  progressBar_->setMinimumWidth(qMax(1, (statusBar_->width() + 3) / 4));
  progressBar_->setRange(0, 1);
  progressBar_->setValue(0);
  statusBar_->installEventFilter(this);
  progressWidget_->setMaximumWidth(qMax(1, statusBar_->width() / 2));
  QHBoxLayout *progressLayout = new QHBoxLayout(progressWidget_);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(2);
  progressLayout->addWidget(progressBar_, 1);
  progressWidget_->hide();

  QToolButton *stopUpdateButton = new QToolButton(progressWidget_);
  stopUpdateButton->setFocusPolicy(Qt::NoFocus);
  stopUpdateButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  stopUpdateButton->setIconSize(QSize(16, 16));
  stopUpdateButton->setCursor(Qt::ArrowCursor);
  stopUpdateButton->setDefaultAction(stopUpdateAction);
  stopUpdateButton->setStyleSheet(
        "QToolButton { border: none; padding: 0px; background: none; }"
        "QToolButton:hover { background: rgba(150, 150, 150, 60) }");
  progressLayout->addWidget(stopUpdateButton);

  QToolButton *loadImagesButton = new QToolButton(statusBar_);
  loadImagesButton->setFocusPolicy(Qt::NoFocus);
  loadImagesButton->setIconSize(QSize(16, 16));
  loadImagesButton->setDefaultAction(loadImagesAction);
  loadImagesButton->setStyleSheet(
        "QToolButton { border: none; padding: 0px; background: none; }");

  QToolButton *fullScreenButton = new QToolButton(statusBar_);
  fullScreenButton->setFocusPolicy(Qt::NoFocus);
  fullScreenButton->setIconSize(QSize(16, 16));
  fullScreenButton->setDefaultAction(fullScreenAction);
  fullScreenButton->setStyleSheet(
        "QToolButton { border: none; padding: 0px; background: none; }");

  statusBar_->addPermanentWidget(progressWidget_, 1);
  statusUnread_->hide();
  statusBar_->addPermanentWidget(statusUnread_);
  statusAll_->hide();
  statusBar_->addPermanentWidget(statusAll_);
  statusBar_->addPermanentWidget(loadImagesButton);
  statusBar_->addPermanentWidget(fullScreenButton);
  statusBar_->show();
}

bool StatusBarController::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == statusBar_ && event->type() == QEvent::Resize) {
    progressBar_->setMinimumWidth(qMax(1, (statusBar_->width() + 3) / 4));
    // Leave room for status messages as well as the permanent controls.
    progressWidget_->setMaximumWidth(qMax(1, statusBar_->width() / 2));
  }
  return QObject::eventFilter(watched, event);
}

void StatusBarController::queueFeed(int feedId, const QString &name)
{
  if (pendingFeeds_.contains(feedId)) return;
  if (pendingFeeds_.isEmpty()) {
    totalFeeds_ = 0;
    completedFeeds_ = 0;
    activeFeeds_.clear();
  }
  pendingFeeds_.insert(feedId, {name.isEmpty() ? tr("Feed %1").arg(feedId) : name,
                               tr("Queued…")});
  ++totalFeeds_;
  refreshProgress();
  progressWidget_->show();
}

void StatusBarController::setFeedStage(int feedId, const QString &stage)
{
  auto feed = pendingFeeds_.find(feedId);
  if (feed == pendingFeeds_.end()) return;
  feed->stage = stage;
  // Retain the original start order across redirects and retries.
  if (!activeFeeds_.contains(feedId)) activeFeeds_.append(feedId);
  refreshProgress();
}

void StatusBarController::finishFeed(int feedId)
{
  if (!pendingFeeds_.remove(feedId)) return;
  activeFeeds_.removeAll(feedId);
  ++completedFeeds_;
  refreshProgress();
  if (pendingFeeds_.isEmpty()) progressWidget_->hide();
}

void StatusBarController::refreshProgress()
{
  progressBar_->setRange(0, totalFeeds_);
  progressBar_->setValue(completedFeeds_);
  const QString counts = QString("%1/%2").arg(completedFeeds_).arg(totalFeeds_);
  QString label = counts;
  QStringList details;
  if (!activeFeeds_.isEmpty()) {
    const auto &feed = pendingFeeds_[activeFeeds_.first()];
    label = tr("%1 — %2: %3").arg(counts, feed.name, feed.stage);
    if (activeFeeds_.size() > 1)
      label += tr(" (+%1 other feeds)").arg(activeFeeds_.size() - 1);
    for (int id : activeFeeds_) {
      const auto &active = pendingFeeds_[id];
      details.append(tr("%1: %2").arg(active.name, active.stage).toHtmlEscaped());
    }
  } else if (!pendingFeeds_.isEmpty()) {
    label = tr("%1 — Waiting to start…").arg(counts);
  }
  const int queued = pendingFeeds_.size() - activeFeeds_.size();
  if (queued > 0) details.append(tr("%1 feeds queued").arg(queued).toHtmlEscaped());
  progressBar_->setLabel(label);
  // Escape names explicitly so a feed title cannot become tooltip markup.
  progressBar_->setToolTip("<qt>" + details.join("<br/>") + "</qt>");
}

void StatusBarController::hideProgress()
{
  // Stop hides the display immediately; cancellation completions still drain
  // tracking. Stage/completion events cannot show the widget again.
  progressWidget_->hide();
}

void StatusBarController::showMessage(const QString &message, int timeout)
{
  statusBar_->showMessage(message, timeout);
}

void StatusBarController::setCountTexts(const QString &unreadText, const QString &allText)
{
  statusUnread_->setText(unreadText);
  statusAll_->setText(allText);
}

QString StatusBarController::unreadText() const
{
  return statusUnread_->text();
}

QString StatusBarController::allText() const
{
  return statusAll_->text();
}

void StatusBarController::setCountsVisible(bool unreadVisible, bool allVisible)
{
  statusUnread_->setVisible(unreadVisible);
  statusAll_->setVisible(allVisible);
}

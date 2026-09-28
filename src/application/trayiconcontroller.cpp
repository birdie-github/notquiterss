#include <QGuiApplication>
#include "trayiconcontroller.h"

#include <QAction>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

TrayIconController::TrayIconController(QWidget *menuParent, QAction *showWindowAction,
                                       QAction *addFeedAction, QAction *updateAllFeedsAction,
                                       QAction *markAllFeedsReadAction, QAction *optionsAction,
                                       QAction *exitAction, QObject *parent)
  : QObject(parent)
  , trayIcon_(new QSystemTrayIcon(QIcon(":/images/application256"), this))
  , trayMenu_(new QMenu(menuParent))
{
  trayIcon_->setToolTip(QGuiApplication::applicationDisplayName());

  trayMenu_->addAction(showWindowAction);
  trayMenu_->addAction(addFeedAction);
  trayMenu_->addAction(updateAllFeedsAction);
  trayMenu_->addAction(markAllFeedsReadAction);
  trayMenu_->addSeparator();
  trayMenu_->addAction(optionsAction);
  trayMenu_->addSeparator();
  trayMenu_->addAction(exitAction);
  trayIcon_->setContextMenu(trayMenu_);
}

QSystemTrayIcon *TrayIconController::systemTrayIcon() const
{
  return trayIcon_;
}

void TrayIconController::activateMenu()
{
  trayMenu_->activateWindow();
}

QString TrayIconController::toolTip() const
{
  return trayIcon_->toolTip();
}

void TrayIconController::setToolTip(const QString &text)
{
  trayIcon_->setToolTip(text);
}

void TrayIconController::show()
{
  trayIcon_->show();
}

void TrayIconController::hide()
{
  trayIcon_->hide();
}

void TrayIconController::showMessage(const QString &title, const QString &message)
{
  trayIcon_->showMessage(title, message);
}

void TrayIconController::showDefaultIcon()
{
  trayIcon_->setIcon(QIcon(":/images/application256"));
}

void TrayIconController::showNewNewsIcon()
{
  trayIcon_->setIcon(QIcon(":/images/applicationNewNews"));
}

void TrayIconController::showCountIcon(int count)
{
  if (count == 0) {
    showDefaultIcon();
    return;
  }

  QString countText;
  QFont font("Consolas");
  if (count > 99) {
    font.setBold(false);
    if (count < 1000) {
      font.setPixelSize(60);
      countText = QString::number(count);
    } else {
      font.setPixelSize(86);
      countText = "#";
    }
  } else {
    font.setBold(true);
    font.setPixelSize(90);
    countText = QString::number(count);
  }

  QPixmap icon(128, 128);
  icon.fill(Qt::transparent);
  QPainter painter(&icon);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const QRect rectangle(0, 0, 128, 128);
  QLinearGradient gradient(rectangle.bottomLeft(), rectangle.topLeft());
  const QColor color("#117C04");
  gradient.setColorAt(0, color.lighter());
  gradient.setColorAt(0.5, color);
  gradient.setColorAt(1, color.lighter());
  painter.setBrush(gradient);
  painter.drawRoundedRect(rectangle, 20, 20);
  painter.setFont(font);
  painter.setPen("#FFFFFF");
  painter.drawText(rectangle, Qt::AlignVCenter | Qt::AlignHCenter, countText);
  trayIcon_->setIcon(icon);
}

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
#include "splashscreen.h"

SplashScreen::SplashScreen(Qt::WindowFlags flag)
  : QSplashScreen(QPixmap(), flag)
{
  QPixmap pixmap(420, 140);
  pixmap.fill(palette().color(QPalette::Window));
  QPainter painter(&pixmap);
  painter.drawPixmap(16, 24, QPixmap(":/images/application256").scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  QFont titleFont = font();
  titleFont.setPixelSize(28);
  titleFont.setBold(true);
  painter.setFont(titleFont);
  painter.setPen(palette().color(QPalette::WindowText));
  painter.drawText(QRect(112, 24, 292, 80), Qt::AlignVCenter, QGuiApplication::applicationDisplayName());
  painter.end();
  setPixmap(pixmap);
  setFixedSize(pixmap.size());
  setContentsMargins(5, 0, 5, 0);
  setEnabled(false);
  showMessage("Prepare loading...   " %
              QString("%1").arg(QCoreApplication::applicationVersion()),
              Qt::AlignRight | Qt::AlignTop, Qt::darkGray);
  setAttribute(Qt::WA_DeleteOnClose);

  QFont font = this->font();
  font.setPixelSize(12);
  setFont(font);

  splashProgress_.setObjectName("splashProgress");
  splashProgress_.setTextVisible(false);
  splashProgress_.setFixedHeight(10);
  splashProgress_.setMaximum(100);
  splashProgress_.setValue(10);

  QVBoxLayout *layout = new QVBoxLayout();
  layout->addStretch(1);
  layout->addWidget(&splashProgress_);
  setLayout(layout);
}

void SplashScreen::setProgress(int value)
{
  qApp->processEvents();
  splashProgress_.setValue(value);
  showMessage("Loading: " % QString::number(value) % "%   " %
              QString("%1").arg(QCoreApplication::applicationVersion()),
              Qt::AlignRight | Qt::AlignTop, Qt::darkGray);
}

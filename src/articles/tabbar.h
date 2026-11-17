/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
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
#ifndef TABBAR_H
#define TABBAR_H

#include <QtWidgets>

class TabBar : public QTabBar
{
  Q_OBJECT
public:
  explicit TabBar(QWidget *parent = 0);

  enum CloseTabsState {
    CloseTabIdle = 0,
    CloseTabCurrentIndex,
    CloseTabOtherIndex
  };

  CloseTabsState closingTabState_;

public slots:
  void slotCloseTab();
  void slotCloseOtherTabs();
  void slotCloseAllTab();
  void slotNextTab();
  void slotPrevTab();

signals:
  void closeTab(int index);

protected:
  bool eventFilter(QObject *obj, QEvent *ev);

private slots:
  void showContextMenuTabBar(const QPoint &pos);

private:
  int indexClickedTab_;
  bool tabFixed_;

};

#endif // TABBAR_H

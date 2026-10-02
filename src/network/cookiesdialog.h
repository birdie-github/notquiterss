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
#ifndef COOKIESDIALOG_H
#define COOKIESDIALOG_H

#include "dialog.h"

class CookiesDialog : public Dialog
{
  Q_OBJECT
public:
  explicit CookiesDialog(QWidget *parent);
private:
  void refresh();
  void importCookies();
  void removeSite();
  QListWidget *sites_;
  QPushButton *remove_;
};
#endif

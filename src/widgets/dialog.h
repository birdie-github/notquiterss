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
#ifndef DIALOG_H
#define DIALOG_H

#include <QtWidgets>

class Dialog : public QDialog
{
  Q_OBJECT
public:
  explicit Dialog(QWidget *parent, Qt::WindowFlags flag = Qt::WindowFlags());
  void bindGeometrySetting(const QString &key);

  static QPixmap pickIconPixmap(QWidget *parent, const QString &imageFilesText,
                                const QString &dialogTitle, const QString &errorText);
  static QByteArray prepareFaviconIcoData(const QPixmap &pixmap);
  static QByteArray prepareFaviconIcoData(const QByteArray &byteArray,
                                          const QString &format = QString());
  static QIcon colorSwatchIcon(const QColor &color);
  static QColor pickColor(QWidget *parent, const QColor &initial = QColor());

  QVBoxLayout *mainLayout;
  QVBoxLayout *pageLayout;
  QHBoxLayout *buttonsLayout;

  QDialogButtonBox *buttonBox;

};

#endif // DIALOG_H

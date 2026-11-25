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
#include "dialog.h"
#include "preferences/settings.h"

#include <QBuffer>
#include <QImageReader>

Dialog::Dialog(QWidget *parent, Qt::WindowFlags flag)
  : QDialog(parent, flag)
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  pageLayout = new QVBoxLayout();
  pageLayout->setContentsMargins(10, 10, 10, 5);
  pageLayout->setSpacing(5);

  buttonBox = new QDialogButtonBox();

  buttonsLayout = new QHBoxLayout();
  buttonsLayout->setContentsMargins(10, 10, 10, 10);
  buttonsLayout->addWidget(buttonBox);

  QFrame *line = new QFrame();
  line->setFrameStyle(QFrame::HLine | QFrame::Sunken);

  mainLayout = new QVBoxLayout();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  mainLayout->addLayout(pageLayout, 1);
  mainLayout->addWidget(line);
  mainLayout->addLayout(buttonsLayout);
  setLayout(mainLayout);

  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void Dialog::bindGeometrySetting(const QString &key)
{
  Settings settings;
  const QByteArray saved = settings.value(key).toByteArray();
  if (!saved.isEmpty()) restoreGeometry(saved);
  connect(this, &QDialog::finished, this, [this, key] {
    Settings out;
    out.setValue(key, saveGeometry());
  });
}

QPixmap Dialog::pickIconPixmap(QWidget *parent, const QString &imageFilesText,
                               const QString &dialogTitle, const QString &errorText)
{
  QString filter;
  for (const QByteArray &imageFormat : QImageReader::supportedImageFormats()) {
    if (!filter.isEmpty()) filter.append(QLatin1Char(' '));
    filter.append(QLatin1String("*.")).append(QString::fromLatin1(imageFormat));
  }
  filter = imageFilesText + QStringLiteral(" (%1)").arg(filter);

  const QString fileName = QFileDialog::getOpenFileName(
      parent, dialogTitle, QDir::homePath(), filter);
  if (fileName.isNull()) return QPixmap();

  QFile file(fileName);
  QPixmap pixmap;
  if (!file.open(QIODevice::ReadOnly) || !pixmap.loadFromData(file.readAll())) {
    QMessageBox::warning(parent, QString(), errorText);
    return QPixmap();
  }

  return pixmap.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QByteArray Dialog::prepareFaviconIcoData(const QPixmap &pixmap)
{
  if (pixmap.isNull()) return QByteArray();

  const QPixmap icon = (pixmap.size() == QSize(16, 16))
      ? pixmap
      : pixmap.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QByteArray faviconData;
  QBuffer buffer(&faviconData);
  buffer.open(QIODevice::WriteOnly);
  if (!icon.save(&buffer, "ICO"))
    return QByteArray();
  // Qt's ICO writer leaves the 2-byte row-alignment padding in each 16-pixel
  // 1-bit AND-mask scanline (offset 1086 + y * 4 + 2..3) uninitialized.
  if (faviconData.size() == 1150) {
    for (int y = 0; y < 16; ++y) {
      faviconData[1086 + y * 4 + 2] = 0;
      faviconData[1086 + y * 4 + 3] = 0;
    }
  }
  return faviconData;
}

QByteArray Dialog::prepareFaviconIcoData(const QByteArray &byteArray, const QString &format)
{
  QPixmap icon;
  if (!icon.loadFromData(byteArray) &&
      (format.isEmpty() || !icon.loadFromData(byteArray, format.toUtf8().constData()))) {
    return QByteArray();
  }
  return prepareFaviconIcoData(icon);
}

QIcon Dialog::colorSwatchIcon(const QColor &color)
{
  QPixmap pixmap(14, 14);
  pixmap.fill(color.isValid() ? color : QColor(0, 0, 0, 0));
  return QIcon(pixmap);
}

QColor Dialog::pickColor(QWidget *parent, const QColor &initial)
{
  // Pass initial to the QColorDialog constructor rather than getColor(), whose
  // default constructor seeds native color dialogs with Qt::white.
  QColorDialog colorDialog(initial.isValid() ? initial : QColor(Qt::black), parent);
  return colorDialog.exec() == QDialog::Accepted ? colorDialog.selectedColor() : QColor();
}

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
#include "labeldialog.h"
#include "mainwindow/mainwindow.h"

LabelDialog::LabelDialog(QWidget *parent)
  : Dialog(parent, Qt::MSWindowsFixedSizeDialogHint)
{
  setWindowTitle(tr("New Label"));

  nameEdit_ = new LineEdit(this);

  QMenu *iconMenu = new QMenu(this);
  for (int i = 0; i < 6; i++) {
    iconMenu->addAction(QIcon(QString(":/images/label_%1").arg(i+1)),
                        MainWindow::trNameLabels().at(i));
  }
  iconMenu->addSeparator();
  QAction *newIcon = new QAction(tr("Load icon..."), this);
  iconMenu->addAction(newIcon);

  iconButton_ = new QToolButton(this);
  iconButton_->setObjectName("ToolButton");
  iconButton_->setIconSize(QSize(16, 16));
  iconButton_->setPopupMode(QToolButton::MenuButtonPopup);
  iconButton_->setMenu(iconMenu);

  auto createColorButton = [this](QString &colorStr) {
    auto *menu = new QMenu(this);
    QAction *defaultAct = menu->addAction(tr("Default"));
    menu->addSeparator();
    QAction *selectAct = menu->addAction(tr("Select color..."));

    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("ToolButton"));
    button->setIconSize(QSize(16, 16));
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setMenu(menu);

    connect(button, &QAbstractButton::clicked, button, &QToolButton::showMenu);
    connect(defaultAct, &QAction::triggered, this, [&colorStr, button]() {
      colorStr.clear();
      button->setIcon(colorSwatchIcon(QColor()));
    });
    connect(selectAct, &QAction::triggered, this, [this, &colorStr, button]() {
      const QColor color = pickColor(this, QColor(colorStr));
      if (!color.isValid()) return;
      colorStr = color.name();
      button->setIcon(colorSwatchIcon(color));
    });
    return button;
  };

  colorTextButton_ = createColorButton(colorTextStr_);
  colorBgButton_ = createColorButton(colorBgStr_);

  QHBoxLayout *layoutH1 = new QHBoxLayout();
  layoutH1->addWidget(new QLabel(tr("Name:")));
  layoutH1->addWidget(nameEdit_, 1);

  QHBoxLayout *layoutH2 = new QHBoxLayout();
  layoutH2->addWidget(new QLabel(tr("Icon:")));
  layoutH2->addWidget(iconButton_);
  layoutH2->addSpacing(10);
  layoutH2->addWidget(new QLabel(tr("Color text:")));
  layoutH2->addWidget(colorTextButton_);
  layoutH2->addSpacing(10);
  layoutH2->addWidget(new QLabel(tr("Color background:")));
  layoutH2->addWidget(colorBgButton_);
  layoutH2->addStretch(1);

  pageLayout->addLayout(layoutH1);
  pageLayout->addLayout(layoutH2);

  buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

  connect(nameEdit_, &QLineEdit::textChanged, this, &LabelDialog::nameEditChanged);
  connect(iconButton_, &QAbstractButton::clicked, iconButton_, &QToolButton::showMenu);
  connect(iconMenu, &QMenu::triggered, this, &LabelDialog::selectIcon);
  connect(newIcon, &QAction::triggered, this, &LabelDialog::loadIcon);
}

void LabelDialog::loadData()
{
  iconButton_->setIcon(icon_);
  colorTextButton_->setIcon(colorSwatchIcon(QColor(colorTextStr_)));
  colorBgButton_->setIcon(colorSwatchIcon(QColor(colorBgStr_)));
}

void LabelDialog::nameEditChanged(const QString& text)
{
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
}

void LabelDialog::selectIcon(QAction *action)
{
  icon_ = action->icon();
  iconButton_->setIcon(action->icon());
}

void LabelDialog::loadIcon()
{
  const QPixmap pixmap = pickIconPixmap(this, tr("Image files"), tr("Select Image"),
                                        tr("Could not open the icon file."));
  if (pixmap.isNull()) return;

  icon_ = QIcon(pixmap);
  iconButton_->setIcon(icon_);
}

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
#include "itemcondition.h"

ItemCondition::ItemCondition(QWidget * parent)
  : QWidget(parent)
{
  comboBox1_ = new QComboBox(this);
  comboBox2_ = new QComboBox(this);
  comboBox3_ = new QComboBox(this);
  lineEdit_ = new LineEdit(this);

  QStringList itemList;
  itemList << tr("Title")  << tr("Description")
           << tr("Author") << tr("Category") << tr("State")
           << tr("Link") << tr("News")
              /*<< tr("Published") << tr("Received")*/;
  comboBox1_->addItems(itemList);

  itemList.clear();
  itemList << tr("New") << tr("Read") << tr("Starred");
  comboBox3_->addItems(itemList);

  currentIndexChanged(comboBox1_->currentIndex());

  addButton_ = new ToolButton(this);
  addButton_->setIcon(QIcon(":/images/addT"));
  addButton_->setToolTip(tr("Add Condition"));
  addButton_->setAutoRaise(true);
  deleteButton_ = new ToolButton(this);
  deleteButton_->setIcon(QIcon(":/images/deleteT"));
  deleteButton_->setToolTip(tr("Delete Condition"));
  deleteButton_->setAutoRaise(true);

  QHBoxLayout *buttonsLayout = new QHBoxLayout();
  buttonsLayout->setAlignment(Qt::AlignCenter);
  buttonsLayout->setContentsMargins(0, 0, 0, 0);
  buttonsLayout->setSpacing(5);
  buttonsLayout->addWidget(comboBox1_);
  buttonsLayout->addWidget(comboBox2_);
  buttonsLayout->addWidget(comboBox3_, 1);
  buttonsLayout->addWidget(lineEdit_, 1);
  buttonsLayout->addWidget(addButton_);
  buttonsLayout->addWidget(deleteButton_);

  setLayout(buttonsLayout);

  connect(deleteButton_, &QAbstractButton::clicked, this, &ItemCondition::deleteFilterRules);
  connect(comboBox1_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ItemCondition::currentIndexChanged);
}

void ItemCondition::deleteFilterRules()
{
  emit signalDeleteCondition(this);
}

void ItemCondition::currentIndexChanged(int index)
{
  const bool isState = (index == 4);
  comboBox2_->clear();
  comboBox3_->setVisible(isState);
  lineEdit_->setVisible(!isState);

  switch (index) {
  case 0: // Title
  case 3: // Category
  case 5: // Link
    comboBox2_->addItems({tr("contains"), tr("doesn't contain"),
                          tr("is"), tr("isn't"),
                          tr("begins with"), tr("ends with"),
                          tr("Regular expressions")});
    break;
  case 1: // Description
  case 6: // News
    comboBox2_->addItems({tr("contains"), tr("doesn't contain"),
                          tr("Regular expressions")});
    break;
  case 2: // Author
    comboBox2_->addItems({tr("contains"), tr("doesn't contain"),
                          tr("is"), tr("isn't"),
                          tr("Regular expressions")});
    break;
  case 4: // State
    comboBox2_->addItems({tr("is"), tr("isn't")});
    break;
  default:
    break;
  }
}

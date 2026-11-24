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
#include "addfolderdialog.h"
#include "feeds/feedselectiontree.h"

#include <QDebug>

AddFolderDialog::AddFolderDialog(QWidget *parent, int curFolderId)
  : Dialog(parent, Qt::MSWindowsFixedSizeDialogHint)
{
  setWindowTitle(tr("Add Folder"));
  setMinimumWidth(400);
  setMinimumHeight(300);

  nameFeedEdit_ = new LineEdit(this);

  foldersTree_ = new QTreeWidget(this);
  FeedSelectionTree::configure(foldersTree_, QStringLiteral("foldersTree_"));

  QString treeError;
  if (!FeedSelectionTree::populateFolders(foldersTree_, QSqlDatabase::database(),
                                          tr("All Feeds"), curFolderId, treeError)) {
    qWarning().noquote() << treeError;
  }

  pageLayout->addWidget(new QLabel(tr("Name:")));
  pageLayout->addWidget(nameFeedEdit_);
  pageLayout->addWidget(new QLabel(tr("Location:")));
  pageLayout->addWidget(foldersTree_);

  buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

  connect(nameFeedEdit_, &QLineEdit::textChanged, this, &AddFolderDialog::nameFeedEditChanged);
}

void AddFolderDialog::nameFeedEditChanged(const QString& text)
{
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
}

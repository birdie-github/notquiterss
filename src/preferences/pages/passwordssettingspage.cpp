// SPDX-License-Identifier: GPL-3.0-or-later
#include "passwordssettingspage.h"

#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"

#include <QByteArray>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVBoxLayout>

PasswordsSettingsPage::PasswordsSettingsPage(QWidget *parent,
                                             QVector<OptionSearchEntry> *searchEntries,
                                             int pageIndex)
  : QWidget(parent)
{
  passTree_ = new QTreeWidget(this);
  passTree_->setObjectName(QStringLiteral("labelsTree_"));
  passTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  passTree_->setColumnCount(4);
  passTree_->setColumnHidden(0, true);
  passTree_->setColumnHidden(3, true);
  passTree_->setColumnWidth(1, 250);
  passTree_->header()->setMinimumSectionSize(22);
  passTree_->setHeaderLabels({QStringLiteral("Id"), OptionsDialog::tr("Site"), OptionsDialog::tr("User"), OptionsDialog::tr("Password")});

  auto *deletePass = new QPushButton(OptionsDialog::tr("Delete"), this);
  connect(deletePass, &QAbstractButton::clicked, this, &PasswordsSettingsPage::deleteSelected);
  auto *deleteAllPass = new QPushButton(OptionsDialog::tr("Delete All"), this);
  connect(deleteAllPass, &QAbstractButton::clicked, this, &PasswordsSettingsPage::deleteAll);
  auto *showPass = new QPushButton(OptionsDialog::tr("Show Passwords"), this);
  connect(showPass, &QAbstractButton::clicked, this, &PasswordsSettingsPage::showPasswords);

  auto *passButtonLayout = new QVBoxLayout();
  passButtonLayout->addWidget(deletePass);
  passButtonLayout->addWidget(deleteAllPass);
  passButtonLayout->addSpacing(10);
  passButtonLayout->addWidget(showPass);
  passButtonLayout->addStretch(1);

  auto *passLayout = new QHBoxLayout(this);
  passLayout->setContentsMargins(0, 0, 0, 0);
  passLayout->addWidget(passTree_);
  passLayout->addLayout(passButtonLayout);

  if (searchEntries && pageIndex >= 0) {
    searchEntries->append({
        pageIndex, -1, OptionsDialog::tr("Passwords"), QString(),
        OptionsDialog::tr("Site") + QLatin1Char(' ') + OptionsDialog::tr("User") + QLatin1Char(' ') + OptionsDialog::tr("Show Passwords")
            + QStringLiteral(" credentials login auth"),
        passTree_});
  }
}

void PasswordsSettingsPage::load()
{
  if (loadPasswordsOk_) return;
  loadPasswordsOk_ = true;

  QSqlQuery q;
  q.exec(QStringLiteral("SELECT id, server, username, password FROM passwords"));
  while (q.next()) {
    const QString id = q.value(0).toString();
    const QString server = q.value(1).toString();
    const QString user = q.value(2).toString();
    const QString pass = QString::fromUtf8(QByteArray::fromBase64(q.value(3).toByteArray()));
    passTree_->addTopLevelItem(new QTreeWidgetItem(QStringList{id, server, user, pass}));
  }
}

void PasswordsSettingsPage::apply(QSqlDatabase &db)
{
  if (!loadPasswordsOk_) return;
  db.transaction();
  QSqlQuery q(db);
  for (int i = 0; i < passTree_->topLevelItemCount(); ++i) {
    if (passTree_->topLevelItem(i)->isHidden()) {
      const QString id = passTree_->topLevelItem(i)->text(0);
      q.exec(QStringLiteral("DELETE FROM passwords WHERE id=='%1'").arg(id));
    }
  }
  db.commit();
}

void PasswordsSettingsPage::deleteSelected()
{
  if (QTreeWidgetItem *item = passTree_->currentItem()) {
    item->setHidden(true);
  }
}

void PasswordsSettingsPage::deleteAll()
{
  for (int i = 0; i < passTree_->topLevelItemCount(); ++i) {
    passTree_->topLevelItem(i)->setHidden(true);
  }
}

void PasswordsSettingsPage::showPasswords()
{
  if (passTree_->isColumnHidden(3)) {
    passTree_->showColumn(3);
    passTree_->setColumnWidth(1, passTree_->columnWidth(1) - passTree_->columnWidth(3));
  }
}

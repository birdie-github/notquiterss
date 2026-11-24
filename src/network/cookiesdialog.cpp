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
#include "cookiesdialog.h"
#include "cookiejar.h"
#include "app/mainapplication.h"

namespace {
QListWidget *populateSiteListDialog(Dialog &dialog, const QString &title,
                                    const QSize &size, const QString &descriptionText,
                                    QDialogButtonBox::StandardButtons buttons)
{
  dialog.setWindowTitle(title);
  dialog.resize(size);
  auto *description = new QLabel(descriptionText, &dialog);
  description->setWordWrap(true);
  dialog.pageLayout->addWidget(description);
  auto *list = new QListWidget(&dialog);
  dialog.pageLayout->addWidget(list);
  dialog.buttonBox->setStandardButtons(buttons);
  if (buttons.testFlag(QDialogButtonBox::Ok)) {
    QObject::connect(dialog.buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  }
  return list;
}
} // namespace

CookiesDialog::CookiesDialog(QWidget *parent) : Dialog(parent)
{
  sites_ = populateSiteListDialog(*this, tr("Website cookies"), QSize(560, 380), tr(
      "Cookies are disabled until you import them for a website. Enabled websites "
      "may also set cookies. A leading dot includes subdomains.\n\n"
      "Persistent cookies are saved every six hours and on exit when changed. "
      "Session cookies last only until exit. Imports and removals are saved immediately."),
      QDialogButtonBox::Close);
  auto *import = new QPushButton(tr("Import cookies…"), this);
  remove_ = new QPushButton(tr("Remove cookies and disable"), this);
  auto *actions = new QHBoxLayout;
  actions->addWidget(import);
  actions->addWidget(remove_);
  pageLayout->addLayout(actions);
  connect(import, &QPushButton::clicked, this, &CookiesDialog::importCookies);
  connect(remove_, &QPushButton::clicked, this, &CookiesDialog::removeSite);
  connect(sites_, &QListWidget::currentRowChanged, this, [this](int row) {
    remove_->setEnabled(row >= 0);
  });
  refresh();
}

void CookiesDialog::refresh()
{
  sites_->clear();
  sites_->addItems(mainApp->cookieJar()->enabledSites());
  remove_->setEnabled(false);
}

void CookiesDialog::importCookies()
{
  const QString path = QFileDialog::getOpenFileName(this, tr("Import browser cookies"),
      QString(), tr("Cookie files (*.txt);;All files (*)"));
  if (path.isEmpty()) return;
  CookieJar::Import data;
  QString error;
  if (!CookieJar::readImport(path, &data, &error)) {
    QMessageBox::warning(this, tr("Cookie import failed"), error);
    return;
  }
  if (data.cookies.isEmpty()) {
    QMessageBox::information(this, tr("Import cookies"),
        tr("No unexpired cookies were found. Expired cookies skipped: %1.").arg(data.expired));
    return;
  }

  Dialog preview(this);
  QListWidget *choices = populateSiteListDialog(preview, tr("Choose websites to enable"),
      QSize(520, 360), tr(
          "Select only the websites needed by your feeds. Their cookies will be merged "
          "with existing cookies. A leading dot includes subdomains. Session cookies "
          "will be imported for this run only.\n\nExpired cookies skipped: %1.").arg(data.expired),
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  for (const auto &site : data.sites) {
    auto *item = new QListWidgetItem(site, choices);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
  }
  if (preview.exec() != QDialog::Accepted) return;
  CookieJar::Import selected;
  for (int i = 0; i < choices->count(); ++i) {
    if (choices->item(i)->checkState() == Qt::Checked)
      selected.sites.append(choices->item(i)->text());
  }
  for (const auto &cookie : data.cookies) {
    if (selected.sites.contains(cookie.domain())) selected.cookies.append(cookie);
  }
  if (selected.cookies.isEmpty()) return;
  if (!mainApp->cookieJar()->importCookies(selected, &error))
    QMessageBox::warning(this, tr("Cookie import failed"), error);
  refresh();
}

void CookiesDialog::removeSite()
{
  if (!sites_->currentItem()) return;
  const QString site = sites_->currentItem()->text();
  if (QMessageBox::question(this, tr("Remove website cookies"),
      tr("Remove cookies and disable cookie handling for %1? This affects all feeds "
         "using this scope. A leading dot includes subdomains.").arg(site)) != QMessageBox::Yes) return;
  QString error;
  if (!mainApp->cookieJar()->removeSite(site, &error))
    QMessageBox::warning(this, tr("Could not remove cookies"), error);
  refresh();
}

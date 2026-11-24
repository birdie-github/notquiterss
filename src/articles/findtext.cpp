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
#include "findtext.h"

#include <QAction>

FindTextContent::FindTextContent(QWidget *parent)
  : SearchLineEdit(QStringLiteral(":/images/selectFindInNews"), true, parent)
{
  findInNewsAct_ = addSearchAction(QStringLiteral("findInNewsAct"),
                                   QStringLiteral(":/images/findInNews"),
                                   QStringLiteral(":/images/selectFindInNews"),
                                   true);
  findTitleAct_ = addSearchAction(QStringLiteral("findTitleAct"),
                                  QStringLiteral(":/images/findInNews"),
                                  QString(), false, true);
  findAuthorAct_ = addSearchAction(QStringLiteral("findAuthorAct"),
                                   QStringLiteral(":/images/findInNews"));
  findCategoryAct_ = addSearchAction(QStringLiteral("findCategoryAct"),
                                     QStringLiteral(":/images/findInNews"));
  findContentAct_ = addSearchAction(QStringLiteral("findContentAct"),
                                    QStringLiteral(":/images/findInNews"));
  findLinkAct_ = addSearchAction(QStringLiteral("findLinkAct"),
                                 QStringLiteral(":/images/findInNews"));
  findInBrowserAct_ = addSearchAction(QStringLiteral("findInBrowserAct"),
                                      QStringLiteral(":/images/findText"),
                                      QStringLiteral(":/images/selectFindInBrowser"),
                                      false, true);
  setPlaceholderLabel(tr("Search Articles"));
}

void FindTextContent::retranslateStrings()
{
  findInNewsAct_->setText(tr("All Articles"));
  findTitleAct_->setText(tr("Titles"));
  findAuthorAct_->setText(tr("Authors"));
  findCategoryAct_->setText(tr("Categories"));
  findContentAct_->setText(tr("Descriptions"));
  findLinkAct_->setText(tr("Links"));
  findInBrowserAct_->setText(tr("Current Article"));
  refreshLabel();
}

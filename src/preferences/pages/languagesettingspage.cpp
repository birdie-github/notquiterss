// SPDX-License-Identifier: GPL-3.0-or-later
#include "languagesettingspage.h"

#include "app/mainapplication.h"
#include "articles/rendering/articlecontent.h"
#include "preferences/languagecatalog.h"
#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"
#include "projectmetadata.h"

#include <QLabel>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

LanguageSettingsPage::LanguageSettingsPage(QWidget *parent,
                                           QVector<OptionSearchEntry> *searchEntries,
                                           int pageIndex)
  : QWidget(parent)
  , selectedLanguage_(mainApp->language())
{
  languageFileList_ = new QTreeWidget(this);
  languageFileList_->setObjectName(QStringLiteral("languageFileList_"));
  languageFileList_->setColumnCount(4);
  languageFileList_->setColumnHidden(0, true);
  languageFileList_->setColumnWidth(1, 240);
  languageFileList_->setColumnWidth(2, 160);
  languageFileList_->setHeaderLabels({QStringLiteral("Id"), OptionsDialog::tr("Language"),
                                      OptionsDialog::tr("Author"), OptionsDialog::tr("Contact")});

  const QString linkWikiStr = QStringLiteral("<a href=\"%1\">%2</a>")
      .arg(ProjectMetadata::translationsUrl().toHtmlEscaped(), OptionsDialog::tr("Translation website"));
  auto *linkTranslators = new QLabel(linkWikiStr, this);
  linkTranslators->setOpenExternalLinks(false);
  connect(linkTranslators, &QLabel::linkActivated, this, [](const QString &link) {
    const QUrl url(link);
    if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
  });

  auto *languageLayout = new QVBoxLayout(this);
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(new QLabel(OptionsDialog::tr("Choose language:"), this));
  languageLayout->addWidget(languageFileList_);
  languageLayout->addWidget(linkTranslators);

  if (searchEntries && pageIndex >= 0) {
    searchEntries->append({
        pageIndex, -1, OptionsDialog::tr("Choose language:"), QString(),
        OptionsDialog::tr("Language") + QStringLiteral(" locale translation ui"),
        languageFileList_});
  }
}

void LanguageSettingsPage::load()
{
  if (loadLanguagesOk_) return;
  loadLanguagesOk_ = true;

  const LanguageCatalog catalog = mainApp->languageCatalog();
  for (const LanguageCatalog::Entry &entry : catalog.entries()) {
    QString label = entry.name + QStringLiteral(" [%1]").arg(entry.id);
    if (!entry.emoji.isEmpty()) label.prepend(entry.emoji + QLatin1Char(' '));
    auto *item = new QTreeWidgetItem(languageFileList_, {entry.id, label, entry.author, entry.contact});
    if (!entry.fileName.isEmpty()) item->setToolTip(1, entry.fileName);
  }
  setLanguage(selectedLanguage_);
}

QString LanguageSettingsPage::language() const
{
  if (!loadLanguagesOk_) return selectedLanguage_;
  const QTreeWidgetItem *item = languageFileList_->currentItem();
  return item ? item->text(0) : QStringLiteral("en");
}

void LanguageSettingsPage::setLanguage(const QString &langFileName)
{
  selectedLanguage_ = langFileName;
  if (!loadLanguagesOk_) return;
  QList<QTreeWidgetItem *> list =
      languageFileList_->findItems(langFileName, Qt::MatchFixedString, 0);
  if (!list.isEmpty()) {
    languageFileList_->setCurrentItem(list.at(0));
  } else {
    list = languageFileList_->findItems(QStringLiteral("en"), Qt::MatchFixedString, 0);
    if (!list.isEmpty()) languageFileList_->setCurrentItem(list.at(0));
  }
}

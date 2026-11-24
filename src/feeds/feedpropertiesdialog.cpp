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
/*This file is prepared for Doxygen automatic documentation generation.*/
#include "network/feedurl.h"
#include "feedpropertiesdialog.h"
#include "articles/rendering/articlecontent.h"
#include "app/mainapplication.h"
#include "feedselectiontree.h"
#include "feedhealth.h"
#include "preferences/optionspagebuilder.h"
#include "widgets/reorderabletreecontroller.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {
template <typename Fn>
QWidget *buildPropertiesTab(Fn &&fn)
{
  auto *tab = new QWidget();
  auto *layout = new QVBoxLayout(tab);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(5);
  OptionsPageBuilder b(tab, layout, -1, -1, nullptr);
  fn(b);
  return tab;
}
} // namespace

FeedPropertiesDialog::FeedPropertiesDialog(bool isFeed, QWidget *parent, bool bulk)
  : Dialog(parent)
  , isFeed_(isFeed)
  , bulk_(bulk)
{
  setWindowTitle(bulk_ ? tr("Bulk configure feeds") : tr("Properties"));
  setMinimumWidth(500);
  setMinimumHeight(400);

  if (bulk_) {
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    bulkState_ = new QComboBox(this);
    bulkState_->addItems({tr("Enabled"), tr("Disabled")});
    layoutDirection_ = new QCheckBox(tr("Right-to-left layout"));
    bulkEditors_ << bulkState_ << createUpdateSchedule() << createImageEditor()
                 << createImagePreloadEditor() << layoutDirection_ << createColumnsTab()
                 << createFaviconUpdateEditor();
    auto *splitter = new QSplitter(this);
//    auto *actions = new QGroupBox(tr("Action to apply"));
    auto *actions = new QGroupBox();
    auto *actionsLayout = new QVBoxLayout(actions);
    bulkAction_ = new QComboBox();
    bulkAction_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    bulkAction_->addItems({tr("Choose an action..."), tr("Enable/disable"),
                          tr("Update schedule"), tr("Image loading"),
                          tr("Article image preloading"),
                          tr("Text direction"), tr("Columns and sorting"),
                          tr("Update favicons")});
    actionsLayout->addWidget(bulkAction_);
    auto *pages = new QStackedWidget();
    pages->addWidget(new QWidget());
    for (int i = 0; i < bulkEditors_.size(); ++i) {
      auto *page = new QWidget();
      auto *layout = new QVBoxLayout(page);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->addWidget(bulkEditors_.at(i));
      if (i < bulkEditors_.size() - 2) layout->addStretch();
      pages->addWidget(page);
    }
    actionsLayout->addWidget(pages, 1);
//    auto *targets = new QGroupBox(tr("Feeds and folders"));
    auto *targets = new QGroupBox();
    auto *targetsLayout = new QVBoxLayout(targets);
    auto *hint = new QLabel(tr("Check feeds to select them. Checking a folder includes its feeds and subfolders."));
    hint->setWordWrap(true);
    targetsLayout->addWidget(hint);
    bulkTargets_ = new QTreeWidget();
    FeedSelectionTree::configure(bulkTargets_);
    targetsLayout->addWidget(bulkTargets_, 1);
    splitter->addWidget(actions);
    splitter->addWidget(targets);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    pageLayout->addWidget(splitter, 1);
    bulkScope_ = new QLabel();
    bulkScope_->setWordWrap(true);
    pageLayout->addWidget(bulkScope_);
    bulkResult_ = new QLabel();
    bulkResult_->setWordWrap(true);
    pageLayout->addWidget(bulkResult_);
    connect(bulkAction_, QOverload<int>::of(&QComboBox::currentIndexChanged), pages, &QStackedWidget::setCurrentIndex);
    connect(bulkAction_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
      bulkResult_->clear();
      updateBulkApplyButton();
    });
    connect(bulkTargets_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
      if (column != 0) return;
      FeedSelectionTree::updateChecks(bulkTargets_, item);
      bulkResult_->clear();
      updateBulkApplyButton();
    });
    resize(850, 550);
  } else {
    tabWidget = new QTabWidget(this);
    tabWidget->addTab(createGeneralTab(), tr("General"));
    tabWidget->addTab(createDisplayTab(), tr("Display"));
    tabWidget->addTab(createColumnsTab(), tr("Columns"));
    const int authTabIndex = tabWidget->addTab(createAuthenticationTab(), tr("Authentication"));
    tabWidget->addTab(createStatusTab(), tr("Status"));
    if (!isFeed_) tabWidget->removeTab(authTabIndex);
    pageLayout->addWidget(tabWidget);
  }

  if (bulk_) {
    auto *apply = buttonBox->addButton(QDialogButtonBox::Apply);
    buttonBox->addButton(QDialogButtonBox::Close);
    apply->setEnabled(false);
    connect(apply, &QPushButton::clicked, this, &FeedPropertiesDialog::applyRequested);
  } else {
    buttonBox->addButton(QDialogButtonBox::Ok);
    buttonBox->addButton(QDialogButtonBox::Cancel);
  }
  connect(buttonBox, &QDialogButtonBox::accepted, this, [this] {
    if (isFeed_ && editURL->text().trimmed() != feedProperties.general.url) {
      QUrl url = FeedUrl::normalize(editURL->text());
      if (!FeedUrl::confirm(this, url)) return;
      editURL->setText(url.toString());
    }
    accept();
  });

  if (!bulk_) {
    connect(this, &FeedPropertiesDialog::signalLoadIcon,
            qobject_cast<MainWindow *>(parent), &MainWindow::faviconRequestUrl);
    connect(qobject_cast<MainWindow *>(parent), &MainWindow::signalIconFeedReady,
            this, &FeedPropertiesDialog::slotFaviconUpdate);
  }
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createGeneralTab()
{
  return buildPropertiesTab([this](OptionsPageBuilder &b) {
    auto *labelTitleCapt = new QLabel(tr("Title:"));
    auto *labelHomepageCapt = new QLabel(tr("Homepage:"));
    auto *labelURLCapt = new QLabel(tr("Feed URL:"));

    editTitle = new LineEdit();
    auto *loadTitleButton = new ToolButton();
    loadTitleButton->setObjectName("ToolButton");
    loadTitleButton->setIcon(QIcon(":/images/updateFeed"));
    loadTitleButton->setIconSize(QSize(16, 16));
    loadTitleButton->setToolTip(tr("Load Title"));
    loadTitleButton->setFocusPolicy(Qt::NoFocus);

    selectIconButton_ = new QToolButton(this);
    auto *selectIconMenu = new QMenu(selectIconButton_);
    selectIconMenu->addAction(tr("Load Favicon"));
    selectIconMenu->addSeparator();
    selectIconMenu->addAction(tr("Select Icon..."));
    selectIconButton_->setObjectName("ToolButton");
    selectIconButton_->setIconSize(QSize(16, 16));
    selectIconButton_->setToolTip(tr("Select Icon"));
    selectIconButton_->setFocusPolicy(Qt::NoFocus);
    selectIconButton_->setPopupMode(QToolButton::MenuButtonPopup);
    selectIconButton_->setMenu(selectIconMenu);

    auto *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(editTitle, 1);
    titleLayout->addWidget(loadTitleButton);
    titleLayout->addWidget(selectIconButton_);
    editURL = new LineEdit();

    b.grid(std::nullopt, -1, [&](OptionsGridBuilder &g) {
      g.addWidget(0, 0, labelTitleCapt);
      g.addLayout(0, 1, titleLayout);
      g.addWidget(1, 0, labelURLCapt);
      g.addWidget(1, 1, editURL);
    });

    b.row([this, labelHomepageCapt](OptionsRowBuilder &r) {
      labelHomepage = new QLabel();
      labelHomepage->setOpenExternalLinks(false);
      connect(labelHomepage, &QLabel::linkActivated, this, [](const QString &link) {
        const QUrl url(link);
        if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
      });
      r.addWidget(labelHomepageCapt);
      r.addWidget(labelHomepage, 1);
    }, QMargins(0, 0, 0, 0), false);

    b.spacing(15);
    b.check(disableUpdate_, tr("Disable"),
            tr("Exclude this feed from automatic, folder, and Update All operations. You can still update it individually."));
    if (!isFeed_) {
      folderDisabledCount_ = new QLabel();
      b.addWidget(folderDisabledCount_);
      auto *hint = b.label(tr("Changing this option enables or disables all feeds in this folder and its subfolders, replacing their individual disabled states. Update schedules are unchanged."));
      hint->setWordWrap(true);
      disableUpdate_->setToolTip(hint->text());
    }

    QGroupBox *updateSchedule = createUpdateSchedule();
    b.addWidget(updateSchedule);
    connect(disableUpdate_, &QCheckBox::toggled, updateSchedule, &QWidget::setDisabled);

    b.spacing(15);
    b.check(starredOn_, tr("Starred"));
    b.check(displayOnStartup, tr("Display in new tab on startup"));
    b.check(duplicateNewsMode_, tr("Automatically delete duplicate articles"));
    b.spacing(15);
    b.check(addSingleNewsAnyDateOn_, tr("Add articles regardless of publication date"));

    b.group(avoidedOldSingleNewsDateOn_,
            tr("Do not add articles published before this date to the database:"),
            true, [this](OptionsPageBuilder &grp) {
      avoidedOldSingleNewsDate_ = new QCalendarWidget();
      avoidedOldSingleNewsDate_->setSelectedDate(QDate::currentDate());
      avoidedOldSingleNewsDate_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
      avoidedOldSingleNewsDate_->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
      avoidedOldSingleNewsDate_->ensurePolished();
      avoidedOldSingleNewsDate_->setMinimumHeight(avoidedOldSingleNewsDate_->sizeHint().height() + 12);
      avoidedOldSingleNewsDate_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
      grp.addWidget(avoidedOldSingleNewsDate_);
      grp.stretch();
    }, Qt::Horizontal, QMargins(5, 5, 5, 5));
    avoidedOldSingleNewsDateOn_->setChecked(false);

    b.stretch();

    connect(addSingleNewsAnyDateOn_, &QAbstractButton::toggled,
            this, &FeedPropertiesDialog::setGroupBoxCheckboxState);
    connect(addSingleNewsAnyDateOn_, &QAbstractButton::toggled,
            avoidedOldSingleNewsDateOn_, &QWidget::setDisabled);
    connect(loadTitleButton, &QAbstractButton::clicked, this, &FeedPropertiesDialog::setDefaultTitle);
    connect(selectIconButton_, &QAbstractButton::clicked, this, &FeedPropertiesDialog::selectIcon);
    connect(selectIconMenu->actions().at(0), &QAction::triggered, this, &FeedPropertiesDialog::loadDefaultIcon);
    connect(selectIconMenu->actions().at(2), &QAction::triggered, this, &FeedPropertiesDialog::selectIcon);

    if (!isFeed_) {
      labelTitleCapt->setText(tr("Folder name:"));
      for (QWidget *w : std::initializer_list<QWidget *>{
               updateSchedule, displayOnStartup, loadTitleButton, selectIconButton_,
               labelURLCapt, editURL, labelHomepageCapt, labelHomepage, starredOn_,
               duplicateNewsMode_, addSingleNewsAnyDateOn_, avoidedOldSingleNewsDateOn_,
               avoidedOldSingleNewsDate_}) {
        w->hide();
      }
    }
  });
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createDisplayTab()
{
  return buildPropertiesTab([this](OptionsPageBuilder &b) {
    if (!isFeed_) {
      auto *hint = b.label(tr("These settings affect this folder's combined article view, not its contained feeds."));
      hint->setWordWrap(true);
    }
    b.addWidget(createImageEditor());
    QGroupBox *imagePreload = createImagePreloadEditor();
    b.addWidget(imagePreload);
    connect(imagePolicy_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            imagePreload, [imagePreload](int index) {
      imagePreload->setEnabled(index != 0);
    });
    b.check(layoutDirection_, tr("Right-to-left layout"));
    b.stretch();
  });
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createColumnsTab()
{
  return buildPropertiesTab([this](OptionsPageBuilder &b) {
    if (!isFeed_ && !bulk_) {
      auto *hint = b.label(tr("These columns and sorting affect this folder's combined article view, not its contained feeds."));
      hint->setWordWrap(true);
    }

    columnsTree_ = new QTreeWidget(this);
    columnsTree_->setObjectName("columnsTree");
    columnsTree_->setIndentation(0);
    columnsTree_->setColumnCount(2);
    columnsTree_->setColumnHidden(1, true);
    columnsTree_->setSortingEnabled(false);
    columnsTree_->setHeaderHidden(true);
    columnsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    columnsTree_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Index")});

    sortByColumnBox_ = new QComboBox(this);
    sortOrderBox_ = new QComboBox(this);
    sortOrderBox_->addItems({tr("Ascending"), tr("Descending")});

    auto *sortLayout = new QHBoxLayout();
    sortLayout->setContentsMargins(0, 0, 0, 0);
    sortLayout->addWidget(new QLabel(tr("Sort by:")));
    sortLayout->addWidget(sortByColumnBox_);
    sortLayout->addSpacing(10);
    sortLayout->addWidget(sortOrderBox_);
    sortLayout->addStretch();

    auto *mainVLayout = new QVBoxLayout();
    mainVLayout->addWidget(columnsTree_, 1);
    mainVLayout->addLayout(sortLayout);

    addButtonMenu_ = new QMenu(this);
    addButton_ = new QPushButton(tr("Add"));
    addButton_->setMenu(addButtonMenu_);
    connect(addButtonMenu_, &QMenu::aboutToShow, this, &FeedPropertiesDialog::showMenuAddButton);
    connect(addButtonMenu_, &QMenu::triggered, this, &FeedPropertiesDialog::addColumn);

    auto *removeButton = new QPushButton(tr("Remove"));
    auto *moveUpButton = new QPushButton(tr("Move up"));
    auto *moveDownButton = new QPushButton(tr("Move down"));
    auto *defaultButton = new QPushButton(tr("Default"));
    connect(defaultButton, &QAbstractButton::clicked, this, &FeedPropertiesDialog::defaultColumns);

    auto *buttonsVLayout = new QVBoxLayout();
    buttonsVLayout->addWidget(addButton_);
    buttonsVLayout->addWidget(removeButton);
    buttonsVLayout->addSpacing(10);
    buttonsVLayout->addWidget(moveUpButton);
    buttonsVLayout->addWidget(moveDownButton);
    buttonsVLayout->addSpacing(10);
    buttonsVLayout->addWidget(defaultButton);
    buttonsVLayout->addStretch();

    auto *columnsLayout = new QHBoxLayout();
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->addLayout(mainVLayout);
    columnsLayout->addLayout(buttonsVLayout);
    b.addLayout(columnsLayout);

    new ReorderableTreeController(columnsTree_, moveUpButton, moveDownButton, removeButton);
  });
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createAuthenticationTab()
{
  return buildPropertiesTab([this](OptionsPageBuilder &b) {
    b.group(authentication_, tr("Server requires authentication:"), true,
            [this](OptionsPageBuilder &grp) {
      grp.grid(std::nullopt, -1, [this](OptionsGridBuilder &g) {
        user_ = new LineEdit(this);
        pass_ = new LineEdit(this);
        pass_->setEchoMode(QLineEdit::Password);
        g.addWidget(0, 0, new QLabel(tr("Username:")));
        g.addWidget(0, 1, user_);
        g.addWidget(1, 0, new QLabel(tr("Password:")));
        g.addWidget(1, 1, pass_);
      });
    });
    authentication_->setChecked(false);
    b.stretch(1);
  });
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createStatusTab()
{
  return buildPropertiesTab([this](OptionsPageBuilder &b) {
    statusFeed_ = new QLabel();
    statusFeed_->setWordWrap(true);
    createdFeed_ = new QLabel();
    newsCount_ = new QLabel();
    auto *feedsCountLabel = new QLabel(tr("Feed count:"));
    feedsCount_ = new QLabel();
    auto *descriptionLabel = new QLabel(tr("Description:"));
    descriptionText_ = new QTextEdit();
    descriptionText_->setReadOnly(true);

    b.grid(std::nullopt, 1, [&](OptionsGridBuilder &g) {
      g.addWidget(0, 0, new QLabel(tr("Status:")));
      g.addWidget(0, 1, statusFeed_);
      g.addWidget(1, 0, new QLabel(tr("Created:")));
      g.addWidget(1, 1, createdFeed_);
      if (isFeed_) {
        lastUpdateFeed_ = new QLabel(QStringLiteral("—"));
        lastAttemptFeed_ = new QLabel(QStringLiteral("—"));
        nextCheckFeed_ = new QLabel(QStringLiteral("—"));
        auto *attemptLabel = new QLabel(tr("Last attempt:"));
        const QString attemptTip = tr("The latest recorded update attempt. This can include cancelled requests.");
        attemptLabel->setToolTip(attemptTip);
        lastAttemptFeed_->setToolTip(attemptTip);
        g.addWidget(2, 0, new QLabel(tr("Last successful update:")));
        g.addWidget(2, 1, lastUpdateFeed_);
        g.addWidget(3, 0, attemptLabel);
        g.addWidget(3, 1, lastAttemptFeed_);
        g.addWidget(4, 0, new QLabel(tr("Next check:")));
        g.addWidget(4, 1, nextCheckFeed_);
      }
      g.addWidget(5, 0, new QLabel(tr("Article count:")));
      g.addWidget(5, 1, newsCount_);
      g.addWidget(6, 0, feedsCountLabel);
      g.addWidget(6, 1, feedsCount_);
      g.addWidget(7, 0, descriptionLabel, Qt::AlignTop);
      g.addWidget(7, 1, descriptionText_, Qt::AlignTop);
    });
    b.stretch(1);

    if (!isFeed_) {
      descriptionLabel->hide();
      descriptionText_->hide();
    } else {
      feedsCountLabel->hide();
      feedsCount_->hide();
    }
  });
}
//------------------------------------------------------------------------------
/*virtual*/ void FeedPropertiesDialog::showEvent(QShowEvent *)
{
  if (initialized_) return;
  initialized_ = true;
  if (!bulk_) {
    editURL->selectAll();
    if (isFeed_) editURL->setFocus();
    else editTitle->setFocus();
    selectIconButton_->setIcon(windowIcon());
  }
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::setUpdateStatus(const QDateTime &lastSuccess, const QDateTime &lastAttempt,
                                           const QString &nextCheck)
{
  const auto localDate = [](const QDateTime &date) {
    return date.isValid() ? date.toLocalTime().toString("dd.MM.yy hh:mm") : QStringLiteral("—");
  };
  lastUpdateFeed_->setText(localDate(lastSuccess));
  lastAttemptFeed_->setText(localDate(lastAttempt));
  nextCheckFeed_->setText(nextCheck);
}

void FeedPropertiesDialog::setDefaultTitle()
{
  editTitle->setText(feedProperties.general.title);
}

void FeedPropertiesDialog::loadDefaultIcon()
{
  emit signalLoadIcon(feedProperties.general.homepage, feedProperties.general.url);
}

void FeedPropertiesDialog::selectIcon()
{
  QString filter;
  for (const QByteArray &imageFormat : QImageReader::supportedImageFormats()) {
    if (!filter.isEmpty()) filter.append(" ");
    filter.append("*.").append(imageFormat);
  }
  filter = tr("Image files") + QString(" (%1)").arg(filter);

  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Image"),
                                                  QDir::homePath(),
                                                  filter);

  if (fileName.isNull()) return;

  QMessageBox msgBox(this);
  msgBox.setText(tr("Could not open the icon file."));
  msgBox.setIcon(QMessageBox::Warning);

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    msgBox.exec();
    return;
  }

  QPixmap pixmap;
  if (pixmap.loadFromData(file.readAll())) {
    pixmap = pixmap.scaled(16, 16, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
    QByteArray faviconData;
    QBuffer    buffer(&faviconData);
    buffer.open(QIODevice::WriteOnly);
    if (pixmap.save(&buffer, "ICO")) {
      slotFaviconUpdate(feedProperties.general.url, faviconData);
    }
  } else {
    msgBox.exec();
  }

  file.close();
}

void FeedPropertiesDialog::slotFaviconUpdate(const QString &feedUrl, const QByteArray &faviconData)
{
  if (feedUrl == feedProperties.general.url) {
    feedProperties.general.image = faviconData;
    if (!faviconData.isNull()) {
      QPixmap icon;
      icon.loadFromData(faviconData);
      setWindowIcon(icon);
    } else if (isFeed_) {
      setWindowIcon(QPixmap(":/images/feed"));
    } else {
      setWindowIcon(QPixmap(":/images/folder"));
    }
    selectIconButton_->setIcon(windowIcon());
  }
}
//------------------------------------------------------------------------------
FEED_PROPERTIES FeedPropertiesDialog::getFeedProperties()
{
  feedProperties.general.text = editTitle->text();
  feedProperties.general.url = editURL->text();

  feedProperties.general.disableUpdate = disableUpdate_->isChecked();
  feedProperties.general.useGlobalUpdate = useGlobalUpdate_->isChecked();
  feedProperties.general.updateEnable = updateEnable_->isChecked();
  feedProperties.general.updateInterval = updateInterval_->value();
  feedProperties.general.intervalType = updateIntervalType_->currentIndex() - 1;

  feedProperties.general.displayOnStartup = displayOnStartup->isChecked();
  feedProperties.general.starred = starredOn_->isChecked();
  feedProperties.display.displayEmbeddedImages = imagePolicy_->currentIndex();
  feedProperties.display.useGlobalPreload = useGlobalPreload_->isChecked();
  feedProperties.display.preloadPages = preloadPages_->value();
  feedProperties.general.duplicateNewsMode = duplicateNewsMode_->isChecked();
  feedProperties.display.layoutDirection = layoutDirection_->isChecked();
  feedProperties.general.addSingleNewsAnyDateOn = addSingleNewsAnyDateOn_->isChecked();
  feedProperties.general.avoidedOldSingleNewsDateOn = avoidedOldSingleNewsDateOn_->isChecked();
  if (!avoidedOldSingleNewsDate_->selectedDate().isNull() && avoidedOldSingleNewsDate_->selectedDate().isValid()) {
    feedProperties.general.avoidedOldSingleNewsDate = avoidedOldSingleNewsDate_->selectedDate();
  } else {
    feedProperties.general.avoidedOldSingleNewsDate = QDate::currentDate();
  }

  feedProperties.column.columns.clear();
  for (int i = 0; i < columnsTree_->topLevelItemCount(); ++i) {
    int index = columnsTree_->topLevelItem(i)->text(1).toInt();
    feedProperties.column.columns.append(index);
  }
  feedProperties.column.sortBy =
      sortByColumnBox_->itemData(sortByColumnBox_->currentIndex()).toInt();
  feedProperties.column.sortType = sortOrderBox_->currentIndex();

  feedProperties.authentication.on = authentication_->isChecked();
  feedProperties.authentication.user = user_->text();
  feedProperties.authentication.pass = pass_->text();

  return (feedProperties);
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::setFeedProperties(FEED_PROPERTIES properties)
{
  feedProperties = properties;

  if (!bulk_) {
    editTitle->setText(feedProperties.general.text);
    editURL->setText(feedProperties.general.url);
    labelHomepage->setText(QString("<a href='%1'>%1</a>").arg(feedProperties.general.homepage));
    selectIconButton_->setIcon(windowIcon());
  }

  useGlobalUpdate_->setText(tr("Use global settings (%1)")
                           .arg(feedProperties.general.globalUpdateDescription));
  useGlobalUpdate_->setChecked(feedProperties.general.useGlobalUpdate);
  updateEnable_->setChecked(!feedProperties.general.useGlobalUpdate &&
                           feedProperties.general.updateEnable);
  noScheduledUpdates_->setChecked(!feedProperties.general.useGlobalUpdate &&
                                 !feedProperties.general.updateEnable);
  updateInterval_->setValue(feedProperties.general.updateInterval);
  updateIntervalType_->setCurrentIndex(feedProperties.general.intervalType + 1);
  if (!bulk_) {
    disableUpdate_->setChecked(feedProperties.general.disableUpdate);

    displayOnStartup->setChecked(feedProperties.general.displayOnStartup);
    starredOn_->setChecked(feedProperties.general.starred);
    duplicateNewsMode_->setChecked(feedProperties.general.duplicateNewsMode);

    addSingleNewsAnyDateOn_->setChecked(feedProperties.general.addSingleNewsAnyDateOn);
    avoidedOldSingleNewsDateOn_->setChecked(feedProperties.general.avoidedOldSingleNewsDateOn);
    avoidedOldSingleNewsDate_->setSelectedDate(feedProperties.general.avoidedOldSingleNewsDate);
  }

  imagePolicy_->setCurrentIndex(feedProperties.display.displayEmbeddedImages);
  useGlobalPreload_->setText(tr("Use global settings (%1)")
                             .arg(feedProperties.display.globalPreloadDescription));
  useGlobalPreload_->setChecked(feedProperties.display.useGlobalPreload);
  customPreload_->setChecked(!feedProperties.display.useGlobalPreload);
  preloadPages_->setValue(feedProperties.display.preloadPages);
  if (!bulk_)
    imagePreloadGroup_->setEnabled(feedProperties.display.displayEmbeddedImages != 0);
  layoutDirection_->setChecked(feedProperties.display.layoutDirection);

  columnsTree_->clear();
  for (int i = 0; i < feedProperties.column.columns.count(); ++i) {
    int index = feedProperties.column.indexList.indexOf(feedProperties.column.columns.at(i));
    QString name = feedProperties.column.nameList.at(index);
    columnsTree_->addTopLevelItem(new QTreeWidgetItem(QStringList{
        name, QString::number(feedProperties.column.columns.at(i))}));
  }
  sortByColumnBox_->clear();
  for (int i = 0; i < feedProperties.column.indexList.count(); ++i) {
    sortByColumnBox_->addItem(feedProperties.column.nameList.at(i),
                              feedProperties.column.indexList.at(i));
    if (feedProperties.column.sortBy == feedProperties.column.indexList.at(i))
      sortByColumnBox_->setCurrentIndex(i);
  }
  sortOrderBox_->setCurrentIndex(feedProperties.column.sortType);
  columnsTree_->setCurrentIndex(QModelIndex());

  if (bulk_) return;

  authentication_->setChecked(feedProperties.authentication.on);
  user_->setText(feedProperties.authentication.user);
  pass_->setText(feedProperties.authentication.pass);

  const auto health = FeedHealth::read(feedProperties.status.feedStatus);
  statusFeed_->setText(health.error.isEmpty() ? tr("Good") : health.error);

  descriptionText_->setText(feedProperties.status.description);
  if (feedProperties.status.createdTime.isValid())
    createdFeed_->setText(feedProperties.status.createdTime.toString("dd.MM.yy hh:mm"));
  else
    createdFeed_->setText(tr("Long ago ;-)"));

  newsCount_->setText(QString("%1 (%2 %3, %4 %5)").
                      arg(feedProperties.status.undeleteCount).
                      arg(feedProperties.status.newCount).
                      arg(tr("new")).
                      arg(feedProperties.status.unreadCount).
                      arg(tr("unread")));
  feedsCount_->setText(QString("%1").arg(feedProperties.status.feedsCount));
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::showMenuAddButton()
{
  QListIterator<QAction *> iter(addButtonMenu_->actions());
  while (iter.hasNext()) {
    QAction *nextAction = iter.next();
    delete nextAction;
  }

  for (int i = 0; i < feedProperties.column.indexList.count(); ++i) {
    int index = feedProperties.column.indexList.at(i);
    QList<QTreeWidgetItem *> treeItems = columnsTree_->findItems(QString::number(index),
                                                                 Qt::MatchFixedString,
                                                                 1);
    if (!treeItems.count()) {
      QAction *action = addButtonMenu_->addAction(feedProperties.column.nameList.at(i));
      action->setData(index);
    }
  }
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::addColumn(QAction *action)
{
  QStringList treeItem;
  treeItem << action->text() << action->data().toString();
  QTreeWidgetItem *item = new QTreeWidgetItem(treeItem);
  columnsTree_->addTopLevelItem(item);
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::defaultColumns()
{
  columnsTree_->clear();
  for (int i = 0; i < feedProperties.columnDefault.columns.count(); ++i) {
    int index = feedProperties.column.indexList.indexOf(feedProperties.columnDefault.columns.at(i));
    QString name = feedProperties.column.nameList.at(index);
    QStringList treeItem;
    treeItem << name
             << QString::number(feedProperties.columnDefault.columns.at(i));
    QTreeWidgetItem *item = new QTreeWidgetItem(treeItem);
    columnsTree_->addTopLevelItem(item);
  }
  for (int i = 0; i < feedProperties.column.indexList.count(); ++i) {
    if (feedProperties.columnDefault.sortBy == feedProperties.column.indexList.at(i))
      sortByColumnBox_->setCurrentIndex(i);
  }
  sortOrderBox_->setCurrentIndex(feedProperties.columnDefault.sortType);
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::setGroupBoxCheckboxState(bool _on)
{
  if (_on) {
    avoidedOldSingleNewsDateOn_->setChecked(false);
  } else {
    avoidedOldSingleNewsDateOn_->setChecked(true);
  }
}

bool FeedPropertiesDialog::loadBulkTargets(QSqlDatabase db, bool defaultIcons, QString &error)
{
  const bool ok = FeedSelectionTree::populate(bulkTargets_, db, defaultIcons, tr("All feeds"), -1, error);
  updateBulkApplyButton();
  return ok;
}

QList<int> FeedPropertiesDialog::bulkFeedIds() const
{
  return FeedSelectionTree::checkedFeeds(bulkTargets_);
}

QString FeedPropertiesDialog::bulkActionName() const
{
  return bulkAction_->currentText();
}

void FeedPropertiesDialog::updateBulkApplyButton()
{
  const int count = bulkFeedIds().size();
  bulkScope_->setText(tr("%1 feeds selected. Only the displayed action will be applied. Folder settings will not change.").arg(count));
  const int action = bulkAction_->currentIndex();
  const bool isFaviconAction = (action == 7);
  if (auto *apply = buttonBox->button(QDialogButtonBox::Apply))
    apply->setEnabled(!faviconUpdateRunning_ && count > 0 && action > 0 && !isFaviconAction);
  if (auto *close = buttonBox->button(QDialogButtonBox::Close))
    close->setEnabled(!faviconUpdateRunning_);
  if (faviconActionButton_)
    faviconActionButton_->setEnabled(faviconUpdateRunning_ || count > 0);
  if (bulkAction_)
    bulkAction_->setEnabled(!faviconUpdateRunning_);
  if (bulkTargets_)
    bulkTargets_->setEnabled(!faviconUpdateRunning_);
}

void FeedPropertiesDialog::bulkApplySucceeded(int count)
{
  bulkResult_->setText(tr("%1 applied to %2 feeds.").arg(bulkActionName()).arg(count));
}

void FeedPropertiesDialog::startFaviconUpdate(int totalFeeds)
{
  faviconUpdateRunning_ = true;
  faviconStatusPos_ = -1;
  bulkResult_->clear();
  faviconLog_->clear();
  faviconProgress_->setRange(0, qMax(1, totalFeeds));
  faviconProgress_->setValue(0);
  faviconProgress_->show();
  faviconActionButton_->setText(tr("Cancel"));
  updateBulkApplyButton();
}

void FeedPropertiesDialog::appendFaviconLog(const QString &text, bool newLine)
{
  if (!faviconLog_) return;
  faviconStatusPos_ = -1;
  QTextCursor cursor(faviconLog_->document());
  cursor.movePosition(QTextCursor::End);
  if (newLine && !faviconLog_->document()->isEmpty())
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
  cursor.insertText(text, QTextCharFormat());
  if (!newLine)
    faviconLog_->setTextCursor(cursor);
  if (QScrollBar *bar = faviconLog_->verticalScrollBar())
    bar->setValue(bar->maximum());
}

void FeedPropertiesDialog::updateFaviconStatusText(const QString &text, bool finalizeStatus)
{
  if (!faviconLog_) return;
  QTextCursor cursor = faviconLog_->textCursor();
  if (faviconStatusPos_ >= 0) {
    cursor.setPosition(faviconStatusPos_);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(text);
  } else {
    cursor.movePosition(QTextCursor::End);
    faviconStatusPos_ = cursor.position();
    cursor.insertText(text);
  }
  if (finalizeStatus)
    faviconStatusPos_ = -1;
  faviconLog_->setTextCursor(cursor);
  if (QScrollBar *bar = faviconLog_->verticalScrollBar())
    bar->setValue(bar->maximum());
}

void FeedPropertiesDialog::colorFaviconLogLine(FaviconLogResult result)
{
  const bool darkBackground = qGray(faviconLog_->palette().color(QPalette::Base).rgb()) < 128;
  const QColor updated = darkBackground ? QColor("#77dd88") : QColor("#176b32");
  const QColor failed = darkBackground ? QColor("#ff8585") : QColor("#ad2020");
  QTextCharFormat format;
  format.setForeground(result == FaviconLogResult::Updated ? updated : failed);

  QTextCursor cursor(faviconLog_->document());
  cursor.movePosition(QTextCursor::End);
  cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
  cursor.mergeCharFormat(format);
}

void FeedPropertiesDialog::advanceFaviconProgress(int current, int total)
{
  if (!faviconProgress_) return;
  faviconProgress_->setRange(0, qMax(1, total));
  faviconProgress_->setValue(current);
}

void FeedPropertiesDialog::finishFaviconUpdate(const QString &summary)
{
  faviconUpdateRunning_ = false;
  faviconProgress_->hide();
  faviconActionButton_->setText(tr("Start"));
  faviconStatusPos_ = -1;
  if (!summary.isEmpty()) {
    appendFaviconLog(QString(), true);
    appendFaviconLog(summary, true);
  }
  updateBulkApplyButton();
}

void FeedPropertiesDialog::updateBulkTargetIcon(int feedId, const QByteArray &faviconData, bool defaultIcons)
{
  if (!bulkTargets_) return;
  const QList<QTreeWidgetItem *> items = bulkTargets_->findItems(
      QString::number(feedId), Qt::MatchExactly | Qt::MatchRecursive, 1);
  if (items.isEmpty()) return;
  QPixmap icon;
  if (faviconData.isNull() || defaultIcons) {
    icon.load(QStringLiteral(":/images/feed"));
  } else {
    icon.loadFromData(faviconData);
    if (icon.isNull()) icon.load(QStringLiteral(":/images/feed"));
  }
  for (QTreeWidgetItem *item : items) {
    if (item->data(0, FeedSelectionTree::IsFeedRole).toBool())
      item->setIcon(0, icon);
  }
}

void FeedPropertiesDialog::uncheckBulkTarget(int feedId)
{
  if (!bulkTargets_) return;
  const QList<QTreeWidgetItem *> items = bulkTargets_->findItems(
      QString::number(feedId), Qt::MatchExactly | Qt::MatchRecursive, 1);
  bool changed = false;
  for (QTreeWidgetItem *item : items) {
    if (item->data(0, FeedSelectionTree::IsFeedRole).toBool() &&
        item->checkState(0) != Qt::Unchecked) {
      const QSignalBlocker blocker(bulkTargets_);
      item->setCheckState(0, Qt::Unchecked);
      FeedSelectionTree::updateChecks(bulkTargets_, item);
      changed = true;
    }
  }
  if (changed)
    updateBulkApplyButton();
}

void FeedPropertiesDialog::cancelFaviconUpdateForShutdown()
{
  if (faviconUpdateRunning_)
    emit cancelFaviconUpdateRequested();
}

void FeedPropertiesDialog::reject()
{
  if (faviconUpdateRunning_) {
    if (!mainApp || !mainApp->isClosing()) return;
    cancelFaviconUpdateForShutdown();
  }
  Dialog::reject();
}

void FeedPropertiesDialog::closeEvent(QCloseEvent *event)
{
  if (faviconUpdateRunning_) {
    if (!mainApp || !mainApp->isClosing()) {
      event->ignore();
      return;
    }
    cancelFaviconUpdateForShutdown();
  }
  Dialog::closeEvent(event);
}

FeedBulkSettings::Columns FeedPropertiesDialog::columnSettings() const
{
  QList<int> columns;
  for (int i = 0; i < columnsTree_->topLevelItemCount(); ++i)
    columns.append(columnsTree_->topLevelItem(i)->text(1).toInt());
  const int sortBy = sortByColumnBox_->currentData().toInt();
  const int sortOrder = sortOrderBox_->currentIndex();
  if (columns == feedProperties.columnDefault.columns &&
      sortBy == feedProperties.columnDefault.sortBy &&
      sortOrder == feedProperties.columnDefault.sortType)
    return {QString(), 0, 0};
  QString serialized = ",";
  for (int column : columns) serialized += QString::number(column) + ",";
  return {serialized, sortBy, sortOrder};
}

FeedBulkSettings::Changes FeedPropertiesDialog::bulkChanges()
{
  FeedBulkSettings::Changes changes;
  switch (bulkAction_->currentIndex()) {
  case 1:
    changes.disabled = bulkState_->currentIndex() == 1;
    break;
  case 2:
    changes.schedule = FeedBulkSettings::Schedule{
        useGlobalUpdate_->isChecked() ? -1 : (updateEnable_->isChecked() ? 1 : 0),
        updateInterval_->value(), updateIntervalType_->currentIndex() - 1};
    break;
  case 3:
    changes.images = imagePolicy_->currentIndex();
    break;
  case 4:
    changes.preloadImages = useGlobalPreload_->isChecked() ? -1 : preloadPages_->value();
    break;
  case 5:
    changes.rightToLeft = layoutDirection_->isChecked();
    break;
  case 6:
    changes.columns = columnSettings();
    break;
  default:
    break;
  }
  return changes;
}

QGroupBox *FeedPropertiesDialog::createUpdateSchedule()
{
  auto *updateSchedule = new QGroupBox(tr("Update schedule"));
  auto *scheduleLayout = new QVBoxLayout(updateSchedule);
  OptionsPageBuilder b(updateSchedule, scheduleLayout, -1, -1, nullptr);

  b.radio(useGlobalUpdate_, QString());
  b.row([this, updateSchedule](OptionsRowBuilder &r) {
    updateEnable_ = new QRadioButton(tr("Update every"), updateSchedule);
    r.addWidget(updateEnable_);
    r.spin(updateInterval_, 1, 9999);
    r.combo(updateIntervalType_, {tr("seconds"), tr("minutes"), tr("hours")});
    OptionsPageBuilder::bindEnabled(updateEnable_, {updateInterval_, updateIntervalType_});
  }, QMargins(0, 0, 0, 0));
  b.radio(noScheduledUpdates_, tr("No scheduled updates"));
  noScheduledUpdates_->setToolTip(tr("Startup updates and manual updates, including Update All, are still allowed."));
  return updateSchedule;
}

QWidget *FeedPropertiesDialog::createImageEditor()
{
  auto *imageEditor = new QWidget();
  auto *imageLayout = new QVBoxLayout(imageEditor);
  imageLayout->setContentsMargins(0, 0, 0, 0);
  OptionsPageBuilder b(imageEditor, imageLayout, -1, -1, nullptr);
  b.comboRow(imagePolicy_, tr("Load images:"),
             {tr("Never"), tr("Use global settings"), tr("Always")});
  return imageEditor;
}

QGroupBox *FeedPropertiesDialog::createImagePreloadEditor()
{
  imagePreloadGroup_ = new QGroupBox(tr("Article image preloading"));
  auto *preloadLayout = new QVBoxLayout(imagePreloadGroup_);
  OptionsPageBuilder b(imagePreloadGroup_, preloadLayout, -1, -1, nullptr);

  b.radio(useGlobalPreload_, QString());
  b.row([this](OptionsRowBuilder &r) {
    customPreload_ = new QRadioButton(tr("Preload up to"), imagePreloadGroup_);
    r.addWidget(customPreload_);
    r.spin(preloadPages_, 0, 10, QString(), tr("0 disables image preloading"));
    r.label(tr("pages"));
    OptionsPageBuilder::bindEnabled(customPreload_, {preloadPages_});
  }, QMargins(0, 0, 0, 0));
  return imagePreloadGroup_;
}

QWidget *FeedPropertiesDialog::createFaviconUpdateEditor()
{
  QWidget *editor = new QWidget(this);
  auto *layout = new QVBoxLayout(editor);
  layout->setContentsMargins(0, 0, 0, 0);

  faviconLog_ = new QPlainTextEdit(editor);
  faviconLog_->setReadOnly(true);
  layout->addWidget(faviconLog_, 1);

  faviconProgress_ = new QProgressBar(editor);
  faviconProgress_->setFormat(QStringLiteral("%v / %m"));
  faviconProgress_->hide();
  layout->addWidget(faviconProgress_);

  faviconActionButton_ = new QPushButton(tr("Start"), editor);
  faviconActionButton_->setEnabled(false);
  layout->addWidget(faviconActionButton_, 0, Qt::AlignHCenter);
  connect(faviconActionButton_, &QPushButton::clicked, this, [this] {
    if (faviconUpdateRunning_)
      emit cancelFaviconUpdateRequested();
    else
      emit startFaviconUpdateRequested();
  });

  return editor;
}

void FeedPropertiesDialog::setFolderDisabledCounts(int total, int disabled)
{
  folderInitiallyDisabled_ = total > 0 && disabled == total;
  feedProperties.general.disableUpdate = folderInitiallyDisabled_;
  disableUpdate_->setChecked(folderInitiallyDisabled_);
  disableUpdate_->setEnabled(total > 0);
  folderDisabledCount_->setText(tr("%1 of %2 feeds disabled.").arg(disabled).arg(total));
}

bool FeedPropertiesDialog::folderDisableChanged() const
{
  return !isFeed_ && !bulk_ && disableUpdate_->isEnabled() &&
      disableUpdate_->isChecked() != folderInitiallyDisabled_;
}

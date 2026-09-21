/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
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
#include "articlecontent.h"
#include "mainapplication.h"
#include "feedselectiontree.h"

FeedPropertiesDialog::FeedPropertiesDialog(bool isFeed, QWidget *parent, bool bulk)
  : Dialog(parent)
  , isFeed_(isFeed)
  , bulk_(bulk)
{
  setWindowFlags (windowFlags() & ~Qt::WindowContextHelpButtonHint);
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
                 << layoutDirection_ << createColumnsTab();
    auto *splitter = new QSplitter(this);
    auto *actions = new QGroupBox(tr("Action to apply"));
    auto *actionsLayout = new QVBoxLayout(actions);
    bulkAction_ = new QComboBox();
    bulkAction_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    bulkAction_->addItems({tr("Choose an action..."), tr("Enabled/disabled state"),
                          tr("Update schedule"), tr("Image loading"),
                          tr("Text direction"), tr("Columns and sorting")});
    actionsLayout->addWidget(bulkAction_);
    auto *pages = new QStackedWidget();
    pages->addWidget(new QWidget());
    for (int i = 0; i < bulkEditors_.size(); ++i) {
      auto *page = new QWidget();
      auto *layout = new QVBoxLayout(page);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->addWidget(bulkEditors_.at(i));
      if (i != 4) layout->addStretch();
      pages->addWidget(page);
    }
    actionsLayout->addWidget(pages, 1);
    auto *targets = new QGroupBox(tr("Feeds and folders"));
    auto *targetsLayout = new QVBoxLayout(targets);
    auto *hint = new QLabel(tr("Check feeds to select them. Checking a folder includes its feeds and subfolders."));
    hint->setWordWrap(true);
    targetsLayout->addWidget(hint);
    bulkTargets_ = new QTreeWidget();
    bulkTargets_->setColumnCount(2);
    bulkTargets_->setColumnHidden(1, true);
    bulkTargets_->setHeaderHidden(true);
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
    connect(this, SIGNAL(signalLoadIcon(QString,QString)),
            parent, SIGNAL(faviconRequestUrl(QString,QString)));
    connect(parent, SIGNAL(signalIconFeedReady(QString,QByteArray)),
            this, SLOT(slotFaviconUpdate(QString,QByteArray)));
  }
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createGeneralTab()
{
  QWidget *tab = new QWidget();

  QGridLayout *layoutGeneralGrid = new QGridLayout();
  QLabel *labelTitleCapt = new QLabel(tr("Title:"));
  QLabel *labelHomepageCapt = new QLabel(tr("Homepage:"));
  QLabel *labelURLCapt = new QLabel(tr("Feed URL:"));

  QHBoxLayout *layoutGeneralTitle = new QHBoxLayout();
  editTitle = new LineEdit();
  ToolButton *loadTitleButton = new ToolButton();
  loadTitleButton->setObjectName("ToolButton");
  loadTitleButton->setIcon(QIcon(":/images/updateFeed"));
  loadTitleButton->setIconSize(QSize(16, 16));
  loadTitleButton->setToolTip(tr("Load Title"));
  loadTitleButton->setFocusPolicy(Qt::NoFocus);

  QMenu *selectIconMenu = new QMenu();
  selectIconMenu->addAction(tr("Load Favicon"));
  selectIconMenu->addSeparator();
  selectIconMenu->addAction(tr("Select Icon..."));
  selectIconButton_ = new QToolButton(this);
  selectIconButton_->setObjectName("ToolButton");
  selectIconButton_->setIconSize(QSize(16, 16));
  selectIconButton_->setToolTip(tr("Select Icon"));
  selectIconButton_->setFocusPolicy(Qt::NoFocus);
  selectIconButton_->setPopupMode(QToolButton::MenuButtonPopup);
  selectIconButton_->setMenu(selectIconMenu);

  layoutGeneralTitle->addWidget(editTitle, 1);
  layoutGeneralTitle->addWidget(loadTitleButton);
  layoutGeneralTitle->addWidget(selectIconButton_);
  editURL = new LineEdit();

  disableUpdate_ = new QCheckBox(tr("Disable"), this);
  disableUpdate_->setToolTip(tr("Disabled feeds are excluded from all updates, including manual updates."));
  disableUpdate_->setChecked(false);

  QGroupBox *updateSchedule = createUpdateSchedule();
  connect(disableUpdate_, &QCheckBox::toggled,
          updateSchedule, &QWidget::setDisabled);

  starredOn_ = new QCheckBox(tr("Starred"));
  displayOnStartup = new QCheckBox(tr("Display in new tab on startup"));
  duplicateNewsMode_ = new QCheckBox(tr("Automatically delete duplicate articles"));

  QHBoxLayout *layoutGeneralHomepage = new QHBoxLayout();
  labelHomepage = new QLabel();
  labelHomepage->setOpenExternalLinks(false);
  connect(labelHomepage, &QLabel::linkActivated, this, [](const QString &link) {
    const QUrl url(link);
    if (ArticleContent::isExternalLink(url)) mainApp->openExternalUrl(url);
  });
  layoutGeneralHomepage->addWidget(labelHomepageCapt);
  layoutGeneralHomepage->addWidget(labelHomepage, 1);

  layoutGeneralGrid->addWidget(labelTitleCapt, 0, 0);
  layoutGeneralGrid->addLayout(layoutGeneralTitle, 0 ,1);
  layoutGeneralGrid->addWidget(labelURLCapt, 1, 0);
  layoutGeneralGrid->addWidget(editURL, 1, 1);

  addSingleNewsAnyDateOn_ = new QCheckBox(tr("Add articles regardless of publication date"));
  addSingleNewsAnyDateOn_->setCheckable(true);
  addSingleNewsAnyDateOn_->setChecked(false);

  avoidedOldSingleNewsDate_ = new QCalendarWidget();
  avoidedOldSingleNewsDate_->setSelectedDate(QDate::currentDate());
  avoidedOldSingleNewsDate_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  avoidedOldSingleNewsDate_->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
  QHBoxLayout *avoidedOldNewsDateLayout = new QHBoxLayout();
  avoidedOldNewsDateLayout->setContentsMargins(5, 5, 5, 5);
  avoidedOldNewsDateLayout->addWidget(avoidedOldSingleNewsDate_);
  avoidedOldNewsDateLayout->addStretch();

  avoidedOldSingleNewsDateOn_ = new QGroupBox(tr("Do not add articles published before this date to the database:"));
  avoidedOldSingleNewsDateOn_->setCheckable(true);
  avoidedOldSingleNewsDateOn_->setChecked(false);
  avoidedOldSingleNewsDateOn_->setLayout(avoidedOldNewsDateLayout);

  QVBoxLayout *tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(5);
  tabLayout->addLayout(layoutGeneralGrid);
  tabLayout->addLayout(layoutGeneralHomepage);
  tabLayout->addSpacing(15);
  tabLayout->addWidget(disableUpdate_);
  if (!isFeed_) {
    folderDisabledCount_ = new QLabel();
    tabLayout->addWidget(folderDisabledCount_);
    auto *hint = new QLabel(tr("Changing this option enables or disables all feeds in this folder and its subfolders, replacing their individual disabled states. Update schedules are unchanged."));
    hint->setWordWrap(true);
    tabLayout->addWidget(hint);
    disableUpdate_->setToolTip(hint->text());
  }
  tabLayout->addWidget(updateSchedule);
  tabLayout->addSpacing(15);
  tabLayout->addWidget(starredOn_);
  tabLayout->addWidget(displayOnStartup);
  tabLayout->addWidget(duplicateNewsMode_);
  tabLayout->addSpacing(15);
  tabLayout->addWidget(addSingleNewsAnyDateOn_);
  tabLayout->addWidget(avoidedOldSingleNewsDateOn_);
  tabLayout->addStretch();

  connect(addSingleNewsAnyDateOn_, SIGNAL(toggled(bool)),
          this, SLOT(setGroupBoxCheckboxState(bool)));
  connect(addSingleNewsAnyDateOn_, SIGNAL(toggled(bool)),
          avoidedOldSingleNewsDateOn_, SLOT(setDisabled(bool)));

  connect(loadTitleButton, SIGNAL(clicked()), this, SLOT(setDefaultTitle()));
  connect(selectIconButton_, SIGNAL(clicked()),
          this, SLOT(selectIcon()));
  connect(selectIconMenu->actions().at(0), SIGNAL(triggered()),
          this, SLOT(loadDefaultIcon()));
  connect(selectIconMenu->actions().at(2), SIGNAL(triggered()),
          this, SLOT(selectIcon()));

  if (!isFeed_) {
    labelTitleCapt->setText(tr("Folder name:"));
    updateSchedule->hide();
    displayOnStartup->hide();
    loadTitleButton->hide();
    selectIconButton_->hide();
    labelURLCapt->hide();
    editURL->hide();
    labelHomepageCapt->hide();
    labelHomepage->hide();
    starredOn_->hide();
    duplicateNewsMode_->hide();
    addSingleNewsAnyDateOn_->hide();
    avoidedOldSingleNewsDateOn_->hide();
    avoidedOldSingleNewsDate_->hide();
  }

  return tab;
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createDisplayTab()
{
  QWidget *tab = new QWidget();

  QWidget *imageEditor = createImageEditor();

  layoutDirection_ = new QCheckBox(tr("Right-to-left layout"));

  QVBoxLayout *tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(5);
  if (!isFeed_) {
    auto *hint = new QLabel(tr("These settings affect this folder's combined article view, not its contained feeds."));
    hint->setWordWrap(true);
    tabLayout->addWidget(hint);
  }
  tabLayout->addWidget(imageEditor);
  tabLayout->addWidget(layoutDirection_);

  tabLayout->addStretch();

  return tab;
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createColumnsTab()
{
  QWidget *tab = new QWidget();

  columnsTree_ = new QTreeWidget(this);
  columnsTree_->setObjectName("columnsTree");
  columnsTree_->setIndentation(0);
  columnsTree_->setColumnCount(2);
  columnsTree_->setColumnHidden(1, true);
  columnsTree_->setSortingEnabled(false);
  columnsTree_->setHeaderHidden(true);
  columnsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);

  QStringList treeItem;
  treeItem << "Name" << "Index";
  columnsTree_->setHeaderLabels(treeItem);

  sortByColumnBox_ = new QComboBox(this);

  sortOrderBox_ = new QComboBox(this);
  treeItem.clear();
  treeItem << tr("Ascending") << tr("Descending");
  sortOrderBox_->addItems(treeItem);

  QHBoxLayout *styleLayout = new QHBoxLayout();
  styleLayout->setContentsMargins(0, 0, 0, 0);
  styleLayout->addWidget(new QLabel(tr("Sort by:")));
  styleLayout->addWidget(sortByColumnBox_);
  styleLayout->addSpacing(10);
  styleLayout->addWidget(sortOrderBox_);
  styleLayout->addStretch();

  QWidget *styleWidget = new QWidget(this);
  styleWidget->setLayout(styleLayout);

  QVBoxLayout *mainVLayout = new QVBoxLayout();
  mainVLayout->addWidget(columnsTree_, 1);
  mainVLayout->addWidget(styleWidget);

  addButtonMenu_ = new QMenu(this);
  addButton_ = new QPushButton(tr("Add"));
  addButton_->setMenu(addButtonMenu_);
  connect(addButtonMenu_, SIGNAL(aboutToShow()),
          this, SLOT(showMenuAddButton()));
  connect(addButtonMenu_, SIGNAL(triggered(QAction*)),
          this, SLOT(addColumn(QAction*)));

  removeButton_ = new QPushButton(tr("Remove"));
  removeButton_->setEnabled(false);
  connect(removeButton_, SIGNAL(clicked()), this, SLOT(removeColumn()));

  moveUpButton_ = new QPushButton(tr("Move up"));
  moveUpButton_->setEnabled(false);
  connect(moveUpButton_, SIGNAL(clicked()), this, SLOT(moveUpColumn()));
  moveDownButton_ = new QPushButton(tr("Move down"));
  moveDownButton_->setEnabled(false);
  connect(moveDownButton_, SIGNAL(clicked()), this, SLOT(moveDownColumn()));

  QPushButton *defaultButton = new QPushButton(tr("Default"));
  connect(defaultButton, SIGNAL(clicked()), this, SLOT(defaultColumns()));

  QVBoxLayout *buttonsVLayout = new QVBoxLayout();
  buttonsVLayout->addWidget(addButton_);
  buttonsVLayout->addWidget(removeButton_);
  buttonsVLayout->addSpacing(10);
  buttonsVLayout->addWidget(moveUpButton_);
  buttonsVLayout->addWidget(moveDownButton_);
  buttonsVLayout->addSpacing(10);
  buttonsVLayout->addWidget(defaultButton);
  buttonsVLayout->addStretch();

  QWidget *columnsEditor = new QWidget();
  auto *columnsLayout = new QHBoxLayout(columnsEditor);
  columnsLayout->setContentsMargins(0, 0, 0, 0);
  columnsLayout->addLayout(mainVLayout);
  columnsLayout->addLayout(buttonsVLayout);
  auto *tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(5);
  if (!isFeed_ && !bulk_) {
    auto *hint = new QLabel(tr("These columns and sorting affect this folder's combined article view, not its contained feeds."));
    hint->setWordWrap(true);
    tabLayout->addWidget(hint);
  }
  tabLayout->addWidget(columnsEditor);

  connect(columnsTree_, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
          this, SLOT(slotCurrentColumnChanged(QTreeWidgetItem*,QTreeWidgetItem*)));

  return tab;
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createAuthenticationTab()
{
  QWidget *tab = new QWidget();

  authentication_ = new QGroupBox(this);
  authentication_->setTitle(tr("Server requires authentication:"));
  authentication_->setCheckable(true);
  authentication_->setChecked(false);

  user_ = new LineEdit(this);
  pass_ = new LineEdit(this);
  pass_->setEchoMode(QLineEdit::Password);

  QGridLayout *authenticationLayout = new QGridLayout();
  authenticationLayout->addWidget(new QLabel(tr("Username:")), 2, 0);
  authenticationLayout->addWidget(user_, 2, 1);
  authenticationLayout->addWidget(new QLabel(tr("Password:")), 3, 0);
  authenticationLayout->addWidget(pass_, 3, 1);

  authentication_->setLayout(authenticationLayout);

  QVBoxLayout *tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(5);
  tabLayout->addWidget(authentication_);
  tabLayout->addStretch(1);

  return tab;
}
//------------------------------------------------------------------------------
QWidget *FeedPropertiesDialog::createStatusTab()
{
  QWidget *tab = new QWidget();

  statusFeed_ = new QLabel();
  statusFeed_->setWordWrap(true);
  createdFeed_ = new QLabel();
  lastUpdateFeed_ = new QLabel();
  newsCount_ = new QLabel();

  QLabel *feedsCountLabel = new QLabel(tr("Feed count:"));
  feedsCount_ = new QLabel();

  QLabel *descriptionLabel = new QLabel(tr("Description:"));

  descriptionText_ = new QTextEdit();
  descriptionText_->setReadOnly(true);

  QGridLayout *layoutGrid = new QGridLayout();
  layoutGrid->setColumnStretch(1,1);
  layoutGrid->addWidget(new QLabel(tr("Status:")), 0, 0);
  layoutGrid->addWidget(statusFeed_, 0, 1);
  layoutGrid->addWidget(new QLabel(tr("Created:")), 1, 0);
  layoutGrid->addWidget(createdFeed_, 1, 1);
  layoutGrid->addWidget(new QLabel(tr("Last update:")), 2, 0);
  layoutGrid->addWidget(lastUpdateFeed_, 2, 1);
  layoutGrid->addWidget(new QLabel(tr("Article count:")), 3, 0);
  layoutGrid->addWidget(newsCount_, 3, 1);
  layoutGrid->addWidget(feedsCountLabel, 4, 0);
  layoutGrid->addWidget(feedsCount_, 4, 1);
  layoutGrid->addWidget(descriptionLabel, 5, 0, 1, 1, Qt::AlignTop);
  layoutGrid->addWidget(descriptionText_, 5, 1, 1, 1, Qt::AlignTop);

  QVBoxLayout *tabLayout = new QVBoxLayout(tab);
  tabLayout->setContentsMargins(10, 10, 10, 10);
  tabLayout->setSpacing(5);
  tabLayout->addLayout(layoutGrid);
  tabLayout->addStretch(1);

  if (!isFeed_) {
    descriptionLabel->hide();
    descriptionText_->hide();
  } else {
    feedsCountLabel->hide();
    feedsCount_->hide();
  }

  return tab;
}
//------------------------------------------------------------------------------
/*virtual*/ void FeedPropertiesDialog::showEvent(QShowEvent *)
{
  if (initialized_) return;
  initialized_ = true;
  if (!bulk_) {
    editTitle->setText(feedProperties.general.text);
    editURL->setText(feedProperties.general.url);
    editURL->selectAll();
    if (isFeed_) editURL->setFocus();
    else editTitle->setFocus();
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
  layoutDirection_->setChecked(feedProperties.display.layoutDirection);

  for (int i = 0; i < feedProperties.column.columns.count(); ++i) {
    int index = feedProperties.column.indexList.indexOf(feedProperties.column.columns.at(i));
    QString name = feedProperties.column.nameList.at(index);
    QStringList treeItem;
    treeItem << name
             << QString::number(feedProperties.column.columns.at(i));
    QTreeWidgetItem *item = new QTreeWidgetItem(treeItem);
    columnsTree_->addTopLevelItem(item);
  }
  for (int i = 0; i < feedProperties.column.indexList.count(); ++i) {
    sortByColumnBox_->addItem(feedProperties.column.nameList.at(i),
                              feedProperties.column.indexList.at(i));
    if (feedProperties.column.sortBy == feedProperties.column.indexList.at(i))
      sortByColumnBox_->setCurrentIndex(i);
  }
  sortOrderBox_->setCurrentIndex(feedProperties.column.sortType);

  if (bulk_) return;

  authentication_->setChecked(feedProperties.authentication.on);
  user_->setText(feedProperties.authentication.user);
  pass_->setText(feedProperties.authentication.pass);

  QString status = feedProperties.status.feedStatus;
  if (status.isEmpty() || (status == "0"))
    statusFeed_->setText(tr("Good"));
  else
    statusFeed_->setText(status.section(" ", 1));

  descriptionText_->setText(feedProperties.status.description);
  if (feedProperties.status.createdTime.isValid())
    createdFeed_->setText(feedProperties.status.createdTime.toString("dd.MM.yy hh:mm"));
  else
    createdFeed_->setText(tr("Long ago ;-)"));

  QString lastBuildDate = feedProperties.status.lastBuildDate.toString("dd.MM.yy hh:mm");
  if (!lastBuildDate.isEmpty()) lastBuildDate = QString(" (%1)").arg(lastBuildDate);
  lastUpdateFeed_->setText(QString("%1%2").
                           arg(feedProperties.status.lastUpdate.toString("dd.MM.yy hh:mm")).
                           arg(lastBuildDate)
                           );
  newsCount_->setText(QString("%1 (%2 %3, %4 %5)").
                      arg(feedProperties.status.undeleteCount).
                      arg(feedProperties.status.newCount).
                      arg(tr("new")).
                      arg(feedProperties.status.unreadCount).
                      arg(tr("unread")));
  feedsCount_->setText(QString("%1").arg(feedProperties.status.feedsCount));
}
//------------------------------------------------------------------------------
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
  foreach (QByteArray imageFormat, QImageReader::supportedImageFormats()) {
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
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::slotCurrentColumnChanged(QTreeWidgetItem *current,
                                                    QTreeWidgetItem *)
{
  if (columnsTree_->indexOfTopLevelItem(current) == 0)
    moveUpButton_->setEnabled(false);
  else moveUpButton_->setEnabled(true);

  if (columnsTree_->indexOfTopLevelItem(current) == (columnsTree_->topLevelItemCount()-1))
    moveDownButton_->setEnabled(false);
  else moveDownButton_->setEnabled(true);

  if (columnsTree_->indexOfTopLevelItem(current) < 0) {
    removeButton_->setEnabled(false);
    moveUpButton_->setEnabled(false);
    moveDownButton_->setEnabled(false);
  } else {
    removeButton_->setEnabled(true);
  }
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
void FeedPropertiesDialog::removeColumn()
{
  int row = columnsTree_->currentIndex().row();
  columnsTree_->takeTopLevelItem(row);

  if (columnsTree_->currentIndex().row() == 0)
    moveUpButton_->setEnabled(false);
  if (columnsTree_->currentIndex().row() == (columnsTree_->topLevelItemCount()-1))
    moveDownButton_->setEnabled(false);
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::moveUpColumn()
{
  int row = columnsTree_->currentIndex().row();
  QTreeWidgetItem *treeWidgetItem = columnsTree_->takeTopLevelItem(row-1);
  columnsTree_->insertTopLevelItem(row, treeWidgetItem);

  if (columnsTree_->currentIndex().row() == 0)
    moveUpButton_->setEnabled(false);
  if (columnsTree_->currentIndex().row() != (columnsTree_->topLevelItemCount()-1))
    moveDownButton_->setEnabled(true);
}
//------------------------------------------------------------------------------
void FeedPropertiesDialog::moveDownColumn()
{
  int row = columnsTree_->currentIndex().row();
  QTreeWidgetItem *treeWidgetItem = columnsTree_->takeTopLevelItem(row+1);
  columnsTree_->insertTopLevelItem(row, treeWidgetItem);

  if (columnsTree_->currentIndex().row() != 0)
    moveUpButton_->setEnabled(true);
  if (columnsTree_->currentIndex().row() == (columnsTree_->topLevelItemCount()-1))
    moveDownButton_->setEnabled(false);
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
  buttonBox->button(QDialogButtonBox::Apply)->setEnabled(count > 0 && bulkAction_->currentIndex() > 0);
}

void FeedPropertiesDialog::bulkApplySucceeded(int count)
{
  bulkResult_->setText(tr("%1 applied to %2 feeds.").arg(bulkActionName()).arg(count));
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
    changes.rightToLeft = layoutDirection_->isChecked();
    break;
  case 5:
    changes.columns = columnSettings();
    break;
  default:
    break;
  }
  return changes;
}

QGroupBox *FeedPropertiesDialog::createUpdateSchedule()
{
  QGroupBox *updateSchedule = new QGroupBox(tr("Update schedule"));
  useGlobalUpdate_ = new QRadioButton(updateSchedule);
  updateEnable_ = new QRadioButton(tr("Update every"), updateSchedule);
  noScheduledUpdates_ = new QRadioButton(tr("No scheduled updates"), updateSchedule);
  noScheduledUpdates_->setToolTip(tr("Startup updates and manual updates, including Update All, are still allowed."));
  updateInterval_ = new QSpinBox();
  updateInterval_->setEnabled(false);
  updateInterval_->setRange(1, 9999);
  connect(updateEnable_, SIGNAL(toggled(bool)),
          updateInterval_, SLOT(setEnabled(bool)));

  updateIntervalType_ = new QComboBox(this);
  updateIntervalType_->setEnabled(false);
  QStringList intervalTypeList;
  intervalTypeList << tr("seconds") << tr("minutes")  << tr("hours");
  updateIntervalType_->addItems(intervalTypeList);
  connect(updateEnable_, SIGNAL(toggled(bool)),
          updateIntervalType_, SLOT(setEnabled(bool)));

  QHBoxLayout *updateFeedsLayout = new QHBoxLayout();
  updateFeedsLayout->setContentsMargins(0, 0, 0, 0);
  updateFeedsLayout->addWidget(updateEnable_);
  updateFeedsLayout->addWidget(updateInterval_);
  updateFeedsLayout->addWidget(updateIntervalType_);
  updateFeedsLayout->addStretch();

  QVBoxLayout *scheduleLayout = new QVBoxLayout(updateSchedule);
  scheduleLayout->addWidget(useGlobalUpdate_);
  scheduleLayout->addLayout(updateFeedsLayout);
  scheduleLayout->addWidget(noScheduledUpdates_);
  return updateSchedule;
}

QWidget *FeedPropertiesDialog::createImageEditor()
{
  QWidget *imageEditor = new QWidget();
  auto *imageLayout = new QHBoxLayout(imageEditor);
  imageLayout->setContentsMargins(0, 0, 0, 0);
  imageLayout->addWidget(new QLabel(tr("Load images:")));
  imagePolicy_ = new QComboBox();
  imagePolicy_->addItems({tr("Never"), tr("Use global settings"), tr("Always")});
  imageLayout->addWidget(imagePolicy_);
  imageLayout->addStretch();

  return imageEditor;
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

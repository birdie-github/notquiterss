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
#include "common.h"
#include <QTimeZone>
#include "newstabwidget.h"

#include "mainapplication.h"
#include "settings.h"
#include "shareservice.h"
#include "articlecontent.h"
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace {
void makeRtlAlignmentAbsolute(QTextDocument *document)
{
  for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
    QTextBlockFormat format = block.blockFormat();
    if (format.alignment() & Qt::AlignRight) {
      format.setAlignment(format.alignment() | Qt::AlignAbsolute);
      QTextCursor(block).setBlockFormat(format);
    }
  }
}
}

NewsTabWidget::NewsTabWidget(QWidget *parent, TabType type, int feedId, int feedParId)
  : QWidget(parent)
  , type_(type)
  , feedId_(feedId)
  , feedParId_(feedParId)
  , currentNewsIdOld(-1)
  , autoLoadImages_(true)
{
  mainWindow_ = mainApp->mainWindow();
  db_ = QSqlDatabase::database();
  feedsView_ = mainWindow_->feedsView_;
  feedsModel_ = mainWindow_->feedsModel_;
  feedsProxyModel_ = mainWindow_->feedsProxyModel_;

  newsIconTitle_ = new QLabel();
  newsIconMovie_ = new QMovie(":/images/loading");
  newsIconTitle_->setMovie(newsIconMovie_);
  newsTextTitle_ = new QLabel();
  newsTextTitle_->setTextFormat(Qt::PlainText);
  newsTextTitle_->setObjectName("newsTextTitle_");

  closeButton_ = new QToolButton();
  closeButton_->setFixedSize(15, 15);
  closeButton_->setCursor(Qt::ArrowCursor);
  closeButton_->setStyleSheet(
        "QToolButton { background-color: transparent;"
        "border: none; padding: 0px;"
        "image: url(:/images/close); }"
        "QToolButton:hover {"
        "image: url(:/images/closeHover); }"
        );
  connect(closeButton_, SIGNAL(clicked()),
          this, SLOT(slotTabClose()));

  QHBoxLayout *newsTitleLayout = new QHBoxLayout();
  // Layout margins use logical pixels; Qt applies the display scale factor.
  newsTitleLayout->setContentsMargins(4, 0, 4, 0);
  newsTitleLayout->setSpacing(0);
  newsTitleLayout->addWidget(newsIconTitle_);
  newsTitleLayout->addSpacing(3);
  newsTitleLayout->addWidget(newsTextTitle_, 1);
  newsTitleLayout->addWidget(closeButton_);

  newsTitleLabel_ = new QWidget();
  newsTitleLabel_->setObjectName("newsTitleLabel_");
  newsTitleLabel_->setMinimumHeight(16);
  newsTitleLabel_->setLayout(newsTitleLayout);
  newsTitleLabel_->setVisible(false);

  bool showCloseButtonTab = AppSettings::showCloseButtonTab.get();
  if (!showCloseButtonTab) {
    closeButton_->hide();
    newsTitleLabel_->setFixedWidth(MAX_TAB_WIDTH-15);
  } else {
    newsTitleLabel_->setFixedWidth(MAX_TAB_WIDTH);
  }

  if (type_ != TabTypeDownloads) {
    createNewsList();
    createArticleWidget();

    if (type_ != TabTypeDownloads) {
      newsTabWidgetSplitter_ = new QSplitter(this);
      newsTabWidgetSplitter_->setObjectName("newsTabWidgetSplitter");

      if ((mainWindow_->browserPosition_ == TOP_POSITION) ||
          (mainWindow_->browserPosition_ == LEFT_POSITION)) {
        newsTabWidgetSplitter_->addWidget(webWidget_);
        newsTabWidgetSplitter_->addWidget(newsWidget_);
      } else {
        newsTabWidgetSplitter_->addWidget(newsWidget_);
        newsTabWidgetSplitter_->addWidget(webWidget_);
      }
    }
  }

  QVBoxLayout *layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  if (type_ == TabTypeDownloads)
    layout->addWidget(mainApp->downloadManager());
  else
    layout->addWidget(newsTabWidgetSplitter_);
  setLayout(layout);

  if (type_ < TabTypeDownloads) {
    newsTabWidgetSplitter_->setHandleWidth(1);

    if ((mainWindow_->browserPosition_ == RIGHT_POSITION) ||
        (mainWindow_->browserPosition_ == LEFT_POSITION)) {
      newsTabWidgetSplitter_->setOrientation(Qt::Horizontal);
      newsTabWidgetSplitter_->setStyleSheet(
            QString("QSplitter::handle {background: qlineargradient("
                    "x1: 0, y1: 0, x2: 0, y2: 1,"
                    "stop: 0 %1, stop: 0.07 %2);}").
            arg(newsPanelWidget_->palette().window().color().name()).
            arg(qApp->palette().color(QPalette::Dark).name()));
    } else {
      newsTabWidgetSplitter_->setOrientation(Qt::Vertical);
      newsTabWidgetSplitter_->setStyleSheet(
            QString("QSplitter::handle {background: %1; margin-top: 1px; margin-bottom: 1px;}").
            arg(qApp->palette().color(QPalette::Dark).name()));
    }
  }

  connect(this, SIGNAL(signalSetTextTab(QString,NewsTabWidget*)),
          mainWindow_, SLOT(setTextTitle(QString,NewsTabWidget*)));

  if (type_ < TabTypeDownloads) {
    connect(newsView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
              updateActionStates();
            });
    connect(newsModel_, &QAbstractItemModel::modelReset,
            this, &NewsTabWidget::updateActionStates);
    connect(newsModel_, &QAbstractItemModel::rowsInserted,
            this, [this](const QModelIndex &, int, int) { updateActionStates(); });
    connect(newsModel_, &QAbstractItemModel::rowsRemoved,
            this, [this](const QModelIndex &, int, int) { updateActionStates(); });
    connect(articleView_, &ArticleView::articleContentChanged,
            this, &NewsTabWidget::updateActionStates);
  }
}

NewsTabWidget::~NewsTabWidget()
{
  if (type_ == TabTypeDownloads) {
    mainApp->downloadManager()->hide();
    mainApp->downloadManager()->setParent(mainWindow_);
  }
}

void NewsTabWidget::disconnectObjects()
{
  disconnect(this);
  if (type_ != TabTypeDownloads) {
    articleView_->disconnect(this);
    articleView_->disconnectObjects();
  }
}

/** @brief Create news list with all related panels
 *----------------------------------------------------------------------------*/
void NewsTabWidget::createNewsList()
{
  newsView_ = new NewsView(this);
  newsView_->setFrameStyle(QFrame::NoFrame);
  newsModel_ = new NewsModel(this, newsView_);
  newsModel_->setTable("news");
  newsModel_->setFilter("feedId=-1");
  newsHeader_ = new NewsHeader(newsModel_, newsView_);

  newsView_->setModel(newsModel_);
  newsView_->setHeader(newsHeader_);

  newsHeader_->init();

  newsToolBar_ = new QToolBar(this);
  newsToolBar_->setObjectName("newsToolBar");
  newsToolBar_->setStyleSheet("QToolBar { border: none; padding: 0px; }");

  Settings settings;
  QString actionListStr = "markNewsRead,markAllNewsRead,Separator,markStarAct,"
                          "newsLabelAction,shareMenuAct,openInExternalBrowserAct,Separator,"
                          "nextUnreadNewsAct,prevUnreadNewsAct,Separator,"
                          "newsFilter,Separator,deleteNewsAct";
  QString str = settings.value("Settings/newsToolBar", actionListStr).toString();

  foreach (QString actionStr, str.split(",", Qt::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      newsToolBar_->addSeparator();
    } else {
      QListIterator<QAction *> iter(mainWindow_->actions());
      while (iter.hasNext()) {
        QAction *pAction = iter.next();
        if (!pAction->icon().isNull()) {
          if (pAction->objectName() == actionStr) {
            newsToolBar_->addAction(pAction);
            break;
          }
        }
      }
    }
  }
  separatorRAct_ = newsToolBar_->addSeparator();
  separatorRAct_->setObjectName("separatorRAct");
  newsToolBar_->addAction(mainWindow_->restoreNewsAct_);

  findText_ = new FindTextContent(this);
  findText_->setFixedWidth(200);

  QHBoxLayout *newsPanelLayout = new QHBoxLayout();
  newsPanelLayout->setContentsMargins(2, 2, 2, 2);
  newsPanelLayout->setSpacing(2);
  newsPanelLayout->addWidget(newsToolBar_);
  newsPanelLayout->addStretch(1);
  newsPanelLayout->addWidget(findText_);

  newsPanelWidget_ = new QWidget(this);
  newsPanelWidget_->setObjectName("newsPanelWidget_");
  newsPanelWidget_->setStyleSheet(
        QString("#newsPanelWidget_ {border-bottom: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));

  newsPanelWidget_->setLayout(newsPanelLayout);
  if (!mainWindow_->newsToolbarToggle_->isChecked())
    newsPanelWidget_->hide();

  QVBoxLayout *newsLayout = new QVBoxLayout();
  newsLayout->setContentsMargins(0, 0, 0, 0);
  newsLayout->setSpacing(0);
  newsLayout->addWidget(newsPanelWidget_);
  newsLayout->addWidget(newsView_);

  newsWidget_ = new QWidget(this);
  newsWidget_->setLayout(newsLayout);

  markNewsReadTimer_ = new QTimer(this);

  newspaperHeadHtml_ = Common::readAllFileContents(":/html/newspaper_head");
  newspaperHtml_ = Common::readAllFileContents(":/html/newspaper_description");
  newspaperHtmlRtl_ = Common::readAllFileContents(":/html/newspaper_description_rtl");
  htmlString_ = Common::readAllFileContents(":/html/description");
  htmlRtlString_ = Common::readAllFileContents(":/html/description_rtl");

  connect(newsView_, SIGNAL(pressed(QModelIndex)),
          this, SLOT(slotNewsViewClicked(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyUp(QModelIndex)),
          this, SLOT(slotNewsUpPressed(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyDown(QModelIndex)),
          this, SLOT(slotNewsDownPressed(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyHome(QModelIndex)),
          this, SLOT(slotNewsHomePressed(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyEnd(QModelIndex)),
          this, SLOT(slotNewsEndPressed(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyPageUp(QModelIndex)),
          this, SLOT(slotNewsPageUpPressed(QModelIndex)));
  connect(newsView_, SIGNAL(pressKeyPageDown(QModelIndex)),
          this, SLOT(slotNewsPageDownPressed(QModelIndex)));
  connect(newsView_, SIGNAL(signalSetItemRead(QModelIndex, int)),
          this, SLOT(slotSetItemRead(QModelIndex, int)));
  connect(newsView_, SIGNAL(signalSetItemStar(QModelIndex,int)),
          this, SLOT(slotSetItemStar(QModelIndex,int)));
  connect(newsView_, SIGNAL(signalDoubleClicked(QModelIndex)),
          this, SLOT(slotNewsViewDoubleClicked(QModelIndex)));
  connect(newsView_, SIGNAL(signalMiddleClicked(QModelIndex)),
          this, SLOT(slotNewsMiddleClicked(QModelIndex)));
  connect(newsView_, SIGNAL(signaNewslLabelClicked(QModelIndex)),
          this, SLOT(slotNewslLabelClicked(QModelIndex)));
  connect(markNewsReadTimer_, SIGNAL(timeout()),
          this, SLOT(slotMarkReadTimeout()));
  connect(newsView_, SIGNAL(customContextMenuRequested(QPoint)),
          this, SLOT(showContextMenuNews(const QPoint &)));

  connect(newsModel_, SIGNAL(signalSort(int,int)),
          this, SLOT(slotSort(int,int)));

  connect(findText_, SIGNAL(textChanged(QString)),
          this, SLOT(slotFindText(QString)));
  connect(findText_, SIGNAL(signalSelectFind()),
          this, SLOT(slotSelectFind()));
  connect(findText_, SIGNAL(returnPressed()),
          this, SLOT(slotSelectFind()));
  connect(findText_, SIGNAL(signalVisible(bool)),
          mainWindow_, SLOT(findText()));

  connect(mainWindow_->newsToolbarToggle_, SIGNAL(toggled(bool)),
          newsPanelWidget_, SLOT(setVisible(bool)));
}

/** @brief Call context menu of selected news in news list
 *----------------------------------------------------------------------------*/
void NewsTabWidget::showContextMenuNews(const QPoint &pos)
{
  if (!newsView_->currentIndex().isValid()) return;

  QMenu menu;
  menu.addAction(mainWindow_->restoreNewsAct_);
  menu.addSeparator();
  menu.addAction(mainWindow_->openInExternalBrowserAct_);
  menu.addSeparator();
  menu.addAction(mainWindow_->markNewsRead_);
  menu.addAction(mainWindow_->markAllNewsRead_);
  menu.addSeparator();
  menu.addAction(mainWindow_->markStarAct_);
  menu.addAction(mainWindow_->newsLabelMenuAction_);
  menu.addAction(mainWindow_->shareMenuAct_);
  menu.addAction(mainWindow_->copyLinkAct_);
  menu.addSeparator();
  menu.addAction(mainWindow_->updateFeedAct_);
  menu.addSeparator();
  menu.addAction(mainWindow_->deleteNewsAct_);
  menu.addAction(mainWindow_->deleteAllNewsAct_);

  menu.exec(newsView_->viewport()->mapToGlobal(pos));
}

/** @brief Create web-widget and control panel
 *----------------------------------------------------------------------------*/
void NewsTabWidget::createArticleWidget()
{
  articleView_ = new ArticleView(this);
  webViewProgress_ = new QProgressBar(this);
  webViewProgress_->setRange(0, 100);
  webViewProgress_->setFixedHeight(15);
  webViewProgress_->hide();
  QVBoxLayout *layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(articleView_, 1);
  layout->addWidget(webViewProgress_);
  webWidget_ = new QWidget(this);
  webWidget_->setLayout(layout);
  webWidget_->setMinimumSize(100, 100);
  connect(articleView_, SIGNAL(loadStarted()), this, SLOT(slotLoadStarted()));
  connect(articleView_, SIGNAL(loadFinished(bool)), this, SLOT(slotLoadFinished(bool)));
  connect(articleView_, SIGNAL(linkClicked(QUrl)), this, SLOT(slotLinkClicked(QUrl)));
  connect(articleView_, SIGNAL(linkHovered(QString)),
          this, SLOT(slotLinkHovered(QString)));
  connect(articleView_, SIGNAL(loadProgress(int)), webViewProgress_, SLOT(setValue(int)));
  connect(articleView_, SIGNAL(showContextMenu(QPoint)), this, SLOT(showArticleContextMenu(QPoint)));
  connect(mainWindow_->autoLoadImagesToggle_, SIGNAL(triggered()), this, SLOT(setAutoLoadImages()));
}

/** @brief Reapply all visual state derived from the active theme/settings.
 *----------------------------------------------------------------------------*/
void NewsTabWidget::refreshAppearance(bool reloadArticle)
{
  if (type_ == TabTypeDownloads) return;

  const QColor background = qApp->palette().color(QPalette::Base);
  const QString dark = qApp->palette().color(QPalette::Dark).name();
  newsIconMovie_->setFileName(background.lightness() < 128
      ? ":/images/loading_dark" : ":/images/loading");

  newsPanelWidget_->setStyleSheet(
        QString("#newsPanelWidget_ {border-bottom: 1px solid %1;}").arg(dark));
  if ((mainWindow_->browserPosition_ == RIGHT_POSITION) ||
      (mainWindow_->browserPosition_ == LEFT_POSITION)) {
    newsTabWidgetSplitter_->setStyleSheet(
          QString("QSplitter::handle {background: qlineargradient("
                  "x1: 0, y1: 0, x2: 0, y2: 1,"
                  "stop: 0 %1, stop: 0.07 %2);}").
          arg(newsPanelWidget_->palette().window().color().name()).
          arg(dark));
  } else {
    newsTabWidgetSplitter_->setStyleSheet(
          QString("QSplitter::handle {background: %1; margin-top: 1px; margin-bottom: 1px;}").
          arg(dark));
  }

  newsView_->setFont(QFont(mainWindow_->newsListFontFamily_, mainWindow_->newsListFontSize_));
  newsModel_->formatDate_ = mainWindow_->formatDate_;
  newsModel_->formatTime_ = mainWindow_->formatTime_;
  newsModel_->simplifiedDateTime_ = mainWindow_->simplifiedDateTime_;
  newsModel_->textColor_ = mainWindow_->newsListTextColor_;
  newsView_->setStyleSheet(mainWindow_->newsListBackgroundColor_.isEmpty()
      ? QString() : QString("#newsView_ {background: %1;}").arg(mainWindow_->newsListBackgroundColor_));
  newsModel_->newNewsTextColor_ = mainWindow_->newNewsTextColor_;
  newsModel_->unreadNewsTextColor_ = mainWindow_->unreadNewsTextColor_;
  newsModel_->focusedNewsTextColor_ = mainWindow_->focusedNewsTextColor_;
  newsModel_->focusedNewsBGColor_ = mainWindow_->focusedNewsBGColor_;

  Settings settings;
  const QString styleSheetNews = settings.value(
        "Settings/styleSheetNews", mainApp->styleSheetNewsDefaultFile()).toString();
  QFile file(styleSheetNews);
  const QByteArray styleData = file.open(QFile::ReadOnly)
      ? file.readAll() : Common::readAllFileByteContents(":/style/newsStyle");
  cssString_ = QString::fromUtf8(styleData).
      arg(mainWindow_->newsTextFontFamily_).
      arg(mainWindow_->newsTextFontSize_).
      arg(mainWindow_->newsTitleFontFamily_).
      arg(mainWindow_->newsTitleFontSize_).
      arg(0).
      arg(qApp->palette().color(QPalette::Dark).name()).
      arg(mainWindow_->newsBackgroundColor_).
      arg(mainWindow_->newsTitleBackgroundColor_).
      arg(mainWindow_->linkColor_).
      arg(mainWindow_->titleColor_).
      arg(mainWindow_->dateColor_).
      arg(mainWindow_->authorColor_).
      arg(mainWindow_->newsTextColor_);
  file.close();
  // Older installed/custom article styles may still contain this selector.
  // Qt's rich-text CSS parser stops at :not(), losing subsequent rules.
  cssString_.remove(QRegularExpression("img:not\\(\\.quiterss-img\\)\\s*\\{[^}]*\\}"));
  cssString_.replace(QRegularExpression("\\.title\\s*\\{"), ".title, .title a {");

  articleView_->setFont(QFont(mainWindow_->newsTextFontFamily_, mainWindow_->newsTextFontSize_));
  QPalette articlePalette = qApp->palette();
  articlePalette.setColor(QPalette::Base, QColor(mainWindow_->newsBackgroundColor_));
  articlePalette.setColor(QPalette::Text, QColor(mainWindow_->newsTextColor_));
  articleView_->setPalette(articlePalette);

  newsView_->setAlternatingRowColors(mainWindow_->alternatingRowColorsNews_);
  QPalette newsPalette = qApp->palette();
  newsPalette.setColor(QPalette::AlternateBase, mainWindow_->alternatingRowColors_);
  newsView_->setPalette(newsPalette);
  newsView_->viewport()->update();

  if (!reloadArticle) return;
  if (mainWindow_->newsLayout_ == 1)
    loadNewspaper(RefreshWithPos);
  else if (newsView_->currentIndex().isValid())
    updateArticleView(newsView_->currentIndex(), true);
}

/** @brief Read settings from ini-file
 *----------------------------------------------------------------------------*/
void NewsTabWidget::setSettings(bool init, bool newTab)
{
  Settings settings;

  if (type_ == TabTypeDownloads) return;

  if (newTab) {
    newsTabWidgetSplitter_->restoreState(settings.value("NewsTabSplitterState").toByteArray());
    const QString iconStr = AppSettings::newsToolBarIconSize.get();
    mainWindow_->setToolBarIconSize(newsToolBar_, iconStr);
  }

  QModelIndex feedIndex = feedsModel_->indexById(feedId_);

  if (init) {
    if (type_ == TabTypeFeed) {
      const int displayEmbeddedImages =
          feedsModel_->dataField(feedIndex, "displayEmbeddedImages").toInt();
      if (displayEmbeddedImages == 2)
        autoLoadImages_ = true;
      else if (displayEmbeddedImages == 1)
        autoLoadImages_ = mainWindow_->autoLoadImages_;
      else
        autoLoadImages_ = false;
    } else {
      autoLoadImages_ = mainWindow_->autoLoadImages_;
    }
    articleView_->setZoomFactor(qreal(mainWindow_->defaultZoomPages_)/100.0);
  }

  refreshAppearance(!init);
  setAutoLoadImages(false);

  if (!newTab)
    newsModel_->setFilter("feedId=-1");
  newsHeader_->setColumns(feedIndex);
  mainWindow_->slotUpdateStatus(feedId_, false);
  mainWindow_->newsFilter_->setEnabled(type_ == TabTypeFeed);
  separatorRAct_->setVisible(type_ == TabTypeDel);
  mainWindow_->restoreNewsAct_->setVisible(type_ == TabTypeDel);

  switch (mainWindow_->newsLayout_) {
  case 1:
    newsWidget_->setVisible(false);
    break;
  default:
    newsWidget_->setVisible(true);
  }
}

/** @brief Reload translation
 *----------------------------------------------------------------------------*/
void NewsTabWidget::retranslateStrings() {
  if (type_ != TabTypeDownloads) {
    webViewProgress_->setFormat(tr("Loading... (%p%)"));

    if (type_ != TabTypeDownloads) {
      findText_->retranslateStrings();
      newsHeader_->retranslateStrings();
    }

    setAutoLoadImages(false);
  }

  closeButton_->setToolTip(tr("Close Tab"));
}

void NewsTabWidget::setAutoLoadImages(bool apply)
{
  if (type_ == TabTypeDownloads || mainWindow_->currentNewsTab != this) return;
  if (apply) autoLoadImages_ = !autoLoadImages_;
  mainWindow_->autoLoadImagesToggle_->setText(autoLoadImages_ ? tr("Hide Images") : tr("Load Images"));
  mainWindow_->autoLoadImagesToggle_->setToolTip(autoLoadImages_ ?
      tr("Hide images in this article pane") : tr("Load images in this article pane"));
  mainWindow_->autoLoadImagesToggle_->setIcon(QIcon(autoLoadImages_ ? ":/images/imagesOn" : ":/images/imagesOff"));
  if (apply) {
    if (mainWindow_->newsLayout_ == 1) loadNewspaper(RefreshWithPos);
    else updateArticleView(newsView_->currentIndex());
  }
}

/** @brief Process mouse click in news list
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsViewClicked(QModelIndex index)
{
  slotNewsViewSelected(index);
}

// ----------------------------------------------------------------------------
void NewsTabWidget::slotNewsViewSelected(QModelIndex index, bool clicked)
{
  if (mainWindow_->newsLayout_ == 1) return;

  int newsId = newsModel_->dataField(index.row(), "id").toInt();
  if (mainWindow_->markNewsReadOn_ && mainWindow_->markPrevNewsRead_ &&
      (newsId != currentNewsIdOld)) {
    QModelIndex startIndex = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(startIndex, Qt::EditRole, currentNewsIdOld);
    if (!indexList.isEmpty()) {
      slotSetItemRead(indexList.first(), 1);
    }
  }

  if (!index.isValid()) {
    hideArticleContent();
    currentNewsIdOld = newsId;
    return;
  }

  if (!((newsId == currentNewsIdOld) &&
        newsModel_->dataField(index.row(), "read").toInt() >= 1) ||
      clicked) {
    markNewsReadTimer_->stop();
    if (mainWindow_->markNewsReadOn_ && mainWindow_->markCurNewsRead_) {
      if (mainWindow_->markNewsReadTime_ == 0) {
        slotSetItemRead(index, 1);
      } else {
        markNewsReadTimer_->start(mainWindow_->markNewsReadTime_*1000);
      }
    }

    if (type_ == TabTypeFeed) {
      // Write current news to feed
      QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").
          arg(newsId).arg(feedId_);
      mainApp->sqlQueryExec(qStr);

      QModelIndex feedIndex = feedsModel_->indexById(feedId_);
      feedsModel_->setData(feedsModel_->indexSibling(feedIndex, "currentNews"), newsId);
    } else if (type_ == TabTypeLabel) {
      QString qStr = QString("UPDATE labels SET currentNews='%1' WHERE id=='%2'").
          arg(newsId).
          arg(mainWindow_->categoriesTree_->currentItem()->text(2).toInt());
      mainApp->sqlQueryExec(qStr);

      mainWindow_->categoriesTree_->currentItem()->setText(3, QString::number(newsId));
    }

    updateArticleView(index);
    mainWindow_->statusBar()->showMessage(linkNewsString_, 3000);
  }
  currentNewsIdOld = newsId;
}

// ----------------------------------------------------------------------------
void NewsTabWidget::slotNewsViewDoubleClicked(QModelIndex index)
{
  Q_UNUSED(index)
  openInExternalBrowserNews();
}

// ----------------------------------------------------------------------------
void NewsTabWidget::slotNewsMiddleClicked(QModelIndex index)
{
  Q_UNUSED(index)
  openInExternalBrowserNews();
}

/** @brief Process pressing UP-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsUpPressed(QModelIndex index)
{
  if (type_ >= TabTypeDownloads) return;

  int row;
  if (!index.isValid()) {
    if (!newsView_->currentIndex().isValid())
      row = 0;
    else
      row = newsView_->currentIndex().row() - 1;
    if (row < 0)
      return;
    index = newsModel_->index(row, newsModel_->fieldIndex("title"));
    newsView_->setCurrentIndex(index);
  } else {
    row = index.row();
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (row < (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(row - pageStep/2);

  slotNewsViewSelected(index);
}

/** @brief Process pressing DOWN-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsDownPressed(QModelIndex index)
{
  if (type_ >= TabTypeDownloads) return;

  int row;
  if (!index.isValid()) {
    if (!newsView_->currentIndex().isValid())
      row = 0;
    else
      row = newsView_->currentIndex().row() + 1;
    if (row >= newsModel_->rowCount())
      return;
    index = newsModel_->index(row, newsModel_->fieldIndex("title"));
    newsView_->setCurrentIndex(index);
  } else {
    row = index.row();
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (row > (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(row - pageStep/2);
  slotNewsViewSelected(index);
}

/** @brief Process pressing HOME-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsHomePressed(QModelIndex index)
{
  slotNewsViewSelected(index);
}

/** @brief Process pressing END-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsEndPressed(QModelIndex index)
{
  slotNewsViewSelected(index);
}

/** @brief Process pressing PageUp-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsPageUpPressed(QModelIndex index)
{
  int row;
  if (!index.isValid()) {
    if (!newsView_->currentIndex().isValid())
      row = 0;
    else
      row = newsView_->currentIndex().row() - newsView_->verticalScrollBar()->pageStep();
    if (row < 0)
      row = 0;
    index = newsModel_->index(row, newsModel_->fieldIndex("title"));
    newsView_->setCurrentIndex(index);
  }

  slotNewsViewSelected(index);
}

/** @brief Process pressing PageDown-key
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotNewsPageDownPressed(QModelIndex index)
{
  int row;
  if (!index.isValid()) {
    if (!newsView_->currentIndex().isValid())
      row = 0;
    else
      row = newsView_->currentIndex().row() + newsView_->verticalScrollBar()->pageStep();
    if (row >= newsModel_->rowCount())
      row = newsModel_->rowCount()-1;
    index = newsModel_->index(row, newsModel_->fieldIndex("title"));
    newsView_->setCurrentIndex(index);
  }

  slotNewsViewSelected(index);
}

/** @brief Mark news Read
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotSetItemRead(QModelIndex index, int read)
{
  markNewsReadTimer_->stop();
  if (!index.isValid() || (newsModel_->rowCount() == 0)) return;

  bool changed = false;
  int newsId = newsModel_->dataField(index.row(), "id").toInt();

  if (read == 1) {
    if (newsModel_->dataField(index.row(), "new").toInt() == 1) {
      newsModel_->setData(
            newsModel_->index(index.row(), newsModel_->fieldIndex("new")),
            0);
      mainApp->sqlQueryExec(QString("UPDATE news SET new=0 WHERE id=='%1'").arg(newsId));
    }
    if (newsModel_->dataField(index.row(), "read").toInt() == 0) {
      newsModel_->setData(
            newsModel_->index(index.row(), newsModel_->fieldIndex("read")),
            1);
      mainApp->sqlQueryExec(QString("UPDATE news SET read=1 WHERE id=='%1'").arg(newsId));
      changed = true;
    }
  } else {
    if (newsModel_->dataField(index.row(), "read").toInt() != 0) {
      newsModel_->setData(
            newsModel_->index(index.row(), newsModel_->fieldIndex("read")),
            0);
      mainApp->sqlQueryExec(QString("UPDATE news SET read=0 WHERE id=='%1'").arg(newsId));
      changed = true;
    }
  }

  if (changed) {
    newsView_->viewport()->update();
    int feedId = newsModel_->dataField(index.row(), "feedId").toInt();
    mainWindow_->slotUpdateStatus(feedId);
    mainWindow_->recountCategoryCounts();
  }
}

/** @brief Mark news Star
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotSetItemStar(QModelIndex index, int starred)
{
  if (!index.isValid()) return;

  newsModel_->setData(index, starred);

  int newsId = newsModel_->dataField(index.row(), "id").toInt();
  mainApp->sqlQueryExec(QString("UPDATE news SET starred='%1' WHERE id=='%2'").
                        arg(starred).arg(newsId));
  mainWindow_->recountCategoryCounts();
}

void NewsTabWidget::slotMarkReadTimeout()
{
  slotSetItemRead(newsView_->currentIndex(), 1);
}

/** @brief Mark selected news Read
 *----------------------------------------------------------------------------*/
void NewsTabWidget::markNewsRead()
{
  if (type_ >= TabTypeDownloads) return;
  markNewsReadTimer_->stop();

  QModelIndex curIndex;
  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(0);

  int cnt = indexes.count();
  if (cnt == 0) return;

  if (cnt == 1) {
    curIndex = indexes.at(0);
    if (newsModel_->dataField(curIndex.row(), "read").toInt() == 0) {
      slotSetItemRead(curIndex, 1);
    } else {
      slotSetItemRead(curIndex, 0);
    }
  } else {
    QStringList feedIdList;

    bool markRead = false;
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);
      if (newsModel_->dataField(curIndex.row(), "read").toInt() == 0) {
        markRead = true;
        break;
      }
    }

    db_.transaction();
    QSqlQuery q;
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);
      newsModel_->setData(
            newsModel_->index(curIndex.row(), newsModel_->fieldIndex("new")),
            0);
      newsModel_->setData(
            newsModel_->index(curIndex.row(), newsModel_->fieldIndex("read")),
            markRead);

      int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
      q.exec(QString("UPDATE news SET new=0, read='%1' WHERE id=='%2'").
             arg(markRead).arg(newsId));
      QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
      if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
    }
    db_.commit();

    foreach (QString feedId, feedIdList) {
      mainWindow_->slotUpdateStatus(feedId.toInt());
    }
    mainWindow_->recountCategoryCounts();
    newsView_->viewport()->update();
  }
  loadNewspaper(RefreshWithPos);
}

/** @brief Mark all news of the feed Read
 *----------------------------------------------------------------------------*/
void NewsTabWidget::markAllNewsRead()
{
  if (type_ >= TabTypeDownloads) return;
  markNewsReadTimer_->stop();

  int cnt = newsModel_->rowCount();
  if (cnt == 0) return;

  QStringList feedIdList;

  db_.transaction();
  QSqlQuery q;
  for (int i = cnt-1; i >= 0; --i) {
    int newsId = newsModel_->dataField(i, "id").toInt();
    q.exec(QString("UPDATE news SET read=1 WHERE id=='%1' AND read=0").arg(newsId));
    q.exec(QString("UPDATE news SET new=0 WHERE id=='%1' AND new=1").arg(newsId));

    QString feedId = newsModel_->dataField(i, "feedId").toString();
    if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
  }
  db_.commit();

  int currentRow = newsView_->currentIndex().row();

  newsModel_->select();

  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  loadNewspaper(RefreshWithPos);

  newsView_->setCurrentIndex(newsModel_->index(currentRow, newsModel_->fieldIndex("title")));

  foreach (QString feedId, feedIdList) {
    mainWindow_->slotUpdateStatus(feedId.toInt());
  }
  mainWindow_->recountCategoryCounts();
}

/** @brief Mark selected news Starred
 *----------------------------------------------------------------------------*/
void NewsTabWidget::markNewsStar()
{
  if (type_ >= TabTypeDownloads) return;

  QModelIndex curIndex;
  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(
        newsModel_->fieldIndex("starred"));

  int cnt = indexes.count();
  if (cnt == 0) return;

  if (cnt == 1) {
    curIndex = indexes.at(0);
    if (curIndex.data(Qt::EditRole).toInt() == 0) {
      slotSetItemStar(curIndex, 1);
    } else {
      slotSetItemStar(curIndex, 0);
    }
  } else {
    bool markStar = false;
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);
      if (curIndex.data(Qt::EditRole).toInt() == 0) {
        markStar = true;
        break;
      }
    }

    db_.transaction();
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);
      newsModel_->setData(curIndex, markStar);

      int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
      QSqlQuery q;
      q.exec(QString("UPDATE news SET starred='%1' WHERE id=='%2'").
             arg(markStar).arg(newsId));
    }
    db_.commit();

    mainWindow_->recountCategoryCounts();
  }
  loadNewspaper(RefreshWithPos);
}

/** @brief Delete selected news
 *----------------------------------------------------------------------------*/
void NewsTabWidget::deleteNews()
{
  if (type_ >= TabTypeDownloads) return;

  QModelIndex curIndex;
  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(newsModel_->fieldIndex("deleted"));

  int cnt = indexes.count();
  if (cnt == 0) return;

  QStringList feedIdList;

  if (type_ != TabTypeDel) {
    if (cnt == 1) {
      curIndex = indexes.at(0);
      if (newsModel_->dataField(curIndex.row(), "starred").toInt() &&
          mainWindow_->notDeleteStarred_)
        return;
      QString labelStr = newsModel_->dataField(curIndex.row(), "label").toString();
      if (!(labelStr.isEmpty() || (labelStr == ",")) && mainWindow_->notDeleteLabeled_)
        return;

      slotSetItemRead(curIndex, 1);

      newsModel_->setData(curIndex, 1);
      newsModel_->setData(newsModel_->index(curIndex.row(), newsModel_->fieldIndex("deleteDate")),
                          QDateTime::currentDateTime().toString(Qt::ISODate));

      QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
      if (!feedIdList.contains(feedId)) feedIdList.append(feedId);

      newsModel_->submitAll();
    } else {
      db_.transaction();
      QSqlQuery q;
      for (int i = cnt-1; i >= 0; --i) {
        curIndex = indexes.at(i);
        if (newsModel_->dataField(curIndex.row(), "starred").toInt() &&
            mainWindow_->notDeleteStarred_)
          continue;
        QString labelStr = newsModel_->dataField(curIndex.row(), "label").toString();
        if (!(labelStr.isEmpty() || (labelStr == ",")) && mainWindow_->notDeleteLabeled_)
          continue;

        int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
        q.exec(QString("UPDATE news SET new=0, read=2, deleted=1, deleteDate='%1' WHERE id=='%2'").
               arg(QDateTime::currentDateTime().toString(Qt::ISODate)).
               arg(newsId));

        QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
        if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
      }
      db_.commit();

      newsModel_->select();
    }
  }
  else {
    db_.transaction();
    QSqlQuery q;
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);

      int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
      q.exec(QString("UPDATE news SET description='', content='', received='', "
                     "author_name='', author_uri='', author_email='', "
                     "category='', new='', read='', starred='', label='', "
                     "deleteDate='', feedParentId='', deleted=2 WHERE id=='%1'").
             arg(newsId));

      QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
      if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
    }
    db_.commit();

    newsModel_->select();
  }

  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  if (curIndex.row() == newsModel_->rowCount())
    curIndex = newsModel_->index(curIndex.row()-1, newsModel_->fieldIndex("title"));
  else if (curIndex.row() > newsModel_->rowCount())
    curIndex = newsModel_->index(newsModel_->rowCount()-1, newsModel_->fieldIndex("title"));
  else
    curIndex = newsModel_->index(curIndex.row(), newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(curIndex);
  slotNewsViewSelected(curIndex);

  foreach (QString feedId, feedIdList) {
    mainWindow_->slotUpdateStatus(feedId.toInt());
  }
  mainWindow_->recountCategoryCounts();
  loadNewspaper(RefreshWithPos);
}

/** @brief Delete all news of the feed
 *----------------------------------------------------------------------------*/
void NewsTabWidget::deleteAllNewsList()
{
  if (type_ >= TabTypeDownloads) return;

  int cnt = newsModel_->rowCount();
  if (cnt == 0) return;

  QStringList feedIdList;

  db_.transaction();
  QSqlQuery q;
  for (int i = cnt-1; i >= 0; --i) {
    int newsId = newsModel_->dataField(i, "id").toInt();

    if (type_ != TabTypeDel) {
      if (newsModel_->dataField(i, "starred").toInt() &&
          mainWindow_->notDeleteStarred_)
        continue;
      QString labelStr = newsModel_->dataField(i, "label").toString();
      if (!(labelStr.isEmpty() || (labelStr == ",")) && mainWindow_->notDeleteLabeled_)
        continue;

      q.exec(QString("UPDATE news SET new=0, read=2, deleted=1, deleteDate='%1' WHERE id=='%2'").
             arg(QDateTime::currentDateTime().toString(Qt::ISODate)).
             arg(newsId));
    }
    else {
      q.exec(QString("UPDATE news SET description='', content='', received='', "
                     "author_name='', author_uri='', author_email='', "
                     "category='', new='', read='', starred='', label='', "
                     "deleteDate='', feedParentId='', deleted=2 WHERE id=='%1'").
             arg(newsId));
    }

    QString feedId = newsModel_->dataField(i, "feedId").toString();
    if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
  }
  db_.commit();

  newsModel_->select();
  while (newsModel_->canFetchMore()) newsModel_->fetchMore();
  loadNewspaper(RefreshWithPos);

  slotNewsViewSelected(QModelIndex());

  foreach (QString feedId, feedIdList) {
    mainWindow_->slotUpdateStatus(feedId.toInt());
  }
  mainWindow_->recountCategoryCounts();
}

/** @brief Restore deleted news
 *----------------------------------------------------------------------------*/
void NewsTabWidget::restoreNews()
{
  if (type_ >= TabTypeDownloads) return;

  QModelIndex curIndex;
  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(newsModel_->fieldIndex("deleted"));

  int cnt = indexes.count();
  if (cnt == 0) return;

  QStringList feedIdList;

  if (cnt == 1) {
    curIndex = indexes.at(0);
    newsModel_->setData(curIndex, 0);
    newsModel_->setData(newsModel_->index(curIndex.row(), newsModel_->fieldIndex("deleteDate")), "");
    newsModel_->submitAll();

    QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
    if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
  } else {
    db_.transaction();
    QSqlQuery q;
    for (int i = cnt-1; i >= 0; --i) {
      curIndex = indexes.at(i);
      int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
      q.exec(QString("UPDATE news SET deleted=0, deleteDate='' WHERE id=='%1'").
             arg(newsId));

      QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
      if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
    }
    db_.commit();

    newsModel_->select();
  }

  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  loadNewspaper(RefreshWithPos);

  if (curIndex.row() == newsModel_->rowCount())
    curIndex = newsModel_->index(curIndex.row()-1, newsModel_->fieldIndex("title"));
  else if (curIndex.row() > newsModel_->rowCount())
    curIndex = newsModel_->index(newsModel_->rowCount()-1, newsModel_->fieldIndex("title"));
  else
    curIndex = newsModel_->index(curIndex.row(), newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(curIndex);
  slotNewsViewSelected(curIndex);
  mainWindow_->slotUpdateStatus(feedId_);
  mainWindow_->recountCategoryCounts();

  foreach (QString feedId, feedIdList) {
    mainWindow_->slotUpdateStatus(feedId.toInt());
  }
  mainWindow_->recountCategoryCounts();
}

/** @brief Copy news link
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotCopyLinkNews()
{
  if (type_ >= TabTypeDownloads) return;

  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(0);

  int cnt = indexes.count();
  if (cnt == 0) return;

  QString copyStr;
  for (int i = cnt-1; i >= 0; --i) {
    if (!copyStr.isEmpty()) copyStr.append("\n");
    copyStr.append(getLinkNews(indexes.at(i).row()));
  }

  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(copyStr);
}

/** @brief Sort news by Star or Read column
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotSort(int column, int/* order*/)
{
  QString strId;
  if (feedsModel_->isFolder(feedsModel_->indexById(feedId_))) {
    strId = QString("(%1)").arg(mainWindow_->getIdFeedsString(feedId_));
  } else {
    strId = QString("feedId='%1'").arg(feedId_);
  }

  QString qStr;
  if (column == newsModel_->fieldIndex("read")) {
    qStr = QString("UPDATE news SET rights=read WHERE %1").arg(strId);
  }
  else if (column == newsModel_->fieldIndex("starred")) {
    qStr = QString("UPDATE news SET rights=starred WHERE %1").arg(strId);
  }
  else if (column == newsModel_->fieldIndex("rights")) {
    qStr = QString("UPDATE news SET rights = (SELECT text from feeds where id = news.feedId) WHERE %1").arg(strId);
  }

  QSqlQuery q;
  q.exec(qStr);
}

/** @brief Load/Update browser contents
 *----------------------------------------------------------------------------*/
void NewsTabWidget::updateArticleView(QModelIndex index, bool preservePosition)
{
  if (!index.isValid()) {
    hideArticleContent();
    return;
  }

  articleView_->beginArticles();
  QString newsId = newsModel_->dataField(index.row(), "id").toString();
  linkNewsString_ = articleUrl(index.row()).toString();
  QString linkString = ArticleContent::isExternalLink(QUrl(linkNewsString_)) ? linkNewsString_.toHtmlEscaped().replace(QChar(39), "&#39;") : QString();

  QString feedId = newsModel_->dataField(index.row(), "feedId").toString();
  QModelIndex feedIndex = feedsModel_->indexById(feedId.toInt());
  QString htmlStr;
    QString content = newsModel_->dataField(index.row(), "content").toString();
    {

      QString description = newsModel_->dataField(index.row(), "description").toString();
      if (content.isEmpty() || (description.length() > content.length())) {
        content = description;
      }

      QString titleString = newsModel_->dataField(index.row(), "title").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      if (!linkString.isEmpty()) {
        titleString = QString("<a href='%1' class='unread'>%2</a>").
            arg(linkString, titleString);
      }

      QDateTime dtLocal;
      QString dateString = newsModel_->dataField(index.row(), "published").toString();
      if (!dateString.isNull()) {
        QDateTime dtLocalTime = QDateTime::currentDateTime();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), QTimeZone(QTimeZone::UTC));
#else
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
#endif
        int nTimeShift = dtLocalTime.secsTo(dtUTC);

        QDateTime dt = QDateTime::fromString(dateString, Qt::ISODate);
        dtLocal = dt.addSecs(nTimeShift);
      } else {
        dtLocal = QDateTime::fromString(
              newsModel_->dataField(index.row(), "received").toString(),
              Qt::ISODate);
      }
      if (QDateTime::currentDateTime().date() <= dtLocal.date())
        dateString = dtLocal.toString(mainWindow_->formatTime_);
      else
        dateString = dtLocal.toString(mainWindow_->formatDate_ + " " + mainWindow_->formatTime_);

      // Create author panel from news author
      QString authorString;
      QString authorName = newsModel_->dataField(index.row(), "author_name").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      QString authorEmail = newsModel_->dataField(index.row(), "author_email").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      QString authorUri = newsModel_->dataField(index.row(), "author_uri").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

      QRegularExpression reg("(^\\S+@\\S+\\.\\S+)",
          QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
      const QRegularExpressionMatch match = reg.match(authorName);
      int pos = match.capturedStart();
      if (pos > -1) {
        authorName.replace(match.captured(1), QString(" <a href='mailto:%1'>%1</a>").arg(match.captured(1)));
      }

      authorString = authorName;

      if (!authorEmail.isEmpty())
        authorString.append(QString(" <a href='mailto:%1'>e-mail</a>").arg(authorEmail));
      if (!authorUri.isEmpty())
        authorString.append(QString(" <a href='%1'>page</a>"). arg(authorUri));

      // If news author is absent, create author panel from feed author
      // @note(arhohryakov:2012.01.03) Author is got from current feed, because
      //   news is belong to it
      if (authorString.isEmpty()) {
        authorName  = feedsModel_->dataField(feedIndex, "author_name").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
        authorEmail = feedsModel_->dataField(feedIndex, "author_email").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
        authorUri   = feedsModel_->dataField(feedIndex, "author_uri").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

        authorString = authorName;

        if (!authorEmail.isEmpty())
          authorString.append(QString(" <a href='mailto:%1'>e-mail</a>").arg(authorEmail));
        if (!authorUri.isEmpty())
          authorString.append(QString(" <a href='%1'>page</a>").arg(authorUri));
      }

      QString commentsStr;
      QString commentsUrl = newsModel_->dataField(index.row(), "comments").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

      if (!commentsUrl.isEmpty())
      {
        commentsStr = QString("<a href=\"%1\"> %2</a>").arg(commentsUrl, tr("Comments"));
      }

      QString category = newsModel_->dataField(index.row(), "category").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

      if (!authorString.isEmpty())
      {
        authorString = QString(tr("Author: %1")).arg(authorString);

        if (!commentsStr.isEmpty())
        {
          authorString.append(QString(" | %1").arg(commentsStr));
        }
        if (!category.isEmpty())
        {
          authorString.append(QString(" | %1").arg(category));
        }
      }
      else
      {
        if (!commentsStr.isEmpty())
        {
          authorString.append(commentsStr);
        }

        if (!category.isEmpty())
        {
          if (!commentsStr.isEmpty())
          {
            authorString.append(QString(" | %1").arg(category));
          }
          else
          {
            authorString.append(category);
          }
        }
      }

      authorString = articleView_->prepareArticle(
          authorString, articleUrl(index.row()), "author-" + newsId + "-", false);
      QString labelsString = getHtmlLabels(index.row());

      authorString.append(QString("<table class=\"labels\" id=\"labels%1\"><tr>%2</tr></table>").
                          arg(newsId).arg(labelsString));

      QString enclosureStr;
      QString enclosureUrl = newsModel_->dataField(index.row(), "enclosure_url").toString();

      if (!enclosureUrl.isEmpty())
      {
        QString type = newsModel_->dataField(index.row(), "enclosure_type").toString();

        if (type.contains("image"))
        {
          if (!content.contains(enclosureUrl) && autoLoadImages_)
          {
            enclosureStr = QString("<IMG SRC=\"%1\" class=\"enclosureImg\"><p>").arg(enclosureUrl);
          }
        }
        else
        {
          if (type.contains("audio"))
          {
            type = tr("audio");
            enclosureStr.append("<p>");
          }
          else if (type.contains("video"))
          {
            type = tr("video");
            enclosureStr.append("<p>");
          }
          else
          {
            type = tr("media");
          }

          enclosureStr.append(QString("<a href=\"%1\" class=\"enclosure\"> %2 %3 </a><p>").
                              arg(enclosureUrl, tr("Link to"), type));
        }
      }

      content = enclosureStr + content;
      content = articleView_->prepareArticle(
          content, articleUrl(index.row()), "feed-" + newsId + "-", autoLoadImages_);

      bool ltr = !feedsModel_->dataField(feedIndex, "layoutDirection").toInt();
      QString cssStr = cssString_.
          arg(ltr ? "left" : "right").  // text-align
          arg(ltr ? "ltr" : "rtl").    // direction
          arg(ltr ? "right" : "left");  // "Date" text-align

      if (ltr)
        htmlStr = htmlString_.arg(cssStr, titleString, dateString, authorString, content);
      else
        htmlStr = htmlRtlString_.arg(cssStr, titleString, dateString, authorString, content);

    }

    htmlStr.replace("<body>", "<body><a name=\"article-" + newsId + "\">&#8203;</a>");
    articleView_->setArticleHtml(htmlStr, preservePosition);
    if (feedsModel_->dataField(feedIndex, "layoutDirection").toInt())
      makeRtlAlignmentAbsolute(articleView_->document());
}

void NewsTabWidget::loadNewspaper(int refresh)
{
  if (mainWindow_->newsLayout_ != 1) return;
  articleView_->beginArticles();
  QString htmlStr;
  bool ltr = true;

  if (type_ == TabTypeFeed) {
    QModelIndex feedIndex = feedsProxyModel_->mapToSource(feedsView_->currentIndex());
    ltr = !feedsModel_->dataField(feedIndex, "layoutDirection").toInt();
  }

  QString cssStr = cssString_.arg(ltr ? "left" : "right")
      .arg(ltr ? "ltr" : "rtl").arg(ltr ? "right" : "left");
  QString body;
  for (int idx = 0; idx < newsModel_->rowCount(); ++idx) {
    QModelIndex index = newsModel_->index(idx, newsModel_->fieldIndex("id"));
    QString newsId = newsModel_->dataField(index.row(), "id").toString();

    linkNewsString_ = articleUrl(index.row()).toString();
    QString linkString = ArticleContent::isExternalLink(QUrl(linkNewsString_)) ? linkNewsString_.toHtmlEscaped().replace(QChar(39), "&#39;") : QString();

    QString content = newsModel_->dataField(index.row(), "content").toString();
    {

      QString description = newsModel_->dataField(index.row(), "description").toString();
      if (content.isEmpty() || (description.length() > content.length())) {
        content = description;
      }

      //      QTextDocumentFragment textDocument = QTextDocumentFragment::fromHtml(content);
      //      content = textDocument.toPlainText();
      //      content = articleView_->fontMetrics().elidedText(
      //            content, Qt::ElideRight, 1500);

      QString feedId = newsModel_->dataField(index.row(), "feedId").toString();
      QModelIndex feedIndex = feedsModel_->indexById(feedId.toInt());

      QString iconStr = "qrc:/images/bulletRead";
      QString titleStyle = "read";
      if (newsModel_->dataField(index.row(), "new").toInt() == 1) {
        iconStr = "qrc:/images/bulletNew";
        titleStyle = "unread";
      } else if (newsModel_->dataField(index.row(), "read").toInt() == 0) {
        iconStr = "qrc:/images/bulletUnread";
        titleStyle = "unread";
      }
      QString readImg = QString("<a href=\"quiterss://read.action.ui?#%1\" title='%3'>"
                                "<img width='16' height='16' class='quiterss-img' id=\"readAction%1\" src=\"%2\"/></a>").
          arg(newsId).arg(iconStr).arg(tr("Mark Read/Unread"));

      QString feedImg;
      QByteArray byteArray = feedsModel_->dataField(feedIndex, "image").toByteArray();
      if (!byteArray.isEmpty())
        feedImg = QString("<img width='16' height='16' class='quiterss-img' src=\"data:image/png;base64,") % byteArray % "\"/>";
      else
        feedImg = QString("<img width='16' height='16' class='quiterss-img' src=\"qrc:/images/feed\"/>");

      QString titleString = newsModel_->dataField(index.row(), "title").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      if (!linkString.isEmpty()) {
        titleString = QString("<a href='%1' class='%2' id='title%3'>%4</a>").
            arg(linkString, titleStyle, newsId, titleString);
      }

      QDateTime dtLocal;
      QString dateString = newsModel_->dataField(index.row(), "published").toString();
      if (!dateString.isNull()) {
        QDateTime dtLocalTime = QDateTime::currentDateTime();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), QTimeZone(QTimeZone::UTC));
#else
        QDateTime dtUTC(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
#endif
        int nTimeShift = dtLocalTime.secsTo(dtUTC);

        QDateTime dt = QDateTime::fromString(dateString, Qt::ISODate);
        dtLocal = dt.addSecs(nTimeShift);
      } else {
        dtLocal = QDateTime::fromString(
              newsModel_->dataField(index.row(), "received").toString(),
              Qt::ISODate);
      }
      if (QDateTime::currentDateTime().date() <= dtLocal.date())
        dateString = dtLocal.toString(mainWindow_->formatTime_);
      else
        dateString = dtLocal.toString(mainWindow_->formatDate_ + " " + mainWindow_->formatTime_);

      // Create author panel from news author
      QString authorString;
      QString authorName = newsModel_->dataField(index.row(), "author_name").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      QString authorEmail = newsModel_->dataField(index.row(), "author_email").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      QString authorUri = newsModel_->dataField(index.row(), "author_uri").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

      QRegularExpression reg("(^\\S+@\\S+\\.\\S+)",
          QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
      const QRegularExpressionMatch match = reg.match(authorName);
      int pos = match.capturedStart();
      if (pos > -1) {
        authorName.replace(match.captured(1), QString(" <a href='mailto:%1'>%1</a>").arg(match.captured(1)));
      }
      authorString = authorName;

      if (!authorEmail.isEmpty())
        authorString.append(QString(" <a href='mailto:%1'>e-mail</a>").arg(authorEmail));
      if (!authorUri.isEmpty())
        authorString.append(QString(" <a href='%1'>page</a>"). arg(authorUri));

      // If news author is absent, create author panel from feed author
      // @note(arhohryakov:2012.01.03) Author is got from current feed, because
      //   news is belong to it
      if (authorString.isEmpty()) {
        authorName  = feedsModel_->dataField(feedIndex, "author_name").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
        authorEmail = feedsModel_->dataField(feedIndex, "author_email").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
        authorUri   = feedsModel_->dataField(feedIndex, "author_uri").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

        authorString = authorName;
        if (!authorEmail.isEmpty())
          authorString.append(QString(" <a href='mailto:%1'>e-mail</a>").arg(authorEmail));
        if (!authorUri.isEmpty())
          authorString.append(QString(" <a href='%1'>page</a>").arg(authorUri));
      }

      QString commentsStr;
      QString commentsUrl = newsModel_->dataField(index.row(), "comments").toString().toHtmlEscaped().replace(QChar(39), "&#39;");
      if (!commentsUrl.isEmpty()) {
        commentsStr = QString("<a href=\"%1\"> %2</a>").arg(commentsUrl, tr("Comments"));
      }

      QString category = newsModel_->dataField(index.row(), "category").toString().toHtmlEscaped().replace(QChar(39), "&#39;");

      if (!authorString.isEmpty()) {
        authorString = QString(tr("Author: %1")).arg(authorString);
        if (!commentsStr.isEmpty())
          authorString.append(QString(" | %1").arg(commentsStr));
        if (!category.isEmpty())
          authorString.append(QString(" | %1").arg(category));
      } else {
        if (!commentsStr.isEmpty())
          authorString.append(commentsStr);
        if (!category.isEmpty()) {
          if (!commentsStr.isEmpty())
            authorString.append(QString(" | %1").arg(category));
          else
            authorString.append(category);
        }
      }

      authorString = articleView_->prepareArticle(
          authorString, articleUrl(index.row()), "author-" + newsId + "-", false);
      QString labelsString = getHtmlLabels(index.row());
      authorString.append(QString("<table class=\"labels\" id=\"labels%1\"><tr>%2</tr></table>").
                          arg(newsId).arg(labelsString));

      QString enclosureStr;
      QString enclosureUrl = newsModel_->dataField(index.row(), "enclosure_url").toString();
      if (!enclosureUrl.isEmpty()) {
        QString type = newsModel_->dataField(index.row(), "enclosure_type").toString();
        if (type.contains("image")) {
          if (!content.contains(enclosureUrl) && autoLoadImages_) {
            enclosureStr = QString("<IMG SRC=\"%1\" class=\"enclosureImg\"><p>").
                arg(enclosureUrl);
          }
        } else {
          if (type.contains("audio")) {
            type = tr("audio");
            enclosureStr.append("<p>");
          }
          else if (type.contains("video")) {
            type = tr("video");
            enclosureStr.append("<p>");
          }
          else type = tr("media");

          enclosureStr.append(QString("<a href=\"%1\" class=\"enclosure\"> %2 %3 </a><p>").
                              arg(enclosureUrl, tr("Link to"), type));
        }
      }

      content = enclosureStr + content;
      content = articleView_->prepareArticle(
          content, articleUrl(index.row()), "feed-" + newsId + "-", autoLoadImages_);

      iconStr = "qrc:/images/starOff";
      if (newsModel_->dataField(index.row(), "starred").toInt() == 1) {
        iconStr = "qrc:/images/starOn";
      }
      QString starAction = QString("<span class=\"star-action\">"
                                   "<a href=\"quiterss://star.action.ui?#%1\" title='%3'>"
                                   "<img width='16' height='16' class='quiterss-img' id=\"starAction%1\" src=\"%2\"/></a></span>").
          arg(newsId).arg(iconStr).arg(tr("Star Article"));
      QString labelsMenu = QString("<span class=\"labels-menu\">"
                                   "<a href=\"quiterss://labels.menu.ui?#%1\" title='%2'>"
                                   "<img width='16' height='16' class='quiterss-img' id=\"labelsMenu%1\" src=\"qrc:/images/label_5\"/></a></span>").
          arg(newsId).arg(tr("Label"));
      QString shareMenu = QString("<span class=\"share-menu\">"
                                  "<a href=\"quiterss://share.menu.ui?#%1\" title='%2'>"
                                  "<img width='16' height='16' class='quiterss-img' id=\"shareMenu%1\" src=\"qrc:/images/images/share.png\"/></a></span>").
          arg(newsId).arg(tr("Share"));
      QString openBrowserAction = QString("<span class=\"open-browser\">"
                                          "<a href=\"quiterss://open.browser.ui?#%1\" title='%2'>"
                                          "<img width='16' height='16' class='quiterss-img' id=\"openBrowser%1\" src=\"qrc:/images/openBrowser\"/></a></span>").
          arg(newsId).arg(tr("Open"));
      QString deleteAction = QString("<span class=\"delete-action\">"
                                     "<a href=\"quiterss://delete.action.ui?#%1\" title='%2'>"
                                     "<img width='16' height='16' class='quiterss-img' id=\"deleteAction%1\" src=\"qrc:/images/delete\"/></a></span>").
          arg(newsId).arg(tr("Delete"));
      QString actionNews = starAction % " &nbsp; " % labelsMenu % " &nbsp; " % shareMenu %
          " &nbsp; " % openBrowserAction % " &nbsp; " % deleteAction;

      QString border = "1";
      if (idx + 1 == newsModel_->rowCount())
        border = "0";
      if (ltr) {
        htmlStr = newspaperHtml_.arg(newsId, border, readImg, feedImg, titleString,
                                     dateString, authorString, content, actionNews);
      } else {
        htmlStr = newspaperHtmlRtl_.arg(newsId, border, readImg, feedImg, titleString,
                                        dateString, authorString, content, actionNews);
      }

    }

    body += htmlStr;
  }
  QString html = newspaperHeadHtml_.arg(cssStr);
  html.replace("</body>", body + "</body>");
  articleView_->setArticleHtml(html, refresh != RefreshAll);
  if (!ltr)
    makeRtlAlignmentAbsolute(articleView_->document());
}

/** @brief Asynchorous update web view
 *----------------------------------------------------------------------------*/

void NewsTabWidget::hideArticleContent()
{
  if (mainWindow_->newsLayout_ == 1) return;

  articleView_->beginArticles();
  articleView_->setArticleHtml(QString());
}

void NewsTabWidget::slotLinkClicked(QUrl url)
{
  if (url.scheme() == QLatin1String("quiterss-anchor")) {
    articleView_->scrollToAnchor(url.path());
    return;
  }
  if (url.scheme() == QLatin1String("quiterss")) {
    // Publisher HTML is never allowed to contain this scheme.
    actionNewspaper(url);
    return;
  }
  openUrl(url);
}
//----------------------------------------------------------------------------
void NewsTabWidget::slotLinkHovered(const QString &link)
{
  if (QUrl(link).scheme() == QLatin1String("quiterss")) return;

  mainWindow_->statusBar()->showMessage(link.simplified(), 3000);
}
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void NewsTabWidget::slotLoadStarted()
{
  webViewProgress_->setValue(0);
  webViewProgress_->show();
}
//----------------------------------------------------------------------------
void NewsTabWidget::slotLoadFinished(bool)
{
  webViewProgress_->hide();
}

/** @brief Go to short news content
 *----------------------------------------------------------------------------*/

/** @brief Open news in external browser
 *----------------------------------------------------------------------------*/
void NewsTabWidget::openInExternalBrowserNews()
{
  if (type_ == TabTypeDownloads) return;

  if (type_ != TabTypeDownloads) {
    QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(0);
    QStringList feedIdList;

    int cnt = indexes.count();
    if (cnt == 0) return;

    for (int i = cnt-1; i >= 0; --i) {
      QSqlQuery q;
      QModelIndex curIndex = indexes.at(i);
      if (newsModel_->dataField(curIndex.row(), "read").toInt() == 0) {
        newsModel_->setData(
              newsModel_->index(curIndex.row(), newsModel_->fieldIndex("new")),
              0);
        newsModel_->setData(
              newsModel_->index(curIndex.row(), newsModel_->fieldIndex("read")),
              1);

        int newsId = newsModel_->dataField(curIndex.row(), "id").toInt();
        q.exec(QString("UPDATE news SET new=0, read=1 WHERE id=='%2'").arg(newsId));
        QString feedId = newsModel_->dataField(curIndex.row(), "feedId").toString();
        if (!feedIdList.contains(feedId)) feedIdList.append(feedId);
      }

      openUrl(articleUrl(indexes.at(i).row()));
    }

    if (!feedIdList.isEmpty()) {
      foreach (QString feedId, feedIdList) {
        mainWindow_->slotUpdateStatus(feedId.toInt());
      }
      mainWindow_->recountCategoryCounts();
      newsView_->viewport()->update();
    }
  }
}

void NewsTabWidget::setNewsLayout()
{
  if (type_ >= TabTypeDownloads) return;

  switch (mainWindow_->newsLayout_) {
  case 1:
    newsWidget_->setVisible(false);
    loadNewspaper();
    break;
  default:
    newsWidget_->setVisible(true);
    updateArticleView(newsView_->currentIndex());
  }
}

/** @brief Set browser position
 *----------------------------------------------------------------------------*/
void NewsTabWidget::setBrowserPosition()
{
  if (type_ >= TabTypeDownloads) return;

  int idx = newsTabWidgetSplitter_->indexOf(webWidget_);

  switch (mainWindow_->browserPosition_) {
  case TOP_POSITION: case LEFT_POSITION:
    newsTabWidgetSplitter_->insertWidget(0, newsTabWidgetSplitter_->widget(idx));
    break;
  default:
    newsTabWidgetSplitter_->insertWidget(1, newsTabWidgetSplitter_->widget(idx));
  }

  switch (mainWindow_->browserPosition_) {
  case RIGHT_POSITION: case LEFT_POSITION:
    newsTabWidgetSplitter_->setOrientation(Qt::Horizontal);
    newsTabWidgetSplitter_->setStyleSheet(
          QString("QSplitter::handle {background: qlineargradient("
                  "x1: 0, y1: 0, x2: 0, y2: 1,"
                  "stop: 0 %1, stop: 0.07 %2);}").
          arg(newsPanelWidget_->palette().window().color().name()).
          arg(qApp->palette().color(QPalette::Dark).name()));
    break;
  default:
    newsTabWidgetSplitter_->setOrientation(Qt::Vertical);
    newsTabWidgetSplitter_->setStyleSheet(
          QString("QSplitter::handle {background: %1; margin-top: 1px; margin-bottom: 1px;}").
          arg(qApp->palette().color(QPalette::Dark).name()));
  }

  newsTabWidgetSplitter_->setChildrenCollapsible(false);
}

/** @brief Close tab while press X-button
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotTabClose()
{
  mainWindow_->slotCloseTab(mainWindow_->stackedWidget_->indexOf(this));
}

/** @brief Open link
 *----------------------------------------------------------------------------*/

/** @brief Open link in browser
 *----------------------------------------------------------------------------*/
bool NewsTabWidget::openUrl(const QUrl &url)
{
  if (!ArticleContent::isExternalLink(url)) return false;
  return mainApp->openExternalUrl(url);
}
//----------------------------------------------------------------------------
void NewsTabWidget::slotFindText(const QString &text)
{
  QString objectName = findText_->findGroup_->checkedAction()->objectName();
  if (objectName == "findInBrowserAct") {
    articleView_->highlightText(text);
  } else {
    int newsId = newsModel_->dataField(newsView_->currentIndex().row(), "id").toInt();

    QString filterStr;
    switch (type_) {
    case TabTypeUnread:
    case TabTypeStar:
    case TabTypeDel:
    case TabTypeLabel:
      filterStr = categoryFilterStr_;
      break;
    default:
      filterStr = mainWindow_->newsFilterStr;
    }

    if (!text.isEmpty()) {
      QString findText = text;
      findText = findText.replace("'", "''").toUpper();
      if (objectName == "findTitleAct") {
        filterStr.append(
              QString(" AND UPPER(title) LIKE '%%1%'").arg(findText));
      } else if (objectName == "findAuthorAct") {
        filterStr.append(
              QString(" AND UPPER(author_name) LIKE '%%1%'").arg(findText));
      } else if (objectName == "findCategoryAct") {
        filterStr.append(
              QString(" AND UPPER(category) LIKE '%%1%'").arg(findText));
      } else if (objectName == "findContentAct") {
        filterStr.append(
              QString(" AND (UPPER(content) LIKE '%%1%' OR UPPER(description) LIKE '%%1%')").
              arg(findText));
      } else if (objectName == "findLinkAct") {
        filterStr.append(
              QString(" AND link_href LIKE '%%1%'").
              arg(findText));
      } else {
        filterStr.append(
              QString(" AND (UPPER(title) LIKE '%%1%' OR UPPER(author_name) LIKE '%%1%' "
                      "OR UPPER(category) LIKE '%%1%' OR UPPER(content) LIKE '%%1%' "
                      "OR UPPER(description) LIKE '%%1%')").
              arg(findText));
      }
    }

    newsModel_->setFilter(filterStr);

    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
    if (indexList.count()) {
      int newsRow = indexList.first().row();
      newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    } else {
      currentNewsIdOld = newsId;
      hideArticleContent();
    }
  }
}
//----------------------------------------------------------------------------
void NewsTabWidget::slotSelectFind()
{
  articleView_->highlightText(QString());
  slotFindText(findText_->text());
  if (findText_->findGroup_->checkedAction()->objectName() == "findInBrowserAct")
    articleView_->highlightText(findText_->text(), true);
}
//----------------------------------------------------------------------------
void NewsTabWidget::showArticleContextMenu(const QPoint &p)
{
  QMenu menu(this);
  const QUrl link(articleView_->anchorAt(p));
  if (ArticleContent::isExternalLink(link)) {
    linkUrl_ = link;
    menu.addAction(tr("Open"), this, SLOT(openUrlInExternalBrowser()));
    menu.addAction(tr("Copy Link"), [link]() { QApplication::clipboard()->setText(link.toString()); });
    if (ArticleContent::isRemoteImage(link)) {
      menu.addAction(tr("Save Link..."), [link]() {
        mainApp->downloadManager()->download(QNetworkRequest(link));
      });
    }
    menu.addSeparator();
  }
  const QUrl imageUrl = articleView_->imageAt(p);
  if (!imageUrl.isEmpty()) {
    const QImage image = articleView_->image(imageUrl);
    QAction *copyImage = menu.addAction(tr("Copy Image"), [image]() { QApplication::clipboard()->setImage(image); });
    copyImage->setEnabled(!image.isNull());
    menu.addAction(tr("Copy Image Address"), [imageUrl]() { QApplication::clipboard()->setText(imageUrl.toString()); });
    menu.addSeparator();
  }
  QAction *copy = menu.addAction(tr("Copy"), articleView_, SLOT(copy()));
  copy->setEnabled(articleView_->textCursor().hasSelection());
  menu.addAction(tr("Select All"), articleView_, SLOT(selectAll()));
  menu.addSeparator();
  menu.addAction(mainWindow_->autoLoadImagesToggle_);
  menu.addAction(mainWindow_->printAct_);
  menu.addAction(mainWindow_->printPreviewAct_);
  menu.addAction(mainWindow_->savePageAsAct_);
  menu.exec(articleView_->viewport()->mapToGlobal(p));
}

/** @brief Open link in external browser
 *----------------------------------------------------------------------------*/
void NewsTabWidget::openUrlInExternalBrowser()
{
  openUrl(linkUrl_);
}

/** @brief Set label for selected news
 *----------------------------------------------------------------------------*/
void NewsTabWidget::setLabelNews(int labelId)
{
  if (type_ >= TabTypeDownloads) return;

  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(
        newsModel_->fieldIndex("label"));

  int cnt = indexes.count();
  if (cnt == 0) return;

  if (cnt == 1) {
    QModelIndex index = indexes.at(0);
    QString strIdLabels = index.data(Qt::EditRole).toString();
    if (!strIdLabels.contains(QString(",%1,").arg(labelId))) {
      if (strIdLabels.isEmpty()) strIdLabels.append(",");
      strIdLabels.append(QString::number(labelId));
      strIdLabels.append(",");
    } else {
      strIdLabels.replace(QString(",%1,").arg(labelId), ",");
    }
    newsModel_->setData(index, strIdLabels);

    int newsId = newsModel_->dataField(index.row(), "id").toInt();

    QSqlQuery q;
    q.exec(QString("UPDATE news SET label='%1' WHERE id=='%2'").
           arg(strIdLabels).arg(newsId));
    if (newsId != currentNewsIdOld) {
      newsView_->selectionModel()->select(
            index, QItemSelectionModel::Deselect|QItemSelectionModel::Rows);
    }
  } else {
    bool setLabel = false;
    for (int i = cnt-1; i >= 0; --i) {
      QModelIndex index = indexes.at(i);
      QString strIdLabels = index.data(Qt::EditRole).toString();
      if (!strIdLabels.contains(QString(",%1,").arg(labelId))) {
        setLabel = true;
        break;
      }
    }

    db_.transaction();
    for (int i = cnt-1; i >= 0; --i) {
      QModelIndex index = indexes.at(i);
      QString strIdLabels = index.data(Qt::EditRole).toString();
      if (setLabel) {
        if (strIdLabels.contains(QString(",%1,").arg(labelId))) continue;
        if (strIdLabels.isEmpty()) strIdLabels.append(",");
        strIdLabels.append(QString::number(labelId));
        strIdLabels.append(",");
      } else {
        strIdLabels.replace(QString(",%1,").arg(labelId), ",");
      }
      newsModel_->setData(index, strIdLabels);

      int newsId = newsModel_->dataField(index.row(), "id").toInt();

      QSqlQuery q;
      q.exec(QString("UPDATE news SET label='%1' WHERE id=='%2'").
             arg(strIdLabels).arg(newsId));
      if (newsId != currentNewsIdOld) {
        newsView_->selectionModel()->select(
              index, QItemSelectionModel::Deselect|QItemSelectionModel::Rows);
      }
    }
    db_.commit();
  }
  newsView_->viewport()->update();
  mainWindow_->recountCategoryCounts();
  if (mainWindow_->newsLayout_ == 1) loadNewspaper(RefreshWithPos);
  else updateArticleView(newsView_->currentIndex(), true);
}

void NewsTabWidget::slotNewslLabelClicked(QModelIndex index)
{
  if (!newsView_->selectionModel()->isSelected(index)) {
    newsView_->selectionModel()->clearSelection();
    newsView_->selectionModel()->select(
          index, QItemSelectionModel::Select|QItemSelectionModel::Rows);
  }
  mainWindow_->newsLabelMenu_->popup(
        newsView_->viewport()->mapToGlobal(newsView_->visualRect(index).bottomLeft()));
}

void NewsTabWidget::showLabelsMenu()
{
  if (type_ >= TabTypeDownloads) return;
  if (!newsView_->currentIndex().isValid()) return;

  for (int i = newsHeader_->count()-1; i >= 0; i--) {
    int lIdx = newsHeader_->logicalIndex(i);
    if (!newsHeader_->isSectionHidden(lIdx)) {
      int row = newsView_->currentIndex().row();
      slotNewslLabelClicked(newsModel_->index(row, lIdx));
      break;
    }
  }
}

void NewsTabWidget::reduceNewsList()
{
  if (type_ >= TabTypeDownloads) return;

  QList <int> sizes = newsTabWidgetSplitter_->sizes();
  sizes.insert(0, sizes.takeAt(0) - RESIZESTEP);
  newsTabWidgetSplitter_->setSizes(sizes);
}

void NewsTabWidget::increaseNewsList()
{
  if (type_ >= TabTypeDownloads) return;

  QList <int> sizes = newsTabWidgetSplitter_->sizes();
  sizes.insert(0, sizes.takeAt(0) + RESIZESTEP);
  newsTabWidgetSplitter_->setSizes(sizes);
}

/** @brief Search unread news
 * @param next search condition: true - search next, else - previous
 *----------------------------------------------------------------------------*/
int NewsTabWidget::findUnreadNews(bool next)
{
  int newsRow = -1;

  int newsRowCur = newsView_->currentIndex().row();
  QModelIndex index;
  QModelIndexList indexList;
  if (next) {
    index = newsModel_->index(newsRowCur+1, newsModel_->fieldIndex("read"));
    indexList = newsModel_->match(index, Qt::EditRole, 0);
    if (indexList.isEmpty()) {
      index = newsModel_->index(0, newsModel_->fieldIndex("read"));
      indexList = newsModel_->match(index, Qt::EditRole, 0);
    }
  } else {
    index = newsModel_->index(newsRowCur, newsModel_->fieldIndex("read"));
    indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
  }
  if (!indexList.isEmpty()) newsRow = indexList.last().row();

  return newsRow;
}

/** @brief Set tab title
 *----------------------------------------------------------------------------*/
void NewsTabWidget::setTextTab(const QString &text)
{
  int padding = 15;

  if (closeButton_->isHidden())
    padding = 0;

  const QMargins margins = newsTitleLabel_->layout()->contentsMargins();
  QString textTab = newsTextTitle_->fontMetrics().elidedText(
        text, Qt::ElideRight, newsTitleLabel_->width() - 16 - 3 - padding
        - margins.left() - margins.right());
  newsTextTitle_->setText(textTab);
  newsTitleLabel_->setToolTip(text);

  emit signalSetTextTab(text, this);
}

/** @brief Share news
 *----------------------------------------------------------------------------*/
void NewsTabWidget::slotShareNews(QAction *action)
{
  QList<QModelIndex> indexes;
  if (type_ < TabTypeDownloads) {
    indexes = newsView_->selectionModel()->selectedRows(0);
  }
  if (indexes.isEmpty()) {
    ShareServiceLoader::writeDiagnostic(
        QStringLiteral("service '%1' not opened: no article is selected")
        .arg(action->objectName()));
    return;
  }

  ShareServiceLoader::writeDiagnostic(
      QStringLiteral("triggered service '%1' for %2 article(s)")
      .arg(action->objectName()).arg(indexes.count()));

  for (int i = indexes.count()-1; i >= 0; --i) {
    QString title;
    QString linkString;
    if (type_ < TabTypeDownloads) {
      title = newsModel_->dataField(indexes.at(i).row(), "title").toString();
      linkString = getLinkNews(indexes.at(i).row());
    }

    QString error;
    const QUrl url = ShareServiceLoader::createUrl(
        action->data().toString(), title, linkString, &error);
    if (url.isEmpty()) {
      const QString message = tr("Cannot create the sharing URL for %1:\n%2")
          .arg(action->text(), error);
      ShareServiceLoader::writeDiagnostic(message);
      QMessageBox::warning(mainWindow_, tr("Article sharing"), message);
      continue;
    }
    if (!openUrl(url)) {
      const QString message = tr("The system could not open the sharing URL for %1.")
          .arg(action->text());
      ShareServiceLoader::writeDiagnostic(message);
      QMessageBox::warning(mainWindow_, tr("Article sharing"), message);
    }
  }
}
//-----------------------------------------------------------------------------
void NewsTabWidget::updateActionStates()
{
  if (mainWindow_->currentNewsTab != this) return;

  const bool supportsArticles = type_ < TabTypeDownloads;
  const bool hasSelection = supportsArticles &&
      !newsView_->selectionModel()->selectedRows(0).isEmpty();
  const bool hasNews = supportsArticles && newsModel_->rowCount() > 0;
  const bool hasArticle = supportsArticles &&
      articleView_ && !articleView_->document()->isEmpty();
  mainWindow_->updateNewsActionStates(hasSelection, hasNews, hasArticle);
}
//-----------------------------------------------------------------------------
int NewsTabWidget::getUnreadCount(QString countString)
{
  if (countString.isEmpty()) return 0;

  countString.remove(QRegularExpression("[()]", QRegularExpression::DotMatchesEverythingOption));
  switch (type_) {
  case TabTypeUnread:
    return countString.toInt();
  case TabTypeStar:
  case TabTypeLabel:
    return countString.section("/", 0, 0).toInt();
  default:
    return 0;
  }
}

QUrl NewsTabWidget::articleUrl(int row) const
{
  const int feedId = newsModel_->dataField(row, "feedId").toInt();
  const QModelIndex feed = feedsModel_->indexById(feedId);
  QUrl base(feedsModel_->dataField(feed, "xmlUrl").toString());
  const QUrl home(feedsModel_->dataField(feed, "htmlUrl").toString());
  if (!home.isEmpty()) base = base.resolved(home);
  QString link = newsModel_->dataField(row, "link_href").toString();
  if (link.isEmpty()) link = newsModel_->dataField(row, "link_alternate").toString();
  const QUrl result = base.resolved(QUrl(link.trimmed()));
  return ArticleContent::isExternalLink(result) ? result : QUrl();
}

QString NewsTabWidget::getLinkNews(int row)
{
  QString linkString = newsModel_->dataField(row, "link_href").toString();
  if (linkString.isEmpty())
    linkString = newsModel_->dataField(row, "link_alternate").toString();
  return linkString.simplified();
}

QString NewsTabWidget::getHtmlLabels(int row)
{
  QStringList strLabelIdList = newsModel_->dataField(row, "label").toString().
      split(",", Qt::SkipEmptyParts);
  QString labelsString;
  QList<QTreeWidgetItem *> labelListItems = mainWindow_->categoriesTree_->getLabelListItems();
  foreach (QTreeWidgetItem *item, labelListItems) {
    if (strLabelIdList.contains(item->text(2))) {
      strLabelIdList.removeOne(item->text(2));
      QByteArray byteArray = item->data(0, CategoriesTreeWidget::ImageRole).toByteArray();
      labelsString.append(QString("<td><img width='16' height='16' class='quiterss-img' src=\"data:image/png;base64,") % byteArray.toBase64() % "\"/></td>");
      labelsString.append("<td>" % item->text(0).toHtmlEscaped());
      if (strLabelIdList.count())
        labelsString.append(",");
      labelsString.append("</td>");
    }
  }
  return labelsString;
}

void NewsTabWidget::actionNewspaper(QUrl url)
{
  if (mainWindow_->newsLayout_ != 1) return;
  bool valid = false;
  const int newsId = url.fragment().toInt(&valid);
  if (!valid) return;
  QModelIndex first = newsModel_->index(0, newsModel_->fieldIndex("id"));
  const QModelIndexList matches = newsModel_->match(first, Qt::EditRole, newsId, 1, Qt::MatchExactly);
  if (matches.isEmpty()) return;
  const QModelIndex index = matches.first();
  const int row = index.row();
  const QString action = url.host();
  if (action == "read.action.ui") {
    slotSetItemRead(index, newsModel_->dataField(row, "read").toInt() ? 0 : 1);
    loadNewspaper(RefreshWithPos);
  } else if (action == "star.action.ui") {
    slotSetItemStar(newsModel_->index(row, newsModel_->fieldIndex("starred")),
                    newsModel_->dataField(row, "starred").toInt() ? 0 : 1);
    loadNewspaper(RefreshWithPos);
  } else if (action == "open.browser.ui") {
    openUrl(articleUrl(row));
  } else if (action == "labels.menu.ui" || action == "share.menu.ui" || action == "delete.action.ui") {
    newsView_->selectionModel()->clearSelection();
    newsView_->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    currentNewsIdOld = newsId;
    if (action == "labels.menu.ui") mainWindow_->newsLabelMenu_->popup(QCursor::pos());
    else if (action == "share.menu.ui") mainWindow_->shareMenu_->popup(QCursor::pos());
    else deleteNews();
  }
}

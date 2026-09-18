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
#ifndef NEWSTABWIDGET_H
#define NEWSTABWIDGET_H

#include <QtWidgets>
#include <QtSql>

#include "feedsproxymodel.h"
#include "feedsmodel.h"
#include "feedsview.h"
#include "findtext.h"
#include "lineedit.h"
#include "newsheader.h"
#include "newsmodel.h"
#include "newsview.h"
#include "articleview.h"

class MainWindow;

#define TOP_POSITION    0
#define BOTTOM_POSITION 1
#define RIGHT_POSITION  2
#define LEFT_POSITION   3

#define RESIZESTEP 25   // News list/browser size step

class NewsTabWidget : public QWidget
{
  Q_OBJECT
public:
  enum TabType {
    TabTypeFeed,
    TabTypeUnread,
    TabTypeStar,
    TabTypeDel,
    TabTypeLabel,
    TabTypeDownloads = 6
  };

  enum RefreshNewspaper {
    RefreshAll,
    RefreshInsert,
    RefreshWithPos
  };

  explicit NewsTabWidget(QWidget *parent, TabType type, int feedId = -1, int feedParId = -1);
  ~NewsTabWidget();

  void disconnectObjects();

  void retranslateStrings();
  void setSettings(bool init = true, bool newTab = true);
  void refreshAppearance(bool reloadArticle = false);
  void setNewsLayout();
  void setBrowserPosition();
  void markNewsRead();
  void markAllNewsRead();
  void markNewsStar();
  void setLabelNews(int labelId);
  void deleteNews();
  void deleteAllNewsList();
  void restoreNews();
  void slotCopyLinkNews();
  void showLabelsMenu();

  bool openUrl(const QUrl &url);
  void openInExternalBrowserNews();

  void updateArticleView(QModelIndex index, bool preservePosition = false);
  void loadNewspaper(int refresh = RefreshAll);
  void hideArticleContent();
  QString getLinkNews(int row);
  QUrl articleUrl(int row) const;

  void reduceNewsList();
  void increaseNewsList();

  int findUnreadNews(bool next);

  void setTextTab(const QString &text);
  void updateActionStates();

  void slotShareNews(QAction *action);

  /*! \brief Convert \a countString to unreadCount depending on \a type_
   * \param countString from categories tree
   * \return unreadCount for displaying in status
   */
  int getUnreadCount(QString countString);

  TabType type_;
  int feedId_;
  int feedParId_;
  int currentNewsIdOld;
  bool autoLoadImages_;
  int labelId_;
  QString categoryFilterStr_;

  FindTextContent *findText_;

  NewsModel *newsModel_;
  NewsView *newsView_;
  NewsHeader *newsHeader_;
  QToolBar *newsToolBar_;
  QSplitter *newsTabWidgetSplitter_;

  QWidget *newsWidget_;
  ArticleView *articleView_;

  QLabel *newsIconTitle_;
  QMovie *newsIconMovie_;
  QLabel *newsTextTitle_;
  QWidget *newsTitleLabel_;
  QToolButton *closeButton_;

  QAction *separatorRAct_;

public slots:
  void setAutoLoadImages(bool apply = true);
  void slotNewsViewClicked(QModelIndex index);
  void slotNewsViewSelected(QModelIndex index, bool clicked=false);
  void slotNewsViewDoubleClicked(QModelIndex index);
  void slotNewsMiddleClicked(QModelIndex index);
  void slotNewsUpPressed(QModelIndex index=QModelIndex());
  void slotNewsDownPressed(QModelIndex index=QModelIndex());
  void slotNewsHomePressed(QModelIndex index=QModelIndex());
  void slotNewsEndPressed(QModelIndex index=QModelIndex());
  void slotNewsPageUpPressed(QModelIndex index=QModelIndex());
  void slotNewsPageDownPressed(QModelIndex index=QModelIndex());
  void slotSort(int column, int order);

signals:
  void signalSetTextTab(const QString &text, NewsTabWidget *widget);

private slots:
  void showContextMenuNews(const QPoint &pos);
  void slotSetItemRead(QModelIndex index, int read);
  void slotSetItemStar(QModelIndex index, int starred);
  void slotMarkReadTimeout();

  void slotLinkClicked(QUrl url);
  void slotLinkHovered(const QString &link);
  void slotLoadStarted();
  void slotLoadFinished(bool);
  void showArticleContextMenu(const QPoint &p);
  void openUrlInExternalBrowser();

  void slotTabClose();

  void slotFindText(const QString& text);
  void slotSelectFind();

  void slotNewslLabelClicked(QModelIndex index);

private:
  void createNewsList();
  void createArticleWidget();
  QString getHtmlLabels(int row);
  void actionNewspaper(QUrl url);

  MainWindow *mainWindow_;
  QSqlDatabase db_;

  FeedsModel *feedsModel_;
  FeedsProxyModel *feedsProxyModel_;
  FeedsView *feedsView_;

  QFrame *lineWebWidget;
  QWidget *webWidget_;
  QProgressBar *webViewProgress_;

  QTimer *markNewsReadTimer_;


  QUrl linkUrl_;
  QString linkNewsString_;

  QWidget *newsPanelWidget_;

  QString newspaperHeadHtml_;
  QString newspaperHtml_;
  QString newspaperHtmlRtl_;
  QString htmlString_;
  QString htmlRtlString_;
  QString cssString_;

};

#endif // NEWSTABWIDGET_H

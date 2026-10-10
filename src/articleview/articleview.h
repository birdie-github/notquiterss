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
#ifndef ARTICLEVIEW_H
#define ARTICLEVIEW_H

#include <QTextBrowser>
#include <QHash>
#include <QMap>
#include <QImage>
#include <QSet>
#include <QTextDocument>

class ArticleImages;
class ArticleDocument;
class QMovie;
class QPrinter;

// A feed document viewer. All resources go through the article image policy;
// QTextBrowser's source loading, history and automatic navigation are disabled.
class ArticleView : public QTextBrowser {
  Q_OBJECT
public:
  explicit ArticleView(QWidget *parent = nullptr);
  void beginArticles();
  QString prepareArticle(const QString &html, const QUrl &base, const QString &prefix, bool images);
  void setArticleHtml(const QString &html, bool preservePosition = false);
  void highlightText(const QString &text, bool reveal = false);
  qreal zoomFactor() const { return zoom_; }
  void setZoomFactor(qreal factor);
  QUrl imageAt(const QPoint &pos) const;
  QImage image(const QUrl &url) const { return imagesCache_.value(url); }
  QString exportHtml() const;
  void disconnectObjects();

signals:
  void articleContentChanged();
  void linkClicked(const QUrl &url);
  void linkHovered(const QString &url);
  void showContextMenu(const QPoint &pos);
  void loadStarted();
  void loadFinished(bool ok);
  void loadProgress(int percent);

public slots:
  void print(QPrinter *printer);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  void setSource(const QUrl &) override;
#else
  void setSource(const QUrl &);
#endif
  void setSource(const QUrl &, QTextDocument::ResourceType);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void doSetSource(const QUrl &, QTextDocument::ResourceType) override;
#endif
  QVariant loadResource(int type, const QUrl &url) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  friend class ArticleDocument;
  struct Location { QString article; int offset = 0; int absolute = 0; };
  struct ViewState { Location top, position, anchor; int topOffset = 0; int horizontal = 0; };
  Location location(int position) const;
  int position(const Location &location) const;
  ViewState saveView() const;
  void restoreView(const ViewState &state);
  void scaleDocument();
  void requestImage(const QUrl &url);
  void installImage(const QUrl &url, const QImage &image);
  void updateProgress();
  ArticleImages *images_;
  QHash<QUrl, QImage> imagesCache_;
  QHash<QUrl, QMovie *> movies_;
  QSet<QMovie *> pausedMovies_;
  QSet<QUrl> allowedImages_, pending_, failed_;
  QMap<int, QString> articleAnchors_;
  QHash<QString, int> anchorPositions_;
  QString search_;
  qreal zoom_ = 1.0;
  int generation_ = 0;
  int requested_ = 0;
  int completed_ = 0;
  bool loading_ = false;
  bool scaling_ = false;
  bool animationsVisible_ = false;
};
#endif

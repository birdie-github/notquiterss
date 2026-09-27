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
#include "articleview.h"
#include "articlecontent.h"
#include "articleimages.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QContextMenuEvent>
#include <QImageReader>
#include <QMouseEvent>
#include <QMovie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QPrinter>
#include <QScrollBar>
#include <QScopedPointer>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QWheelEvent>

class ArticleDocument : public QTextDocument {
public:
  explicit ArticleDocument(ArticleView *view) : QTextDocument(view), view_(view) {}
protected:
  QVariant loadResource(int type, const QUrl &name) override {
    // Never fall back to QTextDocument's local-file resource loader.
    return view_->loadResource(type, name);
  }
private:
  ArticleView *view_;
};

namespace {
const int OriginalFontSize = QTextFormat::UserProperty + 1;
const int OriginalImageWidth = QTextFormat::UserProperty + 2;
const int OriginalImageHeight = QTextFormat::UserProperty + 3;
QImage placeholder() {
  QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setPen(Qt::gray);
  painter.drawRect(1, 1, 13, 13);
  painter.drawLine(2, 13, 13, 2);
  return image;
}
}

ArticleView::ArticleView(QWidget *parent) : QTextBrowser(parent), images_(new ArticleImages(this)) {
  setReadOnly(true);
  setOpenLinks(false);
  setOpenExternalLinks(false);
  setAcceptDrops(false);
  setUndoRedoEnabled(false);
  setDocument(new ArticleDocument(this));
  connect(this, &QTextBrowser::anchorClicked, this, &ArticleView::linkClicked);
  connect(this, QOverload<const QUrl &>::of(&QTextBrowser::highlighted), this,
          [this](const QUrl &url) { emit linkHovered(url.toString()); });
}

void ArticleView::disconnectObjects() {
  ++generation_;
  images_->reset();
  for (QMovie *movie : movies_) movie->stop();
  disconnect(this);
}

void ArticleView::beginArticles() {
  ++generation_;
  images_->reset();
  allowedImages_.clear();
  pending_.clear();
  failed_.clear();
  requested_ = completed_ = 0;
}

QString ArticleView::prepareArticle(const QString &html, const QUrl &base, const QString &prefix, bool enabled) {
  QSet<QUrl> urls;
  const QString result = ArticleContent::sanitize(html, base, prefix, enabled, &urls);
  allowedImages_ += urls;
  images_->allow(urls);
  ArticleImages::trace(QString("article %1 images-enabled=%2 allowed-images=%3")
                       .arg(prefix).arg(enabled).arg(urls.size()));
  return result;
}

void ArticleView::setArticleHtml(const QString &html, bool preservePosition) {
  const ViewState state = saveView();
  // Replacing the document is synchronous, including clearing an empty feed.
  // requestImage() starts progress only when resources actually need loading.
  QPointer<QTextDocument> old = document();
  ArticleDocument *doc = new ArticleDocument(this);
  doc->setDefaultFont(font());
  // QTextDocument has its own outer margin; the article HTML already controls
  // spacing.  Keep it at zero so body { margin: 0 } behaves like the old
  // WebKit view instead of adding a second inset around every article.
  doc->setDocumentMargin(0);
  setDocument(doc);
  if (old) old->deleteLater();
  doc->setHtml(html);
  clearHistory();
  // Keep cached resources for surviving articles, but release departed articles.
  QSet<QUrl> used;
  articleAnchors_.clear();
  anchorPositions_.clear();
  for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      for (const QString &name : it.fragment().charFormat().anchorNames()) {
        if (name.startsWith("article-")) {
          articleAnchors_.insert(it.fragment().position(), name);
          anchorPositions_.insert(name, it.fragment().position());
        }
      }
      if (it.fragment().charFormat().isImageFormat())
        used.insert(QUrl(it.fragment().charFormat().toImageFormat().name()));
    }
  }
  for (auto it = imagesCache_.begin(); it != imagesCache_.end();) {
    if (!used.contains(it.key())) {
      if (QMovie *movie = movies_.take(it.key())) { movie->stop(); movie->deleteLater(); }
      it = imagesCache_.erase(it);
    } else ++it;
  }
  scaleDocument();
  highlightText(search_);
  if (preservePosition) restoreView(state);
  else { moveCursor(QTextCursor::Start); verticalScrollBar()->setValue(0); }
  updateProgress();
  // Notify after replacement is complete. Connections to the old document
  // cannot observe subsequent article replacements or empty-feed clears.
  emit articleContentChanged();
}

QVariant ArticleView::loadResource(int type, const QUrl &url) {
  if (type != QTextDocument::ImageResource) return QByteArray();
  const bool icon = url.scheme() == "qrc" && url.path().startsWith("/images/");
  const bool allowed = icon || ArticleContent::isInlineImage(url) ||
      (allowedImages_.contains(url) && ArticleContent::isRemoteImage(url));
  if (!allowed) return placeholder();
  if (imagesCache_.contains(url)) return imagesCache_.value(url);
  if (!pending_.contains(url) && !failed_.contains(url)) {
    pending_.insert(url);
    ++requested_;
    const int generation = generation_;
    QTimer::singleShot(0, this, [this, url, generation]() {
      if (generation == generation_) requestImage(url);
    });
  }
  return placeholder();
}

void ArticleView::requestImage(const QUrl &url) {
  if (!loading_) { loading_ = true; emit loadStarted(); }
  const int generation = generation_;
  QNetworkReply *reply = images_->get(QNetworkRequest(url));
  connect(reply, &QNetworkReply::finished, this, [this, url, reply, generation]() {
    reply->deleteLater();
    if (generation != generation_) return;
    pending_.remove(url);
    ++completed_;
    const QByteArray bytes = reply->readAll();
    const QImage image = reply->error() == QNetworkReply::NoError ? QImage::fromData(bytes) : QImage();
    if (image.isNull()) {
      failed_.insert(url);
      ArticleImages::trace("article image unavailable " + ArticleImages::describeUrl(url));
    } else {
      installImage(url, image);
      // QMovie uses the same installed Qt image handlers; it does not fetch URLs.
      QBuffer probe;
      probe.setData(bytes); probe.open(QIODevice::ReadOnly);
      QImageReader reader(&probe);
      if (reader.supportsAnimation()) {
        QMovie *movie = new QMovie(this);
        QBuffer *buffer = new QBuffer(movie);
        buffer->setData(bytes); buffer->open(QIODevice::ReadOnly);
        movie->setDevice(buffer);
        movie->setCacheMode(QMovie::CacheNone);
        movies_.insert(url, movie);
        connect(movie, &QMovie::frameChanged, this, [this, url, movie](int) {
          if (movies_.value(url) == movie) installImage(url, movie->currentImage());
        });
        movie->start();
      }
    }
    updateProgress();
  });
}

void ArticleView::installImage(const QUrl &url, const QImage &image) {
  if (image.isNull()) return;
  const ViewState state = saveView();
  const bool changedSize = !imagesCache_.contains(url) || imagesCache_.value(url).size() != image.size();
  imagesCache_.insert(url, image);
  document()->addResource(QTextDocument::ImageResource, url, image);
  if (changedSize) {
    scaleDocument();
    document()->markContentsDirty(0, document()->characterCount());
    restoreView(state);
  }
  viewport()->update();
}

void ArticleView::updateProgress() {
  emit loadProgress(requested_ ? completed_ * 100 / requested_ : 100);
  if (pending_.isEmpty() && loading_) {
    loading_ = false;
    emit loadFinished(failed_.isEmpty());
  }
}

ArticleView::Location ArticleView::location(int pos) const {
  Location result;
  result.absolute = result.offset = pos;
  auto it = articleAnchors_.upperBound(pos);
  if (it != articleAnchors_.constBegin()) {
    --it;
    result.article = it.value();
    result.offset = pos - it.key();
  }
  return result;
}
int ArticleView::position(const Location &loc) const {
  const int value = anchorPositions_.contains(loc.article) ?
      anchorPositions_.value(loc.article) + loc.offset : loc.absolute;
  return qBound(0, value, document()->characterCount() - 1);
}
ArticleView::ViewState ArticleView::saveView() const {
  ViewState state;
  const QTextCursor top = cursorForPosition(QPoint(1, 1));
  state.top = location(top.position());
  state.topOffset = cursorRect(top).top();
  state.position = location(textCursor().position());
  state.anchor = location(textCursor().anchor());
  state.horizontal = horizontalScrollBar()->value();
  return state;
}
void ArticleView::restoreView(const ViewState &state) {
  QTextCursor selection(document());
  selection.setPosition(position(state.anchor));
  selection.setPosition(position(state.position), QTextCursor::KeepAnchor);
  setTextCursor(selection);
  QTextCursor top(document()); top.setPosition(position(state.top));
  verticalScrollBar()->setValue(verticalScrollBar()->value() + cursorRect(top).top() - state.topOffset);
  horizontalScrollBar()->setValue(state.horizontal);
}

void ArticleView::scaleDocument() {
  if (scaling_) return;
  scaling_ = true;
  QTextCursor edit(document());
  edit.beginEditBlock();
  for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
    // Collect ranges first because format changes invalidate fragment iterators.
    QList<QTextCursor> fragments;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      QTextCursor cursor(document()); cursor.setPosition(fragment.position());
      cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
      fragments.append(cursor);
    }
    for (QTextCursor cursor : fragments) {
      QTextCharFormat format = cursor.charFormat();
      if (format.isImageFormat()) {
        QTextImageFormat image = format.toImageFormat();
        if (!image.hasProperty(OriginalImageWidth)) {
          image.setProperty(OriginalImageWidth, image.width());
          image.setProperty(OriginalImageHeight, image.height());
        }
        const QImage pixels = imagesCache_.value(QUrl(image.name()), placeholder());
        qreal w = image.doubleProperty(OriginalImageWidth);
        qreal h = image.doubleProperty(OriginalImageHeight);
        if (w <= 0 && h <= 0) { w = pixels.width(); h = pixels.height(); }
        else if (w <= 0) w = h * pixels.width() / qMax(1, pixels.height());
        else if (h <= 0) h = w * pixels.height() / qMax(1, pixels.width());
        const qreal width = qMin(w * zoom_, qreal(qMax(16, viewport()->width() - 32)));
        image.setWidth(width);
        image.setHeight(h * width / qMax(qreal(1), w));
        if (cursor.charFormat() != image) cursor.setCharFormat(image);
      } else {
        if (!format.hasProperty(OriginalFontSize)) {
          qreal size = format.font().pointSizeF();
          if (size <= 0 && format.intProperty(QTextFormat::FontPixelSize) > 0)
            size = format.intProperty(QTextFormat::FontPixelSize) * 72.0 / logicalDpiY();
          if (size <= 0) size = document()->defaultFont().pointSizeF();
          format.setProperty(OriginalFontSize, qMax(qreal(1), size));
        }
        format.clearProperty(QTextFormat::FontPixelSize);
        format.clearProperty(QTextFormat::FontSizeAdjustment);
        format.setFontPointSize(format.doubleProperty(OriginalFontSize) * zoom_);
        if (cursor.charFormat() != format) cursor.setCharFormat(format);
      }
    }
  }
  edit.endEditBlock();
  scaling_ = false;
}

void ArticleView::setZoomFactor(qreal factor) {
  const ViewState state = saveView();
  zoom_ = qBound(qreal(0.3), factor, qreal(5.0));
  scaleDocument();
  restoreView(state);
}
void ArticleView::highlightText(const QString &text, bool reveal) {
  search_ = text;
  QList<QTextEdit::ExtraSelection> highlights;
  QTextCursor cursor(document());
  if (!text.isEmpty()) {
    while (!(cursor = document()->find(text, cursor)).isNull()) {
      QTextEdit::ExtraSelection selection;
      selection.cursor = cursor;
      selection.format.setBackground(Qt::yellow);
      selection.format.setForeground(Qt::black);
      highlights.append(selection);
    }
  }
  setExtraSelections(highlights);
  if (reveal && !highlights.isEmpty()) {
    setTextCursor(highlights.first().cursor);
    ensureCursorVisible();
  }
}
QUrl ArticleView::imageAt(const QPoint &pos) const {
  const int index = document()->documentLayout()->hitTest(
      QPointF(pos.x() + horizontalScrollBar()->value(), pos.y() + verticalScrollBar()->value()), Qt::ExactHit);
  if (index < 0) return QUrl();
  QTextCursor cursor(document()); cursor.setPosition(index);
  cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
  return cursor.charFormat().isImageFormat() ? QUrl(cursor.charFormat().toImageFormat().name()) : QUrl();
}
QString ArticleView::exportHtml() const {
  // Export the article, excluding application controls and resource-only icons.
  QScopedPointer<QTextDocument> copy(document()->clone());
  QList<QTextCursor> runs;
  for (QTextBlock block = copy->begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      QTextCursor cursor(copy.data()); cursor.setPosition(it.fragment().position());
      cursor.setPosition(it.fragment().position() + it.fragment().length(), QTextCursor::KeepAnchor);
      runs.append(cursor);
    }
  }
  for (int i = runs.size() - 1; i >= 0; --i) {
    QTextCursor cursor = runs.at(i);
    QTextCharFormat format = cursor.charFormat();
    const QUrl link(format.anchorHref());
    const bool appIcon = format.isImageFormat() &&
        QUrl(format.toImageFormat().name()).scheme() == "qrc";
    if (link.scheme() == "quiterss" || appIcon) cursor.removeSelectedText();
    else if (link.scheme() == "quiterss-anchor") {
      format.setAnchorHref("#" + link.path());
      cursor.setCharFormat(format);
    }
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return copy->toHtml(); // Qt 6 exports UTF-8 HTML.
#else
  return copy->toHtml("UTF-8");
#endif
}
void ArticleView::print(QPrinter *printer) { document()->print(printer); }
void ArticleView::setSource(const QUrl &) {
  // QTextBrowser's virtual source entry point in Qt 5.
}
void ArticleView::setSource(const QUrl &, QTextDocument::ResourceType) {
  // Deliberately no source loader: all documents are supplied by the reader.
}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ArticleView::doSetSource(const QUrl &, QTextDocument::ResourceType) {
  // Qt 6 routes base-class source loading through this virtual entry point.
}
#endif
void ArticleView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && event->modifiers() != Qt::NoModifier)) {
    const QUrl link(anchorAt(event->pos()));
    if (!link.isEmpty()) { emit linkClicked(link); event->accept(); return; }
  }
  QTextBrowser::mouseReleaseEvent(event);
}
void ArticleView::contextMenuEvent(QContextMenuEvent *event) {
  emit showContextMenu(event->pos());
  event->accept();
}
void ArticleView::wheelEvent(QWheelEvent *event) {
  if (event->modifiers() == Qt::ControlModifier) {
    setZoomFactor(zoom_ + (event->angleDelta().y() > 0 ? 0.1 : -0.1));
    event->accept(); return;
  }
  QTextBrowser::wheelEvent(event);
}
void ArticleView::resizeEvent(QResizeEvent *event) {
  const ViewState state = saveView();
  QTextBrowser::resizeEvent(event);
  scaleDocument();
  restoreView(state);
}

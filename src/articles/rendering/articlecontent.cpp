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
#include "articlecontent.h"

#include <libxml/HTMLparser.h>
#include <QRegularExpression>
#include <QCoreApplication>

namespace {
QString text(const xmlChar *s) { return s ? QString::fromUtf8(reinterpret_cast<const char *>(s)) : QString(); }
QString attr(xmlNode *node, const char *name) {
  xmlChar *value = xmlGetProp(node, BAD_CAST name);
  QString result = text(value);
  xmlFree(value);
  return result;
}
QString attribute(const QString &name, const QString &value) {
  return " " + name + "=\"" + value.toHtmlEscaped() + "\"";
}
bool dataImage(const QUrl &url) {
  const QString value = url.toString();
  static const QRegularExpression image("^data:image/(png|jpeg|gif|webp);base64,[A-Za-z0-9+/=\\s]+$", QRegularExpression::CaseInsensitiveOption);
  return value.size() <= 8 * 1024 * 1024 && image.match(value).hasMatch();
}
QString safeStyle(const QString &style) {
  static const QSet<QString> properties = {"color", "background-color", "font-weight", "font-style",
    "text-decoration", "text-align", "vertical-align", "white-space", "direction"};
  static const QRegularExpression value("^[a-zA-Z0-9# .,%()-]+$");
  QString result;
  for (const QString &entry : style.split(';')) {
    const int colon = entry.indexOf(':');
    const QString name = entry.left(colon).trimmed().toLower();
    const QString v = entry.mid(colon + 1).trimmed();
    if (colon > 0 && properties.contains(name) && value.match(v).hasMatch() &&
        !v.contains("url", Qt::CaseInsensitive) && !v.contains("expression", Qt::CaseInsensitive))
      result += name + ":" + v + ";";
  }
  return result;
}
struct Writer {
  QUrl base;
  QString prefix;
  bool imagesEnabled;
  QSet<QUrl> *images;
  int nodes;
  Writer(const QUrl &b, const QString &p, bool enabled, QSet<QUrl> *allowed)
    : base(b), prefix(p), imagesEnabled(enabled), images(allowed), nodes(0) {}
  QString render(xmlNode *node, int depth = 0) {
    if (depth > 128) return QString();
    QString out;
    static const QSet<QString> drop = {"head", "script", "style", "iframe", "frame", "frameset",
      "object", "embed", "applet", "svg", "math", "input", "button", "textarea", "select", "link", "meta", "base"};
    static const QSet<QString> allowed = {"p", "div", "span", "br", "hr", "a", "img", "b", "strong",
      "i", "em", "u", "s", "del", "small", "sub", "sup", "blockquote", "pre", "code", "tt",
      "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "dl", "dt", "dd", "table",
      "thead", "tbody", "tfoot", "tr", "td", "th", "caption", "figure", "figcaption", "center"};
    for (; node; node = node->next) {
      if (++nodes > 50000) break;
      if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
        out += text(node->content).toHtmlEscaped();
        continue;
      }
      if (node->type != XML_ELEMENT_NODE) continue;
      QString tag = text(node->name).toLower();
      if (tag == "iframe" || tag == "object" || tag == "embed" || tag == "audio" || tag == "video") {
        QString source = attr(node, tag == "object" ? "data" : "src");
        if (source.isEmpty()) {
          for (xmlNode *child = node->children; child; child = child->next) {
            if (child->type == XML_ELEMENT_NODE && text(child->name).toLower() == "source") {
              source = attr(child, "src");
              if (!source.isEmpty()) break;
            }
          }
        }
        const QUrl link = base.resolved(QUrl(source));
        if (!source.isEmpty() && ArticleContent::isExternalLink(link))
          out += "<p><a" + attribute("href", link.toString()) + ">" +
            QCoreApplication::translate("ArticleContent", "Open embedded media externally").toHtmlEscaped() + "</a></p>";
        continue;
      }
      if (drop.contains(tag)) continue;
      if (!allowed.contains(tag)) {
        out += render(node->children, depth + 1);
        continue;
      }
      QString attributes;
      if (tag == "a") {
        const QString href = attr(node, "href").trimmed();
        if (href.startsWith('#')) attributes += attribute("href", "quiterss-anchor:" + prefix + href.mid(1));
        else {
          const QUrl url = base.resolved(QUrl(href));
          if (!href.isEmpty() && ArticleContent::isExternalLink(url)) attributes += attribute("href", url.toString());
        }
        const QString name = attr(node, "name");
        if (!name.isEmpty()) attributes += attribute("name", prefix + name);
      }
      const QString id = attr(node, "id");
      if (!id.isEmpty()) out += "<a" + attribute("name", prefix + id) + ">&#8203;</a>";
      if (tag == "img") {
        QString src = attr(node, "src").trimmed();
        if (src.isEmpty()) src = attr(node, "data-src").trimmed();
        const QUrl url = base.resolved(QUrl(src));
        if (!imagesEnabled || src.isEmpty() || !(ArticleContent::isRemoteImage(url) || dataImage(url))) {
          out += attr(node, "alt").toHtmlEscaped();
          continue;
        }
        attributes += attribute("src", url.toString());
        attributes += attribute("style", "max-width:100%;height:auto;");
        images->insert(url);
      }
      for (const char *key : {"title", "alt", "dir", "align", "width", "height", "colspan", "rowspan", "border", "cellpadding", "cellspacing"}) {
        const QString value = attr(node, key);
        const QString name = QString::fromLatin1(key);
        if (tag == "img" && (name == "width" || name == "height")) {
          bool valid = false;
          const uint size = value.toUInt(&valid);
          // Rich text does not implement percentage image dimensions. Let the
          // viewer fit those images to the pane using their actual dimensions.
          if (!valid || size == 0 || size > 32768) continue;
        }
        if (!value.isEmpty()) attributes += attribute(name, value);
      }
      const QString style = safeStyle(attr(node, "style"));
      if (tag != "img" && !style.isEmpty()) attributes += attribute("style", style);
      out += "<" + tag + attributes + ">";
      if (tag != "img" && tag != "br" && tag != "hr") out += render(node->children, depth + 1) + "</" + tag + ">";
    }
    return out;
  }
};
}

bool ArticleContent::isExternalLink(const QUrl &url) {
  return url.isValid() && url.userInfo().isEmpty() &&
    (((url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty()) || url.scheme() == "mailto");
}
bool ArticleContent::isInlineImage(const QUrl &url) { return dataImage(url); }
bool ArticleContent::isRemoteImage(const QUrl &url) {
  return isExternalLink(url) && (url.scheme() == "http" || url.scheme() == "https");
}
QString ArticleContent::sanitize(const QString &html, const QUrl &baseUrl, const QString &anchorPrefix,
                                bool loadImages, QSet<QUrl> *images) {
  const QByteArray bytes = html.toUtf8();
  if (bytes.size() > 4 * 1024 * 1024) return QStringLiteral("<p>Article is too large to display. Open the original article externally.</p>");
  // NONET, no DTD loading/entity substitution, and libxml's normal parser limits.
  htmlDocPtr doc = htmlReadMemory(bytes.constData(), bytes.size(), nullptr, "UTF-8",
    HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER);
  if (!doc) return html.toHtmlEscaped();
  Writer writer(baseUrl, anchorPrefix, loadImages, images);
  const QString result = writer.render(xmlDocGetRootElement(doc));
  xmlFreeDoc(doc);
  return result;
}

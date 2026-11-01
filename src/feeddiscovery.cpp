// SPDX-License-Identifier: GPL-3.0-or-later
#include "feeddiscovery.h"
#include "feedurl.h"
#include "feedxml.h"
#include "credentialscope.h"
#include "networkpolicy.h"
#include <utility>

#include <QtWidgets>
#include <QSqlQuery>
#include <libxml/HTMLparser.h>

namespace {
QString takeText(xmlChar *value)
{
  QString result = QString::fromUtf8(reinterpret_cast<const char *>(value));
  xmlFree(value);
  return result;
}

QString attr(xmlNode *node, const char *name)
{
  return takeText(xmlGetProp(node, BAD_CAST name));
}

bool named(xmlNode *node, const char *name)
{
  return node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name);
}

bool feedHint(const QString &value)
{
  static const QRegularExpression hint("(^|[^a-z])(rss|atom)($|[^a-z])",
                                       QRegularExpression::CaseInsensitiveOption);
  return hint.match(value).hasMatch();
}

// libxml2 bounds the nesting depth. Each node is visited once, including
// descendants of anchors (e.g. an RSS icon whose alt supplies the label).
struct Links {
  QString base;
  QStringList hrefs;
  bool truncated = false;

  void walk(xmlNode *node, bool *anchorHint = nullptr)
  {
    for (; node; node = node->next) {
      if (named(node, "script") || named(node, "style") || named(node, "template")) continue;
      if (anchorHint && !*anchorHint) {
        if (node->type == XML_TEXT_NODE)
          *anchorHint = feedHint(QString::fromUtf8(reinterpret_cast<const char *>(node->content)));
        else if (named(node, "img")) *anchorHint = feedHint(attr(node, "alt"));
      }
      if (node->type != XML_ELEMENT_NODE) continue;
      if (base.isEmpty() && named(node, "base")) base = attr(node, "href");
      const bool anchor = named(node, "a");
      bool match = false;
      if (anchor) {
        match = feedHint(attr(node, "href")) || feedHint(attr(node, "title")) ||
                feedHint(attr(node, "aria-label")) || feedHint(attr(node, "alt"));
      } else if (named(node, "link")) {
        const QString type = attr(node, "type").section(';', 0, 0).trimmed().toLower();
        match = (type == "application/rss+xml" || type == "application/atom+xml") &&
            attr(node, "rel").simplified().toLower().split(' ').contains("alternate");
      }
      walk(node->children, anchor ? &match : anchorHint);
      if (match) {
        const QString href = attr(node, "href").trimmed();
        if (href.isEmpty() || hrefs.contains(href)) continue;
        if (hrefs.size() < 32) hrefs.append(href);
        else truncated = true;
      }
    }
  }
};
}

FeedDiscovery::FeedDiscovery(const QByteArray &html, const QString &charset, const QUrl &page,
                             int scanId, QWidget *parent) : QDialog(parent), scanId_(scanId)
{
  setWindowTitle(tr("Discover Feeds"));
  resize(680, 340);
  auto *layout = new QVBoxLayout(this);
  status_ = new QLabel(tr("Looking for feeds…"));
  layout->addWidget(status_);
  list_ = new QTreeWidget;
  list_->setRootIsDecorated(false);
  list_->setHeaderLabels({tr("Feed"), tr("URL")});
  layout->addWidget(list_);
  buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
  auto *nextButton = buttons_->button(QDialogButtonBox::Ok);
  nextButton->setText(tr("Next"));
  nextButton->setEnabled(false);
  auto *all = buttons_->addButton(tr("Select all"), QDialogButtonBox::ActionRole);
  connect(all, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
      auto *item = list_->topLevelItem(i);
      if (!item->isDisabled()) item->setCheckState(0, Qt::Checked);
    }
  });
  connect(list_, &QTreeWidget::itemChanged, this, [this, nextButton] {
    nextButton->setEnabled(finished_ && !selection().isEmpty());
  });
  connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons_);

  constexpr int htmlLimit = 2 * 1024 * 1024;
  incomplete_ = html.size() > htmlLimit;
  const QByteArray encoding = charset.trimmed().toLatin1();
  htmlDocPtr doc = htmlReadMemory(html.constData(), int(qMin<qint64>(html.size(), htmlLimit)),
      nullptr, encoding.isEmpty() ? nullptr : encoding.constData(),
      HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER);
  Links links;
  if (doc) {
    links.walk(xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
  } else incomplete_ = true;
  incomplete_ |= links.truncated;
  QUrl base = page.resolved(QUrl(links.base));
  if (!NetworkPolicy::isHttpUrl(base)) base = page;
  auto enqueue = [&](QUrl url) {
    url.setFragment(QString());
    if (!NetworkPolicy::isHttpUrl(url) || !url.userInfo().isEmpty()) return;
    // Only the page's own HTTP origin has already received consent.
    if (url.scheme() == "http" && CredentialScope::origin(url) != CredentialScope::origin(page))
      url = FeedUrl::upgrade(url);
    if (url != page && !pending_.contains(url)) pending_.append(url);
  };
  for (const QString &href : std::as_const(links.hrefs)) enqueue(base.resolved(QUrl(href)));
  for (const char *path : {"/feed/", "/rss.xml", "/atom.xml", "/feed.xml"})
    enqueue(page.resolved(QUrl(QString::fromLatin1(path))));
  // Automatic probes contact only the page origin. Ask before contacting
  // additional origins advertised in untrusted HTML.
  const QUrl origin = CredentialScope::origin(page);
  QStringList hosts;
  for (const QUrl &url : std::as_const(pending_)) {
    const QUrl other = CredentialScope::origin(url);
    if (other != origin && !hosts.contains(other.toString())) hosts.append(other.toString());
  }
  QTimer::singleShot(0, this, [this, origin, hosts] {
    if (!hosts.isEmpty() && QMessageBox::question(this, tr("Discover Feeds"),
        tr("Also check feed links on these sites?\n\n%1").arg(hosts.join('\n')),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      for (auto it = pending_.begin(); it != pending_.end();) {
        if (CredentialScope::origin(*it) != origin) it = pending_.erase(it);
        else ++it;
      }
    }
    if (!finished_ && isVisible()) next();
  });
  QTimer::singleShot(60000, this, [this] {
    if (!finished_) { emit stopRequests(); incomplete_ = true; finishScan(); }
  });
}

QStringList FeedDiscovery::selection() const
{
  QStringList urls;
  for (int i = 0; i < list_->topLevelItemCount(); ++i) {
    auto *item = list_->topLevelItem(i);
    if (!item->isDisabled() && item->checkState(0) == Qt::Checked) urls.append(item->text(1));
  }
  return urls;
}

void FeedDiscovery::next()
{
  if (pending_.isEmpty()) { finishScan(); return; }
  emit requestUrl(scanId_, pending_.takeFirst().toString(), {}, {});
}

void FeedDiscovery::received(int result, int id, QString, QString, QByteArray data,
                             QDateTime, QString, QUrl effective)
{
  if (finished_ || id != scanId_) return;
  if (result < 0 && result != -5) incomplete_ = true;
  QDomDocument doc;
  QString error;
  int line, column;
  if (result >= 0 && FeedXml::parse(doc, data, error, line, column) &&
      FeedXml::isFeed(doc) && !found_.contains(effective)) {
    found_.insert(effective);
    QDomElement container = doc.documentElement();
    if (container.tagName() != "feed") container = container.firstChildElement("channel");
    QString title = container.firstChildElement("title").text().simplified();
    QSqlQuery q;
    q.prepare("SELECT id FROM feeds WHERE xmlUrl=?");
    q.addBindValue(effective.toString());
    bool subscribed = q.exec() && q.next();
    if (subscribed) title += tr(" (Already subscribed)");
    auto *item = new QTreeWidgetItem(list_, {title, effective.toString()});
    item->setCheckState(0, Qt::Unchecked);
    item->setDisabled(subscribed);
  }
  next();
}

void FeedDiscovery::finishScan()
{
  finished_ = true;
  status_->setText(incomplete_ ? tr("Scan incomplete. Select from the feeds found.") :
                   list_->topLevelItemCount() ? tr("Select feeds to add.") : tr("No feeds found."));
  buttons_->button(QDialogButtonBox::Ok)->setEnabled(!selection().isEmpty());
  if (!incomplete_ && list_->topLevelItemCount() == 1 && !list_->topLevelItem(0)->isDisabled()) {
    list_->topLevelItem(0)->setCheckState(0, Qt::Checked);
    accept();
  }
}

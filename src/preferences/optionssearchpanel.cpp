// SPDX-License-Identifier: GPL-3.0-or-later
#include "optionssearchpanel.h"

#include <QAbstractAnimation>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

namespace {
class HighlightOverlay final : public QWidget
{
public:
  HighlightOverlay(QWidget *parent, const QRect &rect)
    : QWidget(parent)
  {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(rect);
    show();
    raise();

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(1100);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
      opacity_ = v.toReal();
      update();
    });
    connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor base = palette().color(QPalette::Highlight);
    QColor fill = base;
    fill.setAlphaF(0.18 * opacity_);
    QColor border = base;
    border.setAlphaF(0.85 * opacity_);
    const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    p.setBrush(fill);
    p.setPen(QPen(border, 2.0));
    p.drawRoundedRect(r, 4.0, 4.0);
  }

private:
  qreal opacity_ = 1.0;
};
} // namespace

OptionsSearchPanel::OptionsSearchPanel(QWidget *parent)
  : QWidget(parent)
{
  resultsTree_ = new QTreeWidget(this);
  resultsTree_->setObjectName(QStringLiteral("searchResultsTree_"));
  resultsTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  resultsTree_->setRootIsDecorated(false);
  resultsTree_->setUniformRowHeights(true);
  resultsTree_->setAlternatingRowColors(true);
  resultsTree_->setColumnCount(2);
  resultsTree_->setHeaderHidden(true);
  resultsTree_->header()->setStretchLastSection(false);
  resultsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  resultsTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  emptyLabel_ = new QLabel(
      QCoreApplication::translate("OptionsDialog", "No matching settings found."), this);
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->hide();

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(resultsTree_, 1);
  layout->addWidget(emptyLabel_, 1);

  connect(resultsTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
    if (!item) return;
    const QVariant idxVar = item->data(0, Qt::UserRole);
    if (idxVar.isValid()) emit entryActivated(idxVar.toInt());
  });
}

void OptionsSearchPanel::updateResults(const QString &query,
                                       const QVector<OptionSearchEntry> &entries,
                                       const SectionLookup &sectionForEntry)
{
  const QStringList tokens =
      query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  struct RankedMatch {
    int rank;
    int index;
    QString section;
  };
  QVector<RankedMatch> matches;
  matches.reserve(entries.size());

  for (int i = 0; i < entries.size(); ++i) {
    const OptionSearchEntry &entry = entries.at(i);
    const QString section = sectionForEntry ? sectionForEntry(entry) : QString();
    const QString haystack =
        QStringList{entry.title, section, entry.tooltip, entry.keywords}.join(QLatin1Char(' '));

    bool allMatch = true;
    for (const QString &tok : tokens) {
      if (!haystack.contains(tok, Qt::CaseInsensitive)) {
        allMatch = false;
        break;
      }
    }
    if (!allMatch) continue;

    int rank = 2;
    if (entry.isShortcut) {
      rank = 3;
    } else if (entry.title.contains(query, Qt::CaseInsensitive)) {
      rank = 0;
    } else {
      bool allInTitle = true;
      for (const QString &tok : tokens) {
        if (!entry.title.contains(tok, Qt::CaseInsensitive)) {
          allInTitle = false;
          break;
        }
      }
      if (allInTitle) rank = 1;
    }
    matches.append({rank, i, section});
  }

  std::stable_sort(matches.begin(), matches.end(), [](const RankedMatch &a, const RankedMatch &b) {
    return a.rank < b.rank;
  });

  resultsTree_->clear();
  for (const RankedMatch &m : matches) {
    const OptionSearchEntry &entry = entries.at(m.index);
    auto *item = new QTreeWidgetItem({entry.title, m.section});
    item->setData(0, Qt::UserRole, m.index);
    if (!entry.tooltip.isEmpty()) item->setToolTip(0, entry.tooltip);
    QFont sectionFont = item->font(1);
    sectionFont.setItalic(true);
    item->setFont(1, sectionFont);
    item->setForeground(1, palette().color(QPalette::Disabled, QPalette::Text));
    resultsTree_->addTopLevelItem(item);
  }

  const bool any = !matches.isEmpty();
  resultsTree_->setVisible(any);
  emptyLabel_->setVisible(!any);
  if (any) resultsTree_->setCurrentItem(resultsTree_->topLevelItem(0));
}

void OptionsSearchPanel::clear()
{
  resultsTree_->clear();
}

bool OptionsSearchPanel::hasResults() const
{
  return resultsTree_->isVisible() && resultsTree_->topLevelItemCount() > 0;
}

int OptionsSearchPanel::currentEntryIndex() const
{
  QTreeWidgetItem *item = resultsTree_->currentItem()
      ? resultsTree_->currentItem()
      : resultsTree_->topLevelItem(0);
  if (!item) return -1;
  const QVariant idxVar = item->data(0, Qt::UserRole);
  return idxVar.isValid() ? idxVar.toInt() : -1;
}

void OptionsSearchPanel::focusFirstResult()
{
  if (!hasResults()) return;
  resultsTree_->setFocus();
  resultsTree_->setCurrentItem(resultsTree_->topLevelItem(0));
}

void OptionsSearchPanel::highlightWidget(QWidget *target, QScrollArea *scrollArea)
{
  if (!target || !scrollArea) return;
  if (highlightOverlay_) {
    highlightOverlay_->deleteLater();
    highlightOverlay_ = nullptr;
  }
  QWidget *viewport = scrollArea->viewport();
  if (!viewport || !target->isVisibleTo(viewport)) return;

  QRect rect(target->mapTo(viewport, QPoint(0, 0)), target->size());
  rect = rect.adjusted(-4, -2, 4, 2).intersected(viewport->rect());
  if (rect.isEmpty()) return;

  highlightOverlay_ = new HighlightOverlay(viewport, rect);
}

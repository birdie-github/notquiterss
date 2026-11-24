// SPDX-License-Identifier: GPL-3.0-or-later
#include "optionspagebuilder.h"

#include "widgets/lineedit.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {
void appendSearchEntry(QVector<OptionSearchEntry> *entries, int page, int subTab,
                       QWidget *target, const QString &title,
                       const QString &tooltip, const QString &keywords)
{
  if (!entries || !target || title.isEmpty()) return;
  entries->append({page, subTab, title, tooltip, keywords, target});
}

QCheckBox *makeCheckBox(QWidget *parent, const QString &text,
                        const QString &tooltip)
{
  auto *box = new QCheckBox(text, parent);
  if (!tooltip.isEmpty()) box->setToolTip(tooltip);
  return box;
}

QSpinBox *makeSpinBox(QWidget *parent, int min, int max,
                      const QString &suffix, const QString &tooltip)
{
  auto *spin = new QSpinBox(parent);
  spin->setRange(min, max);
  if (!suffix.isEmpty()) spin->setSuffix(suffix);
  if (!tooltip.isEmpty()) spin->setToolTip(tooltip);
  return spin;
}
} // namespace

OptionsRowBuilder::OptionsRowBuilder(QWidget *parent, QHBoxLayout *layout,
                                     int page, int subTab,
                                     QVector<OptionSearchEntry> *entries)
  : parent_(parent)
  , layout_(layout)
  , page_(page)
  , subTab_(subTab)
  , entries_(entries)
{
}

QLabel *OptionsRowBuilder::label(const QString &text)
{
  auto *lbl = new QLabel(text, parent_);
  layout_->addWidget(lbl);
  return lbl;
}

QCheckBox *OptionsRowBuilder::check(const QString &text, const QString &tooltip,
                                    const QString &keywords)
{
  auto *box = makeCheckBox(parent_, text, tooltip);
  layout_->addWidget(box);
  registerSearch(box, text, tooltip, keywords);
  return box;
}

QCheckBox *OptionsRowBuilder::check(QCheckBox *&out, const QString &text,
                                    const QString &tooltip,
                                    const QString &keywords)
{
  out = check(text, tooltip, keywords);
  return out;
}

QSpinBox *OptionsRowBuilder::spin(QSpinBox *&out, int min, int max,
                                  const QString &suffix,
                                  const QString &tooltip)
{
  out = makeSpinBox(parent_, min, max, suffix, tooltip);
  layout_->addWidget(out);
  return out;
}

QComboBox *OptionsRowBuilder::combo(QComboBox *&out, const QStringList &items)
{
  out = new QComboBox(parent_);
  if (!items.isEmpty()) out->addItems(items);
  layout_->addWidget(out);
  return out;
}

LineEdit *OptionsRowBuilder::lineEdit(LineEdit *&out, const QString &placeholder,
                                      const QString &tooltip)
{
  out = new LineEdit(parent_);
  if (!placeholder.isEmpty()) out->setPlaceholderText(placeholder);
  if (!tooltip.isEmpty()) out->setToolTip(tooltip);
  layout_->addWidget(out);
  return out;
}

QPushButton *OptionsRowBuilder::button(const QString &text)
{
  auto *btn = new QPushButton(text, parent_);
  layout_->addWidget(btn);
  return btn;
}

void OptionsRowBuilder::spacing(int px)
{
  layout_->addSpacing(px);
}

void OptionsRowBuilder::stretch(int factor)
{
  layout_->addStretch(factor);
}

void OptionsRowBuilder::addWidget(QWidget *widget, int stretch,
                                  Qt::Alignment alignment)
{
  layout_->addWidget(widget, stretch, alignment);
}

void OptionsRowBuilder::registerSearch(QWidget *target, const QString &title,
                                       const QString &tooltip,
                                       const QString &keywords)
{
  appendSearchEntry(entries_, page_, subTab_, target, title, tooltip, keywords);
}

OptionsGridBuilder::OptionsGridBuilder(QWidget *parent, QGridLayout *layout,
                                       int page, int subTab,
                                       QVector<OptionSearchEntry> *entries)
  : parent_(parent)
  , layout_(layout)
  , page_(page)
  , subTab_(subTab)
  , entries_(entries)
{
}

QSpinBox *OptionsGridBuilder::addSpinRow(int row, const QString &labelText,
                                         QSpinBox *&out, int min, int max,
                                         const QString &suffix,
                                         const QString &tooltip,
                                         const QString &keywords)
{
  layout_->addWidget(new QLabel(labelText, parent_), row, 0);
  out = makeSpinBox(parent_, min, max, QString(), tooltip);
  if (suffix.isEmpty()) {
    layout_->addWidget(out, row, 1, 1, 1, Qt::AlignLeft);
  } else {
    layout_->addWidget(out, row, 1);
    layout_->addWidget(new QLabel(suffix, parent_), row, 2);
  }
  appendSearchEntry(entries_, page_, subTab_, out, labelText, tooltip, keywords);
  return out;
}

void OptionsGridBuilder::addWidget(int row, int col, QWidget *widget,
                                   Qt::Alignment alignment)
{
  if (alignment == Qt::Alignment()) {
    layout_->addWidget(widget, row, col);
  } else {
    layout_->addWidget(widget, row, col, 1, 1, alignment);
  }
}

void OptionsGridBuilder::addLayout(int row, int col, QLayout *layout,
                                   Qt::Alignment alignment)
{
  if (alignment == Qt::Alignment()) {
    layout_->addLayout(layout, row, col);
  } else {
    layout_->addLayout(layout, row, col, 1, 1, alignment);
  }
}

QCheckBox *OptionsGridBuilder::check(int row, int col, QCheckBox *&out,
                                     const QString &text,
                                     const QString &tooltip,
                                     const QString &keywords)
{
  out = makeCheckBox(parent_, text, tooltip);
  layout_->addWidget(out, row, col);
  appendSearchEntry(entries_, page_, subTab_, out, text, tooltip, keywords);
  return out;
}

QRadioButton *OptionsGridBuilder::radio(int row, int col, QRadioButton *&out,
                                        const QString &text,
                                        const QString &keywords)
{
  out = new QRadioButton(text, parent_);
  layout_->addWidget(out, row, col);
  appendSearchEntry(entries_, page_, subTab_, out, text, QString(), keywords);
  return out;
}

OptionsPageBuilder::OptionsPageBuilder(QWidget *container, QBoxLayout *layout,
                                       int page, int subTab,
                                       QVector<OptionSearchEntry> *entries)
  : container_(container)
  , layout_(layout)
  , page_(page)
  , subTab_(subTab)
  , entries_(entries)
{
}

QFrame *OptionsPageBuilder::buildFrame(int page,
                                       QVector<OptionSearchEntry> *entries,
                                       const PageFn &fn)
{
  auto *frame = new QFrame();
  frame->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *layout = new QVBoxLayout(frame);
  OptionsPageBuilder builder(frame, layout, page, -1, entries);
  fn(builder);
  return frame;
}

QWidget *OptionsPageBuilder::buildWidget(int page, int subTab,
                                         QVector<OptionSearchEntry> *entries,
                                         const PageFn &fn)
{
  auto *widget = new QWidget();
  auto *layout = new QVBoxLayout(widget);
  OptionsPageBuilder builder(widget, layout, page, subTab, entries);
  fn(builder);
  return widget;
}

QTabWidget *OptionsPageBuilder::buildTabs(
    int page, QVector<OptionSearchEntry> *entries,
    const QList<QPair<QString, PageFn>> &tabs)
{
  auto *tabWidget = new QTabWidget();
  for (int i = 0; i < tabs.size(); ++i) {
    tabWidget->addTab(buildWidget(page, i, entries, tabs.at(i).second),
                      tabs.at(i).first);
  }
  return tabWidget;
}

void OptionsPageBuilder::bindEnabled(QAbstractButton *toggle,
                                     std::initializer_list<QWidget *> dependents)
{
  const bool enabled = toggle->isChecked();
  for (QWidget *w : dependents) {
    w->setEnabled(enabled);
    QObject::connect(toggle, &QAbstractButton::toggled, w, &QWidget::setEnabled);
  }
}

QCheckBox *OptionsPageBuilder::check(QCheckBox *&out, const QString &text,
                                     const QString &tooltip,
                                     const QString &keywords)
{
  out = makeCheckBox(container_, text, tooltip);
  layout_->addWidget(out);
  registerSearch(out, text, tooltip, keywords);
  return out;
}

QRadioButton *OptionsPageBuilder::radio(QRadioButton *&out, const QString &text,
                                        const QString &keywords)
{
  out = radio(text, keywords);
  return out;
}

QRadioButton *OptionsPageBuilder::radio(const QString &text,
                                        const QString &keywords)
{
  auto *btn = new QRadioButton(text, container_);
  layout_->addWidget(btn);
  registerSearch(btn, text, QString(), keywords);
  return btn;
}

QLabel *OptionsPageBuilder::label(const QString &text)
{
  auto *lbl = new QLabel(text, container_);
  layout_->addWidget(lbl);
  return lbl;
}

QSpinBox *OptionsPageBuilder::spinRow(QSpinBox *&out, const QString &prefix,
                                      int min, int max,
                                      const QString &trailingText,
                                      const QString &tooltip,
                                      const QString &keywords)
{
  row([&](OptionsRowBuilder &r) {
    r.label(prefix);
    r.spin(out, min, max, QString(), tooltip);
    if (!trailingText.isEmpty()) r.label(trailingText);
  }, QMargins(0, 0, 0, 0));
  const QString title = trailingText.isEmpty()
      ? prefix
      : (prefix + QLatin1Char(' ') + trailingText);
  registerSearch(out, title, tooltip, keywords);
  return out;
}

QComboBox *OptionsPageBuilder::comboRow(QComboBox *&out, const QString &labelText,
                                        const QStringList &items,
                                        const QString &keywords)
{
  row([&](OptionsRowBuilder &r) {
    r.label(labelText);
    r.combo(out, items);
  }, QMargins(0, 0, 0, 0));
  registerSearch(out, labelText, QString(), keywords);
  return out;
}

QPushButton *OptionsPageBuilder::pathRow(LineEdit *&out,
                                         const QString &labelText,
                                         const QString &buttonText,
                                         const QString &keywords)
{
  if (!labelText.isEmpty()) label(labelText);
  QPushButton *btn = nullptr;
  row([&](OptionsRowBuilder &r) {
    r.lineEdit(out);
    btn = r.button(buttonText);
  }, QMargins(0, 0, 0, 0), false);
  if (!labelText.isEmpty()) registerSearch(out, labelText, QString(), keywords);
  return btn;
}

QWidget *OptionsPageBuilder::indented(const QMargins &margins, const PageFn &fn,
                                      bool zeroSpacing)
{
  auto *box = new QWidget(container_);
  auto *subLayout = new QVBoxLayout(box);
  subLayout->setContentsMargins(margins);
  if (zeroSpacing) subLayout->setSpacing(0);
  OptionsPageBuilder sub(box, subLayout, page_, subTab_, entries_);
  fn(sub);
  layout_->addWidget(box);
  return box;
}

QGroupBox *OptionsPageBuilder::group(QGroupBox *&out, const QString &title,
                                     bool checkable, const PageFn &fn,
                                     Qt::Orientation orientation,
                                     std::optional<QMargins> margins,
                                     const QString &keywords)
{
  out = new QGroupBox(title, container_);
  out->setCheckable(checkable);
  QBoxLayout *grpLayout = orientation == Qt::Horizontal
      ? static_cast<QBoxLayout *>(new QHBoxLayout(out))
      : static_cast<QBoxLayout *>(new QVBoxLayout(out));
  if (margins.has_value()) grpLayout->setContentsMargins(*margins);
  OptionsPageBuilder sub(out, grpLayout, page_, subTab_, entries_);
  fn(sub);
  layout_->addWidget(out);
  if (checkable) registerSearch(out, title, QString(), keywords);
  return out;
}

QGroupBox *OptionsPageBuilder::group(const QString &title, const PageFn &fn,
                                     Qt::Orientation orientation,
                                     std::optional<QMargins> margins)
{
  QGroupBox *unused = nullptr;
  return group(unused, title, false, fn, orientation, margins);
}

QHBoxLayout *OptionsPageBuilder::row(const RowFn &fn,
                                     std::optional<QMargins> margins,
                                     bool addTrailingStretch)
{
  auto *rowLayout = new QHBoxLayout();
  if (margins.has_value()) rowLayout->setContentsMargins(*margins);
  OptionsRowBuilder rb(container_, rowLayout, page_, subTab_, entries_);
  fn(rb);
  if (addTrailingStretch) rowLayout->addStretch();
  layout_->addLayout(rowLayout);
  return rowLayout;
}

QGridLayout *OptionsPageBuilder::grid(std::optional<QMargins> margins,
                                      int stretchColumn, const GridFn &fn)
{
  auto *gridLayout = new QGridLayout();
  if (margins.has_value()) gridLayout->setContentsMargins(*margins);
  if (stretchColumn >= 0) gridLayout->setColumnStretch(stretchColumn, 1);
  OptionsGridBuilder gb(container_, gridLayout, page_, subTab_, entries_);
  fn(gb);
  layout_->addLayout(gridLayout);
  return gridLayout;
}

void OptionsPageBuilder::spacing(int px)
{
  layout_->addSpacing(px);
}

void OptionsPageBuilder::stretch(int factor)
{
  layout_->addStretch(factor);
}

void OptionsPageBuilder::addWidget(QWidget *widget)
{
  layout_->addWidget(widget);
}

void OptionsPageBuilder::addLayout(QLayout *layout)
{
  layout_->addLayout(layout);
}

void OptionsPageBuilder::registerSearch(QWidget *target, const QString &title,
                                        const QString &tooltip,
                                        const QString &keywords)
{
  appendSearchEntry(entries_, page_, subTab_, target, title, tooltip, keywords);
}

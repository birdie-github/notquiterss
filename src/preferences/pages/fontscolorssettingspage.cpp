// SPDX-License-Identifier: GPL-3.0-or-later
#include "fontscolorssettingspage.h"

#include "app/mainapplication.h"
#include "mainwindow/mainwindow.h"
#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"

#include <QApplication>
#include <QColorDialog>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

namespace {
class ColorSwatchDelegate final : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
  {
    return QStyledItemDelegate::sizeHint(option, index) + QSize(20, 8);
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override
  {
    QStyleOptionViewItem cell(option);
    initStyleOption(&cell, index);
    const QString label = cell.text;
    cell.text.clear();
    const QStyle *style = cell.widget ? cell.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, cell.widget);
    if (label.isEmpty()) return;

    const QRect swatch = cell.rect.adjusted(4, 3, -5, -4);
    if (swatch.isEmpty()) return;

    QColor fill = cell.palette.color(QPalette::Base);
    QColor ink = cell.palette.color(QPalette::Text);
    const QColor color(index.data(Qt::UserRole).toString());
    if (color.isValid()) {
      // Compute contrast against the visible color, including any transparency.
      const double alpha = color.alphaF();
      fill = QColor::fromRgbF(alpha * color.redF() + (1 - alpha) * fill.redF(),
                             alpha * color.greenF() + (1 - alpha) * fill.greenF(),
                             alpha * color.blueF() + (1 - alpha) * fill.blueF());
      const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92
                                 : std::pow((channel + 0.055) / 1.055, 2.4);
      };
      const double luminance = 0.2126 * linear(fill.redF()) + 0.7152 * linear(fill.greenF())
                             + 0.0722 * linear(fill.blueF());
      const double blackContrast = (luminance + 0.05) / 0.05;
      const double whiteContrast = 1.05 / (luminance + 0.05);
      ink = blackContrast >= whiteContrast ? Qt::black : Qt::white;
    }

    painter->save();
    painter->fillRect(swatch, fill);
    painter->setPen(cell.palette.color(QPalette::Mid));
    painter->drawRect(swatch.adjusted(0, 0, -1, -1));
    painter->setFont(cell.font);
    painter->setPen(ink);
    painter->drawText(swatch, Qt::AlignCenter, label);
    painter->restore();
  }
};
} // namespace

FontsColorsSettingsPage::FontsColorsSettingsPage(QWidget *parent,
                                                 QVector<OptionSearchEntry> *searchEntries,
                                                 int pageIndex)
  : QTabWidget(parent)
{
  //! tab "Fonts"
  fontsTree_ = new QTreeWidget(this);
  fontsTree_->setObjectName(QStringLiteral("fontTree"));
  fontsTree_->setColumnCount(3);
  fontsTree_->setColumnHidden(0, true);
  fontsTree_->setColumnWidth(1, 260);
  fontsTree_->setHeaderLabels({QStringLiteral("Id"), OptionsDialog::tr("Type"), OptionsDialog::tr("Font")});

  static const char *const kFontLabels[] = {
    QT_TRANSLATE_NOOP("OptionsDialog", "Feed list font"),
    QT_TRANSLATE_NOOP("OptionsDialog", "Article list font"),
    QT_TRANSLATE_NOOP("OptionsDialog", "Article title font"),
    QT_TRANSLATE_NOOP("OptionsDialog", "Article text font"),
    QT_TRANSLATE_NOOP("OptionsDialog", "Notification font"),
  };
  for (int i = 0; i < int(sizeof(kFontLabels) / sizeof(kFontLabels[0])); ++i) {
    const QString fontTitle = OptionsDialog::tr(kFontLabels[i]);
    fontsTree_->addTopLevelItem(
        new QTreeWidgetItem(QStringList{QString::number(i), fontTitle}));
    if (searchEntries && pageIndex >= 0) {
      searchEntries->append({
          pageIndex, 0, fontTitle, QString(),
          OptionsDialog::tr("Fonts") + QStringLiteral(" typography size family"),
          this, i});
    }
  }

  fontsTree_->setCurrentItem(fontsTree_->topLevelItem(0));
  connect(fontsTree_, &QAbstractItemView::doubleClicked,
          this, &FontsColorsSettingsPage::slotFontChange);

  auto *fontChange = new QPushButton(OptionsDialog::tr("Change..."), this);
  connect(fontChange, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotFontChange);
  auto *fontReset = new QPushButton(OptionsDialog::tr("Reset"), this);
  connect(fontReset, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotFontReset);
  auto *fontsButtonLayout = new QVBoxLayout();
  fontsButtonLayout->addWidget(fontChange);
  fontsButtonLayout->addWidget(fontReset);
  fontsButtonLayout->addStretch(1);
  auto *fontResetAll = new QPushButton(OptionsDialog::tr("Reset All"), this);
  connect(fontResetAll, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotFontResetAll);
  fontsButtonLayout->addWidget(fontResetAll);

  auto *fontsWidget = new QWidget(this);
  auto *mainFontsLayout = new QHBoxLayout(fontsWidget);
  mainFontsLayout->addWidget(fontsTree_, 1);
  mainFontsLayout->addLayout(fontsButtonLayout);

  //! tab "Colors"
  colorsTree_ = new QTreeWidget(this);
  colorsTree_->setObjectName(QStringLiteral("colorsTree_"));
  colorsTree_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  colorsTree_->setRootIsDecorated(false);
  colorsTree_->setColumnCount(2);
  colorsTree_->setHeaderHidden(true);
  colorsTree_->header()->setStretchLastSection(false);
  colorsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  colorsTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  colorsTree_->setItemDelegateForColumn(1, new ColorSwatchDelegate(colorsTree_));

  // Option IDs match the theme bindings and are independent of display order.
  static const struct {
    int optionId;
    const char *label;
  } entries[] = {
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Article")},
    {10, QT_TRANSLATE_NOOP("OptionsDialog", "Article text color")},
    {12, QT_TRANSLATE_NOOP("OptionsDialog", "Article background")},
    { 2, QT_TRANSLATE_NOOP("OptionsDialog", "Article list text color")},
    { 3, QT_TRANSLATE_NOOP("OptionsDialog", "Article list background")},
    { 4, QT_TRANSLATE_NOOP("OptionsDialog", "Focused article text color")},
    { 5, QT_TRANSLATE_NOOP("OptionsDialog", "Focused article background")},
    {15, QT_TRANSLATE_NOOP("OptionsDialog", "New article text color")},
    {16, QT_TRANSLATE_NOOP("OptionsDialog", "Unread article text color")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Author")},
    { 9, QT_TRANSLATE_NOOP("OptionsDialog", "Author color")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Date")},
    { 8, QT_TRANSLATE_NOOP("OptionsDialog", "Date color")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Feed")},
    { 0, QT_TRANSLATE_NOOP("OptionsDialog", "Feed list text color")},
    { 1, QT_TRANSLATE_NOOP("OptionsDialog", "Feeds list background")},
    {17, QT_TRANSLATE_NOOP("OptionsDialog", "Focused feed color")},
    {18, QT_TRANSLATE_NOOP("OptionsDialog", "Focused feed background color")},
    {13, QT_TRANSLATE_NOOP("OptionsDialog", "Feed with new articles")},
    {14, QT_TRANSLATE_NOOP("OptionsDialog", "Unread article count in feed tree")},
    {19, QT_TRANSLATE_NOOP("OptionsDialog", "Disabled feed")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Link")},
    { 6, QT_TRANSLATE_NOOP("OptionsDialog", "Link color")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Notification")},
    {21, QT_TRANSLATE_NOOP("OptionsDialog", "Notification text color")},
    {22, QT_TRANSLATE_NOOP("OptionsDialog", "Notification background color")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Row")},
    {20, QT_TRANSLATE_NOOP("OptionsDialog", "Alternating row colors")},
    {-1, QT_TRANSLATE_NOOP("OptionsDialog", "Title")},
    { 7, QT_TRANSLATE_NOOP("OptionsDialog", "Title color")},
    {11, QT_TRANSLATE_NOOP("OptionsDialog", "Article title background")},
  };
  colorOptionItems_.resize(MainWindow::kColorOptionCount);
  QTreeWidgetItem *firstColor = nullptr;
  QString currentColorSection;
  for (const auto &entry : entries) {
    const QString label = OptionsDialog::tr(entry.label);
    auto *item = new QTreeWidgetItem(colorsTree_, QStringList{label});
    if (entry.optionId < 0) {
      currentColorSection = label;
      item->setFlags(Qt::ItemIsEnabled);
      QFont headingFont = item->font(0);
      headingFont.setBold(true);
      item->setFont(0, headingFont);
    } else {
      item->setData(0, Qt::UserRole, entry.optionId);
      colorOptionItems_[entry.optionId] = item;
      if (!firstColor) firstColor = item;
      if (searchEntries && pageIndex >= 0) {
        searchEntries->append({
            pageIndex, 1, label, QString(),
            OptionsDialog::tr("Colors") + QLatin1Char(' ') + currentColorSection
                + QStringLiteral(" theme palette"),
            this, entry.optionId});
      }
    }
  }

  connect(colorsTree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item, int) {
    if (item->data(0, Qt::UserRole).isValid()) slotColorChange();
  });

  auto *colorChange = new QPushButton(OptionsDialog::tr("Change..."), this);
  connect(colorChange, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotColorChange);
  auto *colorReset = new QPushButton(OptionsDialog::tr("Reset"), this);
  connect(colorReset, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotColorReset);
  connect(colorsTree_, &QTreeWidget::currentItemChanged, this,
          [colorChange, colorReset](QTreeWidgetItem *item, QTreeWidgetItem *) {
    const bool editable = item && item->data(0, Qt::UserRole).isValid();
    colorChange->setEnabled(editable);
    colorReset->setEnabled(editable);
  });
  colorsTree_->setCurrentItem(firstColor);

  auto *colorsButtonLayout = new QVBoxLayout();
  colorsButtonLayout->addWidget(colorChange);
  colorsButtonLayout->addWidget(colorReset);
  colorsButtonLayout->addStretch(1);
  auto *colorResetAll = new QPushButton(OptionsDialog::tr("Reset All"), this);
  connect(colorResetAll, &QAbstractButton::clicked,
          this, &FontsColorsSettingsPage::slotColorResetAll);
  colorsButtonLayout->addWidget(colorResetAll);

  auto *colorsWidget = new QWidget(this);
  auto *colorsLayout = new QHBoxLayout(colorsWidget);
  colorsLayout->addWidget(colorsTree_);
  colorsLayout->addLayout(colorsButtonLayout);

  addTab(fontsWidget, OptionsDialog::tr("Fonts"));
  addTab(colorsWidget, OptionsDialog::tr("Colors"));
}

QFont FontsColorsSettingsPage::fontOptionValue(int optionId) const
{
  return fontsTree_->topLevelItem(optionId)->data(2, Qt::UserRole).value<QFont>();
}

void FontsColorsSettingsPage::setFontOptionValue(int optionId, const QFont &font)
{
  auto *item = fontsTree_->topLevelItem(optionId);
  const QString value = QStringLiteral("%1, %2").arg(font.family()).arg(font.pointSize());
  const int previewSize = qBound(9, font.pointSize(), 18);
  item->setData(2, Qt::UserRole, QVariant::fromValue(font));
  item->setText(2, value);
  item->setFont(2, QFont(font.family(), previewSize));
  item->setToolTip(2, previewSize == font.pointSize()
                       ? value
                       : OptionsDialog::tr("%1 (preview shown at %2 pt)").arg(value).arg(previewSize));
}

QString FontsColorsSettingsPage::colorOptionValue(int optionId) const
{
  return colorOptionItems_.at(optionId)->data(1, Qt::UserRole).toString();
}

void FontsColorsSettingsPage::setColorOptionValue(int optionId, const QString &colorName)
{
  auto *item = colorOptionItems_.at(optionId);
  const QColor color(colorName);
  item->setData(1, Qt::UserRole, colorName);
  if (color.isValid()) {
    const auto format = color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb;
    item->setText(1, color.name(format).toUpper());
  } else {
    item->setText(1, colorName.isEmpty() ? OptionsDialog::tr("None") : colorName);
  }
}

void FontsColorsSettingsPage::selectFontItem(int row)
{
  if (row < 0 || row >= fontsTree_->topLevelItemCount()) return;
  QTreeWidgetItem *item = fontsTree_->topLevelItem(row);
  fontsTree_->setCurrentItem(item);
  fontsTree_->scrollToItem(item);
  fontsTree_->setFocus();
}

void FontsColorsSettingsPage::selectColorOption(int optionId)
{
  if (optionId < 0 || optionId >= colorOptionItems_.size()) return;
  if (QTreeWidgetItem *item = colorOptionItems_.at(optionId)) {
    colorsTree_->setCurrentItem(item);
    colorsTree_->scrollToItem(item);
    colorsTree_->setFocus();
  }
}

void FontsColorsSettingsPage::slotFontChange()
{
  const auto *item = fontsTree_->currentItem();
  if (!item) return;
  const int optionId = item->text(0).toInt();
  const QFont current = fontOptionValue(optionId);
  bool accepted = false;
  const QFont font = QFontDialog::getFont(&accepted, QFont(current.family(), current.pointSize()), this);
  if (accepted) setFontOptionValue(optionId, font);
}

void FontsColorsSettingsPage::resetFontOption(QTreeWidgetItem *item)
{
  const int optionId = item->text(0).toInt();
  setFontOptionValue(optionId, MainWindow::defaultFontForOption(optionId));
}

void FontsColorsSettingsPage::slotFontReset()
{
  if (auto *item = fontsTree_->currentItem()) {
    resetFontOption(item);
  }
}

void FontsColorsSettingsPage::slotFontResetAll()
{
  if (QMessageBox::question(this, OptionsDialog::tr("Reset All"),
                            OptionsDialog::tr("Reset all font settings to their default values?"),
                            QMessageBox::Ok | QMessageBox::Cancel,
                            QMessageBox::Cancel) != QMessageBox::Ok) return;

  for (int row = 0; row < fontsTree_->topLevelItemCount(); ++row) {
    resetFontOption(fontsTree_->topLevelItem(row));
  }
}

void FontsColorsSettingsPage::slotColorChange()
{
  const auto *item = colorsTree_->currentItem();
  if (!item || !item->data(0, Qt::UserRole).isValid()) return;
  const int optionId = item->data(0, Qt::UserRole).toInt();
  QColorDialog colorDialog(QColor(colorOptionValue(optionId)), this);
  if (colorDialog.exec() == QDialog::Accepted) {
    setColorOptionValue(optionId, colorDialog.selectedColor().name());
  }
}

void FontsColorsSettingsPage::slotColorReset()
{
  const auto *item = colorsTree_->currentItem();
  if (!item || !item->data(0, Qt::UserRole).isValid()) return;
  const int optionId = item->data(0, Qt::UserRole).toInt();
  setColorOptionValue(optionId, mainApp->mainWindow()->defaultColorForOption(optionId));
}

void FontsColorsSettingsPage::slotColorResetAll()
{
  if (QMessageBox::question(this, OptionsDialog::tr("Reset All"),
                            OptionsDialog::tr("Reset all color settings to their default values?"),
                            QMessageBox::Ok | QMessageBox::Cancel,
                            QMessageBox::Cancel) != QMessageBox::Ok) return;

  const auto *mainWindow = mainApp->mainWindow();
  for (int optionId = 0; optionId < colorOptionItems_.size(); ++optionId) {
    setColorOptionValue(optionId, mainWindow->defaultColorForOption(optionId));
  }
}

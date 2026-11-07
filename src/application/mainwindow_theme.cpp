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
#include "mainwindow.h"
#include "applicationstyle.h"
#include "mainapplication.h"
#include "settings.h"
#include <algorithm>

namespace {
QColor mixColor(const QColor &a, const QColor &b, int aPercent)
{
  const int bPercent = 100 - aPercent;
  return QColor((a.red() * aPercent + b.red() * bPercent) / 100,
                (a.green() * aPercent + b.green() * bPercent) / 100,
                (a.blue() * aPercent + b.blue() * bPercent) / 100);
}
}

/** @brief Set application style
 *---------------------------------------------------------------------------*/
void MainWindow::rebuildStyleMenu()
{
  const auto styles = mainApp->applicationStyles();
  const QString activeId = mainApp->applicationStyle().id;
  bool available = false;
  for (const ApplicationStyle &style : styles) available = available || style.id == activeId;
  if (!available) {
    qWarning() << "Selected application style no longer available:" << activeId;
    mainApp->applyApplicationStyle(ApplicationStyles::automaticId());
    // This is a fallback, not an explicit user theme choice. Preserve color
    // overrides rather than discarding them without the confirmation dialog.
    applyThemeColors(false);
  }
  // Actions belong only to this menu/group, never to toolbar customization.
  styleMenu_->clear();
  const auto oldActions = styleGroup_->actions();
  for (QAction *action : oldActions) delete action;

  auto addStyleAction = [this](const ApplicationStyle &style) {
    QAction *action = new QAction(
          QCoreApplication::translate("MainWindow", style.name.toUtf8().constData()), styleGroup_);
    action->setObjectName(style.id);
    action->setData(style.id);
    action->setCheckable(true);
    action->setChecked(style.id == mainApp->applicationStyle().id);
    styleGroup_->addAction(action);
    styleMenu_->addAction(action);
  };

  QList<ApplicationStyle> otherStyles;
  for (const ApplicationStyle &style : styles) {
    if (style.builtIn) addStyleAction(style);
    else otherStyles.append(style);
  }
  std::sort(otherStyles.begin(), otherStyles.end(),
            [](const ApplicationStyle &a, const ApplicationStyle &b) {
              return QString::localeAwareCompare(a.name, b.name) < 0;
            });

  if (!otherStyles.isEmpty() && !styleMenu_->actions().isEmpty())
    styleMenu_->addSeparator();
  for (const ApplicationStyle &style : otherStyles) addStyleAction(style);
}

void MainWindow::setStyleApp(QAction *action)
{
  const QString id = action->data().toString();
  if (id == mainApp->applicationStyle().id) return;

  if (!customColorRows_.isEmpty()) {
    const int answer = QMessageBox::question(
          this, tr("Change application theme"),
          tr("Changing the application theme will reset your custom colors. Continue?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      for (QAction *candidate : styleGroup_->actions())
        candidate->setChecked(candidate->data().toString() == mainApp->applicationStyle().id);
      return;
    }
  }

  mainApp->applyApplicationStyle(id);
  applyThemeColors(true);
  for (QAction *candidate : styleGroup_->actions())
    candidate->setChecked(candidate->data().toString() == mainApp->applicationStyle().id);
}

struct MainWindow::ThemeBindings {
  enum DefaultColorKind {
    PaletteText,
    EmptyBackground,
    PaletteHighlightedText,
    PaletteHighlight,
    PaletteLink,
    MutedText,
    PaletteBase,
    PaletteAlternateBase,
    PaletteWindowText,
    PaletteWindow,
  };

  struct ColorSpec {
    const char *key;
    QString MainWindow::*mwMember;
    QString FeedsModel::*fmMember;
    DefaultColorKind defaultKind;
  };

  struct FontSpec {
    const char *familyKey;
    const char *sizeKey;
    QString MainWindow::*familyMember;
    int MainWindow::*sizeMember;
    int defaultSizeDelta;
  };

  static const ColorSpec kColors[MainWindow::kColorOptionCount];
  static const FontSpec kFonts[MainWindow::kFontOptionCount];
};

const MainWindow::ThemeBindings::ColorSpec MainWindow::ThemeBindings::kColors[MainWindow::kColorOptionCount] = {
  { "feedsListTextColor",       nullptr,                                 &FeedsModel::textColor_,                PaletteText },
  { "feedsListBackgroundColor", nullptr,                                 &FeedsModel::backgroundColor_,          EmptyBackground },
  { "newsListTextColor",        &MainWindow::newsListTextColor_,         nullptr,                                PaletteText },
  { "newsListBackgroundColor",  &MainWindow::newsListBackgroundColor_,   nullptr,                                EmptyBackground },
  { "focusedNewsTextColor",     &MainWindow::focusedNewsTextColor_,      nullptr,                                PaletteHighlightedText },
  { "focusedNewsBGColor",       &MainWindow::focusedNewsBGColor_,        nullptr,                                PaletteHighlight },
  { "linkColor",                &MainWindow::linkColor_,                 nullptr,                                PaletteLink },
  { "titleColor",               &MainWindow::titleColor_,                nullptr,                                PaletteLink },
  { "dateColor",                &MainWindow::dateColor_,                 nullptr,                                MutedText },
  { "authorColor",              &MainWindow::authorColor_,               nullptr,                                MutedText },
  { "newsTextColor",            &MainWindow::newsTextColor_,             nullptr,                                PaletteText },
  { "newsTitleBackgroundColor", &MainWindow::newsTitleBackgroundColor_,  nullptr,                                PaletteBase },
  { "newsBackgroundColor",      &MainWindow::newsBackgroundColor_,       nullptr,                                PaletteBase },
  { "feedWithNewNewsColor",     nullptr,                                 &FeedsModel::feedWithNewNewsColor_,     PaletteLink },
  { "countNewsUnreadColor",     nullptr,                                 &FeedsModel::countNewsUnreadColor_,     PaletteLink },
  { "newNewsTextColor",         &MainWindow::newNewsTextColor_,          nullptr,                                PaletteText },
  { "unreadNewsTextColor",      &MainWindow::unreadNewsTextColor_,       nullptr,                                PaletteText },
  { "focusedFeedTextColor",     nullptr,                                 &FeedsModel::focusedFeedTextColor_,     PaletteHighlightedText },
  { "focusedFeedBGColor",       nullptr,                                 &FeedsModel::focusedFeedBGColor_,       PaletteHighlight },
  { "feedDisabledUpdateColor",  nullptr,                                 &FeedsModel::feedDisabledUpdateColor_,  MutedText },
  { "alternatingRowColors",     &MainWindow::alternatingRowColors_,      nullptr,                                PaletteAlternateBase },
  { "notifierTextColor",        &MainWindow::notifierTextColor_,         nullptr,                                PaletteWindowText },
  { "notifierBackgroundColor",  &MainWindow::notifierBackgroundColor_,   nullptr,                                PaletteWindow },
};

const MainWindow::ThemeBindings::FontSpec MainWindow::ThemeBindings::kFonts[MainWindow::kFontOptionCount] = {
  { "feedsFontFamily",        "feedsFontSize",        nullptr,                               nullptr,                              0 },
  { "newsFontFamily",         "newsFontSize",         &MainWindow::newsListFontFamily_,      &MainWindow::newsListFontSize_,       0 },
  { "newsTitleFontFamily",    "newsTitleFontSize",    &MainWindow::newsTitleFontFamily_,     &MainWindow::newsTitleFontSize_,      2 },
  { "newsTextFontFamily",     "newsTextFontSize",     &MainWindow::newsTextFontFamily_,      &MainWindow::newsTextFontSize_,       2 },
  { "notificationFontFamily", "notificationFontSize", &MainWindow::notificationFontFamily_,  &MainWindow::notificationFontSize_,   0 },
};

const char *MainWindow::colorSettingKey(int row)
{
  return (row >= 0 && row < kColorOptionCount) ? ThemeBindings::kColors[row].key : "";
}

const char *MainWindow::fontFamilySettingKey(int row)
{
  return (row >= 0 && row < kFontOptionCount) ? ThemeBindings::kFonts[row].familyKey : "";
}

const char *MainWindow::fontSizeSettingKey(int row)
{
  return (row >= 0 && row < kFontOptionCount) ? ThemeBindings::kFonts[row].sizeKey : "";
}

QString MainWindow::defaultColorForOption(int row) const
{
  if (row < 0 || row >= kColorOptionCount)
    return qApp->palette().color(QPalette::Text).name();

  const QPalette palette = qApp->palette();
  const QColor text = palette.color(QPalette::Text);
  const QColor base = palette.color(QPalette::Base);

  switch (ThemeBindings::kColors[row].defaultKind) {
  case ThemeBindings::EmptyBackground:
    // Empty means "let the active theme/QSS paint the view background".
    return QString();
  case ThemeBindings::PaletteHighlightedText:
    return palette.color(QPalette::HighlightedText).name();
  case ThemeBindings::PaletteHighlight:
    return palette.color(QPalette::Highlight).name();
  case ThemeBindings::PaletteLink:
    return palette.color(QPalette::Link).name();
  case ThemeBindings::MutedText:
    return mixColor(text, base, 62).name();
  case ThemeBindings::PaletteBase:
    return base.name();
  case ThemeBindings::PaletteAlternateBase:
    return palette.color(QPalette::AlternateBase).name();
  case ThemeBindings::PaletteWindowText:
    return palette.color(QPalette::WindowText).name();
  case ThemeBindings::PaletteWindow:
    return palette.color(QPalette::Window).name();
  case ThemeBindings::PaletteText:
  default:
    return text.name();
  }
}

QString MainWindow::colorValueForOption(int row) const
{
  if (row < 0 || row >= kColorOptionCount) return QString();
  const auto &spec = ThemeBindings::kColors[row];
  return spec.mwMember ? this->*(spec.mwMember) : feedsModel_->*(spec.fmMember);
}

void MainWindow::setColorValueForOption(int row, const QString &value)
{
  if (row < 0 || row >= kColorOptionCount) return;
  const auto &spec = ThemeBindings::kColors[row];
  if (spec.mwMember)
    this->*(spec.mwMember) = value;
  else
    feedsModel_->*(spec.fmMember) = value;
}

QFont MainWindow::defaultFontForOption(int row)
{
  const int delta = (row >= 0 && row < kFontOptionCount)
      ? ThemeBindings::kFonts[row].defaultSizeDelta : 0;
  return QFont(qApp->font().family(), qApp->font().pointSize() + delta);
}

QFont MainWindow::fontForOption(int row) const
{
  if (row == 0) return feedsView_->font();
  if (row < 0 || row >= kFontOptionCount) return qApp->font();
  const auto &spec = ThemeBindings::kFonts[row];
  return QFont(this->*(spec.familyMember), this->*(spec.sizeMember));
}

void MainWindow::setFontForOption(int row, const QFont &font)
{
  if (row == 0) {
    feedsView_->setFont(font);
    feedsModel_->font_ = feedsView_->font();
    categoriesTree_->setFont(font);
    return;
  }
  if (row < 0 || row >= kFontOptionCount) return;
  const auto &spec = ThemeBindings::kFonts[row];
  this->*(spec.familyMember) = font.family();
  this->*(spec.sizeMember) = font.pointSize();
}

void MainWindow::setThemeColorDefaults()
{
  for (int row = 0; row < kColorOptionCount; ++row) {
    if (!customColorRows_.contains(row))
      setColorValueForOption(row, defaultColorForOption(row));
  }
}

void MainWindow::refreshApplicationAppearance()
{
  const QString dark = qApp->palette().color(QPalette::Dark).name();
  const QString window = qApp->palette().color(QPalette::Window).name();

  feedsView_->setStyleSheet(feedsModel_->backgroundColor_.isEmpty()
      ? QString() : QString("#feedsView_ {background: %1;}").arg(feedsModel_->backgroundColor_));
  feedsPanel_->setStyleSheet(
        QString("#feedsPanel_ {border-bottom: 1px solid %1;}").arg(dark));
  feedsSplitter_->setStyleSheet(
        QString("QSplitter::handle {background: %1;}").arg(dark));
  categoriesTree_->setStyleSheet(
        QString("#newsCategoriesTree_ {border-top: 1px solid %1;}").arg(dark));
  tabBar_->setStyleSheet(
        QString("#tabBar_ QToolButton {border: 1px solid %1; border-radius: 2px; background: %2;}").
        arg(dark).arg(window));
#if defined(HAVE_X11) || defined(Q_OS_MAC)
  statusBar()->setStyleSheet(
        QString("QStatusBar::item {border-right: 1px solid %1; margin: 1px;}").arg(dark));
#endif

  feedsView_->viewport()->update();
  categoriesTree_->viewport()->update();

  mainSplitter_->setStyleSheet(
        QString("QSplitter::handle {background: qlineargradient("
                "x1: 0, y1: 0, x2: 0, y2: 1,"
                "stop: 0 %1, stop: 0.07 %2);}").
        arg(feedsPanel_->palette().window().color().name()).
        arg(qApp->palette().color(QPalette::Dark).name()));

  for (int i = 0; i < stackedWidget_->count(); ++i) {
    NewsTabWidget *tab = qobject_cast<NewsTabWidget*>(stackedWidget_->widget(i));
    if (tab) tab->refreshAppearance(true);
  }
}

void MainWindow::applyThemeColors(bool clearOverrides)
{
  if (clearOverrides) {
    Settings().remove("Color");
    customColorRows_.clear();
  }
  setThemeColorDefaults();
  refreshApplicationAppearance();
}

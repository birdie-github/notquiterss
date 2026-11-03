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

QString MainWindow::defaultColorForOption(int row) const
{
  const QPalette palette = qApp->palette();
  const QColor text = palette.color(QPalette::Text);
  const QColor base = palette.color(QPalette::Base);
  const QString muted = mixColor(text, base, 62).name();

  switch (row) {
  case 1: case 3:
    // Empty means "let the active theme/QSS paint the view background".
    return QString();
  case 4: case 17:
    return palette.color(QPalette::HighlightedText).name();
  case 5: case 18:
    return palette.color(QPalette::Highlight).name();
  case 6: case 7: case 13: case 14:
    return palette.color(QPalette::Link).name();
  case 8: case 9: case 19:
    return muted;
  case 11: case 12:
    return palette.color(QPalette::Base).name();
  case 20:
    return palette.color(QPalette::AlternateBase).name();
  case 21:
    return palette.color(QPalette::WindowText).name();
  case 22:
    return palette.color(QPalette::Window).name();
  default:
    return text.name();
  }
}

QString MainWindow::colorValueForOption(int row) const
{
  switch (row) {
  case 0: return feedsModel_->textColor_;
  case 1: return feedsModel_->backgroundColor_;
  case 2: return newsListTextColor_;
  case 3: return newsListBackgroundColor_;
  case 4: return focusedNewsTextColor_;
  case 5: return focusedNewsBGColor_;
  case 6: return linkColor_;
  case 7: return titleColor_;
  case 8: return dateColor_;
  case 9: return authorColor_;
  case 10: return newsTextColor_;
  case 11: return newsTitleBackgroundColor_;
  case 12: return newsBackgroundColor_;
  case 13: return feedsModel_->feedWithNewNewsColor_;
  case 14: return feedsModel_->countNewsUnreadColor_;
  case 15: return newNewsTextColor_;
  case 16: return unreadNewsTextColor_;
  case 17: return feedsModel_->focusedFeedTextColor_;
  case 18: return feedsModel_->focusedFeedBGColor_;
  case 19: return feedsModel_->feedDisabledUpdateColor_;
  case 20: return alternatingRowColors_;
  case 21: return notifierTextColor_;
  case 22: return notifierBackgroundColor_;
  default: return QString();
  }
}

void MainWindow::setColorValueForOption(int row, const QString &value)
{
  switch (row) {
  case 0: feedsModel_->textColor_ = value; break;
  case 1: feedsModel_->backgroundColor_ = value; break;
  case 2: newsListTextColor_ = value; break;
  case 3: newsListBackgroundColor_ = value; break;
  case 4: focusedNewsTextColor_ = value; break;
  case 5: focusedNewsBGColor_ = value; break;
  case 6: linkColor_ = value; break;
  case 7: titleColor_ = value; break;
  case 8: dateColor_ = value; break;
  case 9: authorColor_ = value; break;
  case 10: newsTextColor_ = value; break;
  case 11: newsTitleBackgroundColor_ = value; break;
  case 12: newsBackgroundColor_ = value; break;
  case 13: feedsModel_->feedWithNewNewsColor_ = value; break;
  case 14: feedsModel_->countNewsUnreadColor_ = value; break;
  case 15: newNewsTextColor_ = value; break;
  case 16: unreadNewsTextColor_ = value; break;
  case 17: feedsModel_->focusedFeedTextColor_ = value; break;
  case 18: feedsModel_->focusedFeedBGColor_ = value; break;
  case 19: feedsModel_->feedDisabledUpdateColor_ = value; break;
  case 20: alternatingRowColors_ = value; break;
  case 21: notifierTextColor_ = value; break;
  case 22: notifierBackgroundColor_ = value; break;
  default: break;
  }
}

void MainWindow::setThemeColorDefaults()
{
  for (int row = 0; row < 23; ++row) {
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

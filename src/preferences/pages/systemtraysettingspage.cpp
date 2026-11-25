// SPDX-License-Identifier: GPL-3.0-or-later
#include "systemtraysettingspage.h"

#include "preferences/optionsdialog.h"
#include "preferences/optionspagebuilder.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QRadioButton>
#include <QVBoxLayout>

SystemTraySettingsPage::SystemTraySettingsPage(QWidget *parent,
                                               QVector<OptionSearchEntry> *searchEntries,
                                               int pageIndex)
  : QFrame(parent)
{
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *layout = new QVBoxLayout(this);
  OptionsPageBuilder b(this, layout, pageIndex, -1, searchEntries);

  b.group(showTrayIconBox_, OptionsDialog::tr("Show system tray icon"), true, [this](OptionsPageBuilder &grp) {
    const QString appName = QGuiApplication::applicationDisplayName();
#ifndef Q_OS_MAC
    grp.label(OptionsDialog::tr("Move to the system tray when:"));
    grp.indented(QMargins(15, 0, 5, 10), [this, appName](OptionsPageBuilder &sub) {
      sub.check(startingTray_, OptionsDialog::tr("starting %1").arg(appName), QString(),
                QStringLiteral("minimized hidden launch"));
      sub.check(minimizingTray_, OptionsDialog::tr("minimizing %1").arg(appName), QString(),
                QStringLiteral("minimize hide"));
      sub.check(closingTray_, OptionsDialog::tr("closing %1").arg(appName), QString(),
                QStringLiteral("close quit exit"));
    });
#else
    startingTray_ = new QCheckBox(this);
    startingTray_->hide();
    minimizingTray_ = new QCheckBox(this);
    minimizingTray_->hide();
    closingTray_ = new QCheckBox(this);
    closingTray_->hide();
#endif
    grp.label(OptionsDialog::tr("Tray icon behavior:"));
    grp.indented(QMargins(15, 0, 5, 10), [this](OptionsPageBuilder &sub) {
      sub.radio(staticIconTray_, OptionsDialog::tr("Show a static icon"), QStringLiteral("tray badge"));
      sub.radio(changeIconTray_, OptionsDialog::tr("Change icon when new articles arrive"), QStringLiteral("tray badge"));
      sub.radio(newCountTray_, OptionsDialog::tr("Show number of new articles"), QStringLiteral("tray badge counter"));
      sub.radio(unreadCountTray_, OptionsDialog::tr("Show number of unread articles"), QStringLiteral("tray badge counter"));
    });
#ifndef Q_OS_MAC
    grp.check(singleClickTray_, OptionsDialog::tr("Use a single click instead of a double click to show the window"),
              QString(), QStringLiteral("click open"));
    grp.check(clearStatusNew_, OptionsDialog::tr("Clear 'new' status when minimizing to tray"),
              QString(), QStringLiteral("unread reset"));
#else
    singleClickTray_ = new QCheckBox(this);
    singleClickTray_->hide();
    clearStatusNew_ = new QCheckBox(this);
    clearStatusNew_->hide();
#endif
#if defined(Q_OS_WIN)
    grp.check(emptyWorking_, OptionsDialog::tr("Empty working set when minimizing to tray"),
              QString(), QStringLiteral("ram memory trim"));
#else
    emptyWorking_ = new QCheckBox(this);
    emptyWorking_->hide();
#endif
    grp.stretch(1);
  }, Qt::Vertical, std::nullopt, QStringLiteral("notification area panel"));
}

void SystemTraySettingsPage::setBehaviorIconTray(int behavior)
{
  switch (behavior) {
  case CHANGE_ICON_TRAY:       changeIconTray_->setChecked(true);  break;
  case NEW_COUNT_ICON_TRAY:    newCountTray_->setChecked(true);    break;
  case UNREAD_COUNT_ICON_TRAY: unreadCountTray_->setChecked(true); break;
  default: staticIconTray_->setChecked(true);
  }
}

int SystemTraySettingsPage::behaviorIconTray() const
{
  if (staticIconTray_->isChecked())       return STATIC_ICON_TRAY;
  if (changeIconTray_->isChecked())       return CHANGE_ICON_TRAY;
  if (newCountTray_->isChecked())         return NEW_COUNT_ICON_TRAY;
  if (unreadCountTray_->isChecked())      return UNREAD_COUNT_ICON_TRAY;
  return STATIC_ICON_TRAY;
}

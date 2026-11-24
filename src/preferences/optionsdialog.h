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
#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QtWidgets>
#include <QtSql>

#include "preferences/optionspagebuilder.h"
#include "widgets/dialog.h"
#include "widgets/lineedit.h"
#include "notifications/notificationswidget.h"

class BrowserSettingsPage;
class FeedsSettingsPage;
class FontsColorsSettingsPage;
class GeneralSettingsPage;
class LabelsSettingsPage;
class LanguageSettingsPage;
class NetworkSettingsPage;
class NotificationsSettingsPage;
class NotifierFeedsDialog;
class OptionsSearchPanel;
class PasswordsSettingsPage;
class ShortcutsSettingsPage;
class SystemTraySettingsPage;

#define STATIC_ICON_TRAY        0
#define CHANGE_ICON_TRAY        1
#define NEW_COUNT_ICON_TRAY     2
#define UNREAD_COUNT_ICON_TRAY  3

class OptionsDialog : public Dialog
{
  Q_OBJECT
public:
  enum Page {
    PageGeneral = 0,
    PageSystemTray,
    PageNetwork,
    PageBrowser,
    PageFeeds,
    PageLabels,
    PageNotifications,
    PagePasswords,
    PageLanguage,
    PageFontsColors,
    PageShortcuts,
    PageBackup,
    PageCount
  };

  explicit OptionsDialog(QWidget *parent);
  int currentIndex();
  void setCurrentItem(int index);
  void ensurePageCreated(int pageIndex);
  bool isPageCreated(int pageIndex) const {
    return pageIndex >= 0 && pageIndex < pageWidgets_.size() && pageWidgets_.at(pageIndex) != nullptr;
  }

  GeneralSettingsPage *generalPage() const { return generalPage_; }
  SystemTraySettingsPage *traySystemPage() const { return traySystemPage_; }
  NetworkSettingsPage *networkPage() const { return networkPage_; }
  BrowserSettingsPage *browserPage() const { return browserPage_; }
  FeedsSettingsPage *feedsPage() const { return feedsPage_; }
  LabelsSettingsPage *labelsPage() const { return labelsPage_; }
  NotificationsSettingsPage *notifierPage() const { return notifierPage_; }
  PasswordsSettingsPage *passwordsPage() const { return passwordsPage_; }
  LanguageSettingsPage *languagePage() const { return languagePage_; }
  FontsColorsSettingsPage *fontsColorsPage() const { return fontsColorsPage_; }
  ShortcutsSettingsPage *shortcutsPage() const { return shortcutsPage_; }

  struct NotificationAppearance {
    QString fontFamily;
    int fontSize;
    QString textColor;
    QString backgroundColor;
    QString linkColor;
  };
  NotificationAppearance notificationPreviewAppearance() const;

  void loadActionShortcut(QList<QAction *> actions, const QStringList *list);

signals:
  void signalCategoriesTreeKeyUpDownPressed();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private slots:
  void slotCategoriesItemClicked(QTreeWidgetItem* item, int);
  void slotCategoriesTreeKeyUpDownPressed();
  void searchTextChanged(const QString &text);
  void acceptDialog();
  void closeDialog();
  void showNotification();
  void deleteNotification();

private:
  struct PageDescriptor {
    const char *title;
    QWidget *(OptionsDialog::*create)();
    void (OptionsDialog::*onEnter)();
  };
  static const PageDescriptor kPages[PageCount];

  void showEvent(QShowEvent *) override;
  void buildSearchIndex();
  QString searchEntrySection(const OptionSearchEntry &entry) const;
  QTabWidget *tabWidgetForPage(int pageIndex) const;
  void jumpToSearchEntry(int entryIndex);

  QWidget *createGeneralWidget();
  QWidget *createTraySystemWidget();
  QWidget *createNetworkConnectionsWidget();
  QWidget *createBrowserWidget();
  QWidget *createFeedsWidget();
  void onFeedsPageEntered();
  QWidget *createLabelsWidget();
  void loadLabels();
  QWidget *createNotifierWidget();
  void openFeedsNotifierDlg();
  QWidget *createPasswordsWidget();
  void loadPasswords();
  QWidget *createLanguageWidget();
  void loadLanguages();
  QWidget *createFontsColorsWidget();
  QWidget *createShortcutWidget();
  void loadShortcuts();
  QWidget *createBackupWidget();

  QSqlDatabase db_;

  QLabel *contentLabel_;
  LineEdit *searchEdit_ = nullptr;
  QTreeWidget *categoriesTree_;
  QTreeWidgetItem *searchCategoryItem_ = nullptr;
  QVector<QTreeWidgetItem *> categoryItems_;
  QSplitter *splitter_;
  QStackedWidget *contentStack_;
  OptionsSearchPanel *searchPanel_ = nullptr;
  int lastPageIndex_ = 0;
  bool searchIndexBuilt_ = false;
  QVector<QWidget *> pageWidgets_;
  QVector<QScrollArea *> pageScrollAreas_;

  GeneralSettingsPage *generalPage_ = nullptr;
  SystemTraySettingsPage *traySystemPage_ = nullptr;
  NetworkSettingsPage *networkPage_ = nullptr;
  BrowserSettingsPage *browserPage_ = nullptr;
  FeedsSettingsPage *feedsPage_ = nullptr;
  LabelsSettingsPage *labelsPage_ = nullptr;
  NotificationsSettingsPage *notifierPage_ = nullptr;
  NotifierFeedsDialog *feedsNotifierDlg_ = nullptr;
  PasswordsSettingsPage *passwordsPage_ = nullptr;
  LanguageSettingsPage *languagePage_ = nullptr;
  FontsColorsSettingsPage *fontsColorsPage_ = nullptr;
  ShortcutsSettingsPage *shortcutsPage_ = nullptr;
  QWidget *backupWidget_ = nullptr;

  NotificationWidget *notificationWidget_ = nullptr;
  QList<QAction *> shortcutActions_;
  const QStringList *listDefaultShortcut_ = nullptr;
  QElapsedTimer openTimer_;
  bool showLogged_ = false;
  QVector<OptionSearchEntry> searchEntries_;

};

#endif // OPTIONSDIALOG_H

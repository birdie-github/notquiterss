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
#ifndef SETTINGS_H
#define SETTINGS_H

#include <QSettings>
#include <QVariant>

class Settings
{
public:
  explicit Settings(const QString &prefix = QString()) : prefix_(prefix) {}

  static void createSettings(const QString &fileName = QString());
  static bool syncSettings();
  QString fileName();

  void setValue(const QString &key, const QVariant &defaultValue = QVariant());
  QVariant value(const QString &key, const QVariant &defaultValue = QVariant());
  bool contains(const QString &key);
  void remove(const QString &key);

private:
  static QSettings *storage();
  QString fullKey(const QString &key) const;
  const QString prefix_;

};

namespace AppSettings {
// Shared fixed defaults. Keys are absolute, independent of any wrapper prefix.
template<typename T>
struct Setting
{
  const char *key;
  T defaultValue;

  T get() const { return qvariant_cast<T>(Settings().value(key, defaultValue)); }
  void set(const T &value) const { Settings().setValue(key, value); }
};

constexpr Setting<bool> backupEnabled = {"Backup/enabled", false};
constexpr Setting<bool> backupExit = {"Backup/onExit", true};
constexpr Setting<bool> backupSubscriptions = {"Backup/onSubscriptions", true};
constexpr Setting<bool> backupScheduled = {"Backup/scheduled", false};
constexpr Setting<bool> backupClean = {"Backup/clean", false};
constexpr Setting<int> backupFrequency = {"Backup/frequencyDays", 1};
constexpr Setting<int> backupKeep = {"Backup/keep", 10};

constexpr Setting<bool> showSplashScreen = {"Settings/showSplashScreen", true};
constexpr Setting<bool> autoUpdatefeedsStartUp = {"Settings/autoUpdatefeedsStartUp", false};
constexpr Setting<bool> storeDBMemory = {"Settings/storeDBMemory", true};
constexpr Setting<int> saveDBMemFileInterval = {"Settings/saveDBMemFileInterval", 30};
constexpr Setting<bool> showCloseButtonTab = {"Settings/showCloseButtonTab", true};
constexpr Setting<bool> updateCheckEnabled = {"Settings/updateCheckEnabled", true};
constexpr Setting<int> openingFeedAction = {"Settings/openingFeedAction", 0};
constexpr Setting<bool> openNewsWebViewOn = {"Settings/openNewsWebViewOn", true};
constexpr Setting<int> timeoutRequest = {"Settings/timeoutRequest", 15};
constexpr Setting<int> numberRequest = {"Settings/numberRequest", 10};
constexpr Setting<int> numberRepeats = {"Settings/numberRepeats", 2};

constexpr Setting<int> externalBrowserOn = {"Settings/externalBrowserOn", 1};
extern const Setting<QString> externalBrowser;

extern const Setting<QString> toolBarStyle;
extern const Setting<QString> toolBarIconSize;
extern const Setting<QString> feedsToolBarIconSize;
extern const Setting<QString> newsToolBarIconSize;
} // namespace AppSettings

#endif // SETTINGS_H

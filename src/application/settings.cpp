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
#include "settings.h"

#include <QCoreApplication>
#include <memory>

namespace {
// Initialized once on the main thread, before any workers are started.
QString settingsFileName;
QString settingsOrganization;
QString settingsApplication;
bool settingsCreated = false;
}

void Settings::createSettings(const QString &fileName)
{
  Q_ASSERT(!settingsCreated);
  settingsFileName = fileName;
  settingsOrganization = QCoreApplication::organizationName();
  settingsApplication = QCoreApplication::applicationName();
  settingsCreated = true;
  storage();
}

QSettings *Settings::storage()
{
  Q_ASSERT(settingsCreated);
  // Distinct objects per thread, kept alive across temporary Settings wrappers.
  // QSettings shares changes to the same location within this process.
  static thread_local std::unique_ptr<QSettings> settings = []() {
    std::unique_ptr<QSettings> result(settingsFileName.isEmpty()
        ? new QSettings(QSettings::IniFormat, QSettings::UserScope,
                        settingsOrganization, settingsApplication)
        : new QSettings(settingsFileName, QSettings::IniFormat));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Read settings written by either Qt major version, including Unicode values.
    result->setIniCodec("UTF-8");
#endif
    return result;
  }();
  return settings.get();
}

bool Settings::syncSettings()
{
  storage()->sync();
  return storage()->status() == QSettings::NoError;
}

QString Settings::fileName()
{
  return storage()->fileName();
}

QString Settings::fullKey(const QString &key) const
{
  // Leave slash normalization to QSettings, including legacy shortcut keys.
  return prefix_.isEmpty() ? key : prefix_ + '/' + key;
}

void Settings::setValue(const QString &key, const QVariant &defaultValue)
{
  storage()->setValue(fullKey(key), defaultValue);
}

QVariant Settings::value(const QString &key, const QVariant &defaultValue)
{
  return storage()->value(fullKey(key), defaultValue);
}

bool Settings::contains(const QString &key)
{
  return storage()->contains(fullKey(key));
}

void Settings::remove(const QString &key)
{
  storage()->remove(fullKey(key));
}

namespace AppSettings {
const Setting<QString> externalBrowser = {"Settings/externalBrowser", QString()};
const Setting<QString> toolBarStyle = {"Settings/toolBarStyle", QStringLiteral("toolBarStyleTuI_")};
const Setting<QString> toolBarIconSize = {"Settings/toolBarIconSize", QStringLiteral("toolBarIconNormal_")};
const Setting<QString> feedsToolBarIconSize = {"Settings/feedsToolBarIconSize", QStringLiteral("toolBarIconSmall_")};
const Setting<QString> newsToolBarIconSize = {"Settings/newsToolBarIconSize", QStringLiteral("toolBarIconSmall_")};
} // namespace AppSettings

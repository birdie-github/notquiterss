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
#ifndef LOGFILE_H
#define LOGFILE_H

#include <QCoreApplication>
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QTextStream>
#include <QUrl>

const size_t maxLogFileSize = 1 * 1024 * 1024; //1 MB

class LogFile
{
public:
  static void msgHandler(QtMsgType type, const QMessageLogContext &, const QString &msg);
  static void configure(const QString &fileName, bool enabled, bool suppressDebug);
  static void enableConsole();
  static bool consoleLoggingEnabled();
  static void prepareConsole();
  static bool fileLoggingEnabled();
  static void setFileLoggingEnabled(bool enabled);
  static void showLocation();
  static QString urlForLog(const QUrl &url);
  static QString redactUrls(const QString &message);

private:
  explicit LogFile();

};

#endif // LOGFILE_H

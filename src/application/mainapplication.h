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
#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#define mainApp MainApplication::getInstance()

#include "languagecatalog.h"
#include "applicationstyle.h"
#include <QtWidgets>
#include <qtsingleapplication.h>
#include <QNetworkDiskCache>
#include <QLocale>
#include <QLibraryInfo>
#include <QNetworkProxy>

#include "cookiejar.h"
#include "downloadmanager.h"
#include "mainwindow.h"

class NetworkManager;
class SplashScreen;
class UpdateFeeds;

class MainApplication : public QtSingleApplication
{
  Q_OBJECT
public:
  explicit MainApplication(int &argc, char** argv);
  ~MainApplication();
  int startupExitCode() const { return startupExitCode_; }

  static MainApplication *getInstance();

  bool isPortable() const;
  bool isPortableAppsCom() const;
  void setClosing();
  bool isClosing() const;
  bool isNoDebugOutput() const;
  void showClosingWidget();

  QString resourcesDir() const;
  QString dataDir() const;
  QString absolutePath(const QString &path) const;
  QString dbFileName() const;
  QString cacheDefaultDir() const;
  QString soundNotifyDefaultFile() const;
  QString styleSheetNewsDefaultFile() const;

  bool storeDBMemory() const;
  bool dbFileExists() const { return dbFileExists_; }
  bool isSaveDataLastFeed() const;
  void sqlQueryExec(const QString &query);

  MainWindow *mainWindow();
  NetworkManager *networkManager();
  CookieJar *cookieJar();
  void setDiskCache();
  UpdateFeeds *updateFeeds();
  void runUserFilter(int feedId, int filterId);
  DownloadManager *downloadManager();
  bool openExternalUrl(const QUrl &url);

  LanguageCatalog languageCatalog() const;
  QList<ApplicationStyle> applicationStyles() const;
  const ApplicationStyle &applicationStyle() const { return applicationStyle_; }
  void applyApplicationStyle(const QString &id);
  void setTranslateApplication();
  QString language() const { return langFileName_; }
  void setLanguage(const QString &lang) { langFileName_ = lang; }

  QNetworkProxy networkProxy() const { return networkProxy_; }
  void proxyLoadSettings();
  void proxySaveSettings(const QNetworkProxy &proxy);

public slots:
  void receiveMessage(const QString &message);
  void quitApplication();

signals:
  void signalRunUserFilter(int feedId, int filterId);
  void signalSqlQueryExec(const QString &query);

private slots:
  void commitData(QSessionManager &manager);

private:
  void createSettings();
  bool connectDatabase();
  void setStyleApplication();
  void showSplashScreen();
  void closeSplashScreen();
  void setProgressSplashScreen(int value);

  void setProxy();

  bool isPortableAppsCom_;
  bool isClosing_;
  int startupExitCode_ = 0;

  bool storeDBMemory_;
  bool dbFileExists_;
  bool isSaveDataLastFeed_;
  ApplicationStyle applicationStyle_;
  bool showSplashScreen_;
  bool updateFeedsStartUp_;

  QTranslator *translator_;
  QTranslator *qt_translator_;
  QString langFileName_;
  SplashScreen *splashScreen_;
  MainWindow *mainWindow_;
  NetworkManager *networkManager_;
  CookieJar *cookieJar_;
  QNetworkDiskCache *diskCache_;
  UpdateFeeds *updateFeeds_;
  DownloadManager *downloadManager_;
  QWidget *closingWidget_;

  QNetworkProxy networkProxy_;

};

#endif // MAINAPPLICATION_H

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
#include "databasebackup.h"
#include "database.h"

#include "common.h"
#include "mainapplication.h"
#include "mainwindow.h"
#include "settings.h"
#include "sqlitedriver.h"

#include <sqlite3.h>

namespace {
// Set once during startup, before any SQL workers exist, then read-only.
QString liveDatabaseName;
QString liveDatabaseOptions;
std::recursive_mutex databaseAccess;
}

Database::AccessLock Database::tryAccess()
{
  return AccessLock(databaseAccess, std::try_to_lock);
}

Database::AccessLock Database::backgroundAccess()
{
  AccessLock lock(databaseAccess, std::defer_lock);
  // FeedReadState is also used synchronously on the UI thread. Its caller
  // takes tryAccess() when the operation can be deferred; never block it here.
  if (QThread::currentThread() != QCoreApplication::instance()->thread())
    lock.lock();
  return lock;
}

const int versionDB = 17;

const QString kCreateFeedsTableQuery(
    "CREATE TABLE feeds("
    "id integer primary key, "
    "text varchar, "             // Feed text (replaces title at the moment)
    "title varchar, "            // Feed title
    "description varchar, "      // Feed description
    "xmlUrl varchar, "           // URL-link of the feed
    "htmlUrl varchar, "          // URL-link site, that contains the feed
    "language varchar, "         // Feed language
    "copyrights varchar, "       // Feed copyrights
    "author_name varchar, "      // Feed author: name
    "author_email varchar, "     //              e-mail
    "author_uri varchar, "       //              personal web page
    "webMaster varchar, "        // e-mail of feed's technical support
    "pubdate varchar, "          // Feed publication timestamp
    "lastBuildDate varchar, "    // Timestamp of last modification of the feed
    "category varchar, "         // Categories of content of the feed
    "contributor varchar, "      // Feed contributors (tab separated)
    "generator varchar, "        // Application has used to generate the feed
    "docs varchar, "             // URL-link to document describing RSS-standart
    "cloud_domain varchar, "     // Web-service providing rssCloud interface
    "cloud_port varchar, "       //   .
    "cloud_path varchar, "       //   .
    "cloud_procedure varchar, "  //   .
    "cloud_protocal varchar, "   //   .
    "ttl integer, "              // Time in minutes the feed can be cached
    "skipHours varchar, "        // Tip for aggregators, not to update the feed (specify hours of the day that can be skipped)
    "skipDays varchar, "         // Tip for aggregators, not to update the feed (specify day of the week that can be skipped)
    "image blob, "               // gif, jpeg, png picture, that can be associated with the feed
    "unread integer, "           // number of unread news
    "newCount integer, "         // number of new news
    "currentNews integer, "      // current displayed news
    "label varchar, "            // user purpose label(s)
    "undeleteCount integer, "    // number of all news (not marked deleted)
    "tags varchar, "             // user purpose tags
    // --- Categories ---
    "hasChildren integer default 0, "  // Children presence. Default - none
    "parentId integer default 0, "     // parent id of the feed. Default - tree root
    "rowToParent integer, "            // sequence number relative to parent
    // --- General ---
    "updateIntervalEnable int, "    // auto update enable flag
    "updateInterval int, "          // auto update interval
    "updateIntervalType varchar, "  // auto update interval type(minutes, hours,...)
    "updateOnStartup int, "         // update the feed on application startup
    "displayOnStartup int, "        // show the feed in separate tab in application startup
    // --- Reading ---
    "markReadAfterSecondsEnable int, "    // Enable "Read" timer
    "markReadAfterSeconds int, "          // Number of seconds that must elapse to mark news "Read"
    "markReadInNewspaper int, "           // mark Read when Newspaper layout
    "markDisplayedOnSwitchingFeed int, "  // mark Read on switching to another feed
    "markDisplayedOnClosingTab int, "     // mark Read on tab closing
    "markDisplayedOnMinimize int, "       // mark Read on minimizing to tray
    // --- Display ---
    "layout text, "      // news display layout
    "filter text, "      // news display filter
    "groupBy int, "      // column number to sort by
    "displayNews int, "  // 0 - display content from news; 1 - download content from link
    "displayEmbeddedImages integer default 1, "  // display images embedded in news
    "loadTypes text, "                           // type of content to load ("images" or "images sounds" - images only or images and sound)
    "openLinkOnEmptyContent int, "               // load link, if content is empty
    // --- Columns ---
    "columns text, "  // columns list and order of the news displayed in list
    "sort text, "     // column name to sort by
    "sortType int, "  // sort type (ascend, descend)
    // --- Clean Up ---
    "maximumToKeep int, "           // maximum number of news to keep
    "maximumToKeepEnable int, "     // enable limitation
    "maximumAgeOfNews int, "        // maximum store time of the news
    "maximumAgoOfNewEnable int, "   // enable limitation
    "deleteReadNews int, "          // delete read news
    "neverDeleteUnreadNews int, "   // don't delete unread news
    "neverDeleteStarredNews int, "  // don't delete starred news
    "neverDeleteLabeledNews int, "  // don't delete labeled news
    // --- Status ---
    "status text, "                 // last update result
    "created text, "                // feed creation timestamp
    "updated text, "                // last update timestamp
    "lastDisplayed text, "           // last display timestamp
    "f_Expanded integer default 1, "  // expand folder flag
    "flags text, "                    // more flags (example "focused", "hidden")
    "authentication integer default 0, "    // enable authentification, sets on feed creation
    "duplicateNewsMode integer default 0, " // news duplicates process mode
    "addSingleNewsAnyDateOn integer default 1, " // enable adding news with any date into the database
    "avoidedOldSingleNewsDateOn integer default 0, " // avoid adding news before this date into the database
    "avoidedOldSingleNewsDate varchar, " // date to avoid
    "typeFeed integer default 0, "          // reserved for future purposes
    "showNotification integer default 0, "  //
    "disableUpdate integer default 0, "     // disable update feed
    "javaScriptEnable integer default 1, "  //
    // version 16
    "layoutDirection integer default 0, "    // 0 - ltr; 1 - rtl
    // Version 17
    "SingleClickAction integer default 0, " // ENewsClickAction
    "DoubleClickAction integer default 0, " // ENewsClickAction
    "MiddleClickAction integer default 0 "  // ENewsClickAction
    ")");

const QString kCreateNewsTableQuery(
    "CREATE TABLE news("
    "id integer primary key, "
    "feedId integer, "                     // feed id from feed table
    "guid varchar, "                       // news unique number
    "guidislink varchar default 'true', "  // flag shows that news unique number is URL-link to news
    "description varchar, "                // brief description
    "content varchar, "                    // full content (atom)
    "title varchar, "                      // title
    "published varchar, "                  // publish timestamp
    "modified varchar, "                   // modification timestamp
    "received varchar, "                   // receive news timestamp (set on receive)
    "author_name varchar, "                // author name
    "author_uri varchar, "                 // author web page (atom)
    "author_email varchar, "               // author e-mail (atom)
    "category varchar, "                   // category. May be several item tabs separated
    "label varchar, "                      // label (user purpose label(s))
    "new integer default 1, "              // Flag "new". Set on receive, reset on application close
    "read integer default 0, "             // Flag "read". Set after news has been focused
    "starred integer default 0, "          // Flag "sticky". Set by user
    "deleted integer default 0, "          // Flag "deleted". News is marked deleted by remains in DB,
                                           //   for purpose not to display after next update.
                                           //   News are deleted by cleanup process only
    "attachment varchar, "                 // Links to attachments (tabs separated)
    "comments varchar, "                   // News comments page URL-link
    "enclosure_length, "                   // Media-object, associated to news:
    "enclosure_type, "                     //   length, type,
    "enclosure_url, "                      //   URL-address
    "source varchar, "                     // source, incese of republication (atom: <link via>)
    "link_href varchar, "                  // URL-link to news (atom: <link self>)
    "link_enclosure varchar, "             // URL-link to huge amoun of data,
                                           //   that can't be received in the news
    "link_related varchar, "               // URL-link for related data of the news (atom)
    "link_alternate varchar, "             // URL-link to alternative news representation
    "contributor varchar, "                // contributors (tabs separated)
    "rights varchar, "                     // copyrights
    "deleteDate varchar, "                 // news delete timestamp
    "feedParentId integer default 0 "      // parent feed id from feed table
    ")");

const QString kCreateFiltersTable(
    "CREATE TABLE filters("
    "id integer primary key, "
    "name varchar, "              // filter name
    "type integer, "              // filter type (and, or, for all)
    "feeds varchar, "             // feed list, that are using the filter
    "enable integer default 1, "  // 1 - filter used; 0 - filter not used
    "num integer "                // Sequence number. Used to sort filters
    ")");

const QString kCreateFilterConditionsTable(
    "CREATE TABLE filterConditions("
    "id integer primary key, "
    "idFilter int, "            // filter Id
    "field varchar, "           // field to filter by
    "condition varchar, "       // condition has applied to filed
    "content varchar "          // field content that is used by filter
    ")");

const QString kCreateFilterActionsTable(
    "CREATE TABLE filterActions("
    "id integer primary key, "
    "idFilter int, "            // filter Id
    "action varchar, "          // action that has appled for filter
    "params varchar "           // action parameters
    ")");

const QString kCreateLabelsTable(
    "CREATE TABLE labels("
    "id integer primary key, "
    "name varchar, "            // label name
    "image blob, "              // label image
    "color_text varchar, "      // news text color displayed in news list
    "color_bg varchar, "        // news background color displayed in news list
    "num integer, "             // sequence number to sort with
    "currentNews integer "      // current displayed news
    ")");

const QString kCreatePasswordsTable(
    "CREATE TABLE passwords("
    "id integer primary key, "
    "server varchar, "          // server
    "username varchar, "        // username
    "password varchar "         // password
    ")");

const QString kAddColumnsFeedsTableQuery(
        "ALTER TABLE feeds ADD COLUMN addSingleNewsAnyDateOn integer default 1;"
        "ALTER TABLE feeds ADD COLUMN avoidedOldSingleNewsDateOn integer default 0;"
        "ALTER TABLE feeds ADD COLUMN avoidedOldSingleNewsDate varchar;"
        );

int Database::version()
{
  return versionDB;
}

bool Database::initialization()
{
  if (mainApp->storeDBMemory()) {
    if (!sqlite3_vfs_find("memdb")) {
      qCritical() << "The linked SQLite library does not provide the memdb VFS.";
      QMessageBox::critical(nullptr, tr("Error"),
                            tr("This SQLite build cannot share an in-memory database. "
                               "Install SQLite 3.36.0 or newer with memdb support."));
      return false;
    }
    // A leading slash shares this RAM store across private-cache connections.
    // The unique name cannot collide with another instance or create a file.
    liveDatabaseName = "file:/notquiterss-" + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + "?vfs=memdb";
    // Reserve the writer at transaction entry, before taking read locks. This
    // avoids two RAM connections deadlocking while upgrading read transactions.
    liveDatabaseOptions = "QSQLITE_OPEN_URI;QSQLITE_IMMEDIATE_TRANSACTIONS";
  } else {
    liveDatabaseName = mainApp->dbFileName();
    liveDatabaseOptions.clear();
  }
  prepareDatabase();

  SQLiteDriver *driver = new SQLiteDriver();
  QSqlDatabase db = QSqlDatabase::addDatabase(driver);
  db.setDatabaseName(liveDatabaseName);
  db.setConnectOptions(liveDatabaseOptions);
  if (db.open()) {
    setPragma(db);

    if (mainApp->storeDBMemory()) {
      QString error;
      if (!sqliteDBMemFile(db, error, false)) {
        QMessageBox::critical(nullptr, tr("Error"),
                              tr("Cannot load the database into memory.\n%1").arg(error));
        db.close();
        return false;
      }
    }
    return true;
  }
  qCritical() << "Cannot open live database:" << db.lastError();
  QMessageBox::critical(nullptr, tr("Error"), db.lastError().text());
  return false;
}

void Database::setPragma(QSqlDatabase &db)
{
  Settings settings;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.exec("PRAGMA encoding = \"UTF-8\"");

  QString sync = settings.value("synchronousDB", "FULL").toString();
  q.exec(QString("PRAGMA synchronous = %1").arg(sync));
//  q.exec("PRAGMA journal_mode = MEMORY");
//  q.exec("PRAGMA temp_store = MEMORY");

  q.exec("PRAGMA page_size = 4096");
  q.exec("PRAGMA cache_size = 16384");
  q.finish();
}

void Database::prepareDatabase()
{
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "initialization");
    db.setDatabaseName(mainApp->dbFileName());
    if (!db.open()) {
      QString message = QString("Cannot open SQLite database! \n"
                                "Error: %1").arg(db.lastError().text());
      qCritical() << message;
      QMessageBox::critical(mainApp->mainWindow(), QObject::tr("Error"), message);
    } else {
      setPragma(db);
      QSqlQuery q(db);
      q.setForwardOnly(true);

      if (!mainApp->dbFileExists()) {
        qWarning() << "Creating database";

        createTables(db);
        createLabels(db);
        q.prepare("INSERT INTO info(name, value) VALUES ('version', :version)");
        q.bindValue(":version", version());
        q.exec();
        q.prepare("INSERT INTO info(name, value) VALUES('appVersion', :appVersion)");
        q.bindValue(":appVersion", QCoreApplication::applicationVersion());
        q.exec();
      } else {
        qWarning() << "Preparation database";

        // Version DB > 0.12.1
        int dbVersion = -1;
        q.exec("SELECT value FROM info WHERE name='version'");
        if (q.first()) {
          dbVersion = q.value(0).toInt();
        }

        QString appVersion = QString();
        q.exec("SELECT value FROM info WHERE name='appVersion'");
        if (q.first()) {
          appVersion = q.value(0).toString();
        }

        // Create backups for DB and Settings
        if (appVersion != QCoreApplication::applicationVersion()) {
          q.finish();
          DatabaseBackup::report(DatabaseBackup::create(db, DatabaseBackup::Trigger::Upgrade), false);
        }

        addColumnsToFeedsTables(db);

        if (dbVersion < 14) {
          q.exec("ALTER TABLE feeds ADD COLUMN showNotification integer default 0");
          q.exec("ALTER TABLE feeds ADD COLUMN disableUpdate integer default 0");
          q.exec("ALTER TABLE feeds ADD COLUMN javaScriptEnable integer default 1");
        }
        if (dbVersion < 16) {
          q.exec("ALTER TABLE feeds ADD COLUMN layoutDirection integer default 0");
        }

        if (dbVersion < 17)
        {
          q.exec("ALTER table feeds ADD COLUMN SingleClickAction integer default 0");
          q.exec("ALTER table feeds ADD COLUMN DoubleClickAction integer default 0");
          q.exec("ALTER table feeds ADD COLUMN MiddleClickAction integer default 0");
        }

        // Update appVersion anyway
        if (appVersion.isEmpty()) {
          q.prepare("INSERT INTO info(name, value) VALUES('appVersion', :appVersion)");
          q.bindValue(":appVersion", QCoreApplication::applicationVersion());
          q.exec();
        } else if (appVersion != QCoreApplication::applicationVersion()) {
          q.prepare("UPDATE info SET value=:appVersion WHERE name='appVersion'");
          q.bindValue(":appVersion", QCoreApplication::applicationVersion());
          q.exec();
        }

        if (dbVersion == -1) {
          q.prepare("INSERT INTO info(name, value) VALUES('version', :version)");
          q.bindValue(":version", version());
          q.exec();
        } else if (dbVersion < version()) {
          q.prepare("UPDATE info SET value=:version WHERE name='version'");
          q.bindValue(":version", version());
          q.exec();
        }

        Settings().setValue("VersionDB", version());
      }

      q.finish();
      db.close();
    }
  }
  QSqlDatabase::removeDatabase("initialization");
}

void Database::createTables(QSqlDatabase &db)
{
  db.transaction();

  QSqlQuery(db).exec(kCreateFeedsTableQuery);
  QSqlQuery(db).exec(kAddColumnsFeedsTableQuery);
  QSqlQuery(db).exec(kCreateNewsTableQuery);
  // Create index for feedId field
  QSqlQuery(db).exec("CREATE INDEX feedId ON news(feedId)");

  // Create extra feeds table just in case
  QSqlQuery(db).exec("CREATE TABLE feeds_ex(id integer primary key, "
          "feedId integer, "  // feed Id
          "name varchar, "    // parameter name
          "value varchar "    // parameter value
          ")");
  // Create extra news table just in case
  QSqlQuery(db).exec("CREATE TABLE news_ex(id integer primary key, "
          "feedId integer, "  // feed Id
          "newsId integer, "  // news Id
          "name varchar, "    // parameter name
          "value varchar "    // parameter value
          ")");
  // Create filters table
  QSqlQuery(db).exec(kCreateFiltersTable);
  QSqlQuery(db).exec(kCreateFilterConditionsTable);
  QSqlQuery(db).exec(kCreateFilterActionsTable);
  // Create extra filters just in case
  QSqlQuery(db).exec("CREATE TABLE filters_ex(id integer primary key, "
          "idFilter integer, "  // filter Id
          "name text, "         // parameter name
          "value text"          // parameter value
          ")");
  // Create labels table
  QSqlQuery(db).exec(kCreateLabelsTable);
  // Create password table
  QSqlQuery(db).exec(kCreatePasswordsTable);
  //
  QSqlQuery(db).exec("CREATE TABLE info(id integer primary key, name varchar, value varchar)");

  db.commit();
}

void Database::createLabels(QSqlDatabase &db)
{
  QSqlQuery q(db);
  for (int i = 0; i < 6; i++) {
    q.prepare("INSERT INTO labels(name, image) "
              "VALUES (:name, :image)");
    q.bindValue(":name", MainWindow::nameLabels().at(i));

    q.bindValue(":image", Common::readAllFileByteContents(QString(":/images/label_%1").arg(i+1)));

    q.exec();

    int labelId = q.lastInsertId().toInt();
    q.exec(QString("UPDATE labels SET num='%1' WHERE id=='%1'").arg(labelId));
  }
}

void Database::addColumnsToFeedsTables(QSqlDatabase &db)
{
    QStringList columnsList;
    // Version > 0.18.12
    columnsList.append(" addSingleNewsAnyDateOn integer default 1;");
    columnsList.append(" avoidedOldSingleNewsDateOn integer default 0;");
    columnsList.append(" avoidedOldSingleNewsDate varchar;");

    db.transaction();
    foreach (QString col, columnsList) {
        QSqlQuery(db).exec("ALTER TABLE feeds ADD COLUMN" + col);
    }
    db.commit();
}

QSqlDatabase Database::connection(const QString &connectionName)
{
  Q_ASSERT(!connectionName.isEmpty());
  Q_ASSERT(!QSqlDatabase::contains(connectionName));
  Q_ASSERT(!liveDatabaseName.isEmpty());
  QSqlDatabase db = QSqlDatabase::addDatabase(new SQLiteDriver(), connectionName);
  db.setDatabaseName(liveDatabaseName);
  db.setConnectOptions(liveDatabaseOptions);
  if (db.open()) setPragma(db);
  else qCritical() << "Cannot open SQL worker connection:" << db.lastError();
  return db;
}

bool Database::sqliteDBMemFile(QSqlDatabase &db, QString &error, bool save,
                              const QString &fileName)
{
  error.clear();
  const QString path = fileName.isEmpty() ? mainApp->dbFileName() : fileName;
  auto fail = [&](const QString &detail) {
    error = tr("Database copy failed for %1:\n%2").arg(path, detail);
    qCritical().noquote() << error;
    return false;
  };

  QVariant value = db.isOpen() && db.driver() ? db.driver()->handle() : QVariant();
  if (!value.isValid() || qstrcmp(value.typeName(), "sqlite3*") != 0)
    return fail(tr("The live SQLite connection is unavailable."));
  sqlite3 *memory = *static_cast<sqlite3 **>(value.data());
  if (!memory)
    return fail(tr("The live SQLite connection is closed."));

  sqlite3 *file = nullptr;
  // Loading must not create an empty database if the source has disappeared.
  const int flags = save ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
  const int openRc = sqlite3_open_v2(path.toUtf8().constData(), &file, flags, nullptr);
  if (openRc != SQLITE_OK) {
    const QString detail = QString::fromUtf8(file ? sqlite3_errmsg(file) : sqlite3_errstr(openRc));
    if (file) sqlite3_close(file);
    return fail(detail);
  }

  sqlite3 *source = save ? memory : file;
  sqlite3 *target = save ? file : memory;
  sqlite3_backup *copy = sqlite3_backup_init(target, "main", source, "main");
  if (!copy) {
    const QString detail = QString::fromUtf8(sqlite3_errmsg(target));
    sqlite3_close(file);
    return fail(detail);
  }

  // Stop retrying locks after ten seconds, including SQLite's own busy waits.
  // Successful steps can continue past the deadline for a large database.
  QElapsedTimer timeout;
  timeout.start();
  int stepRc;
  do {
    stepRc = sqlite3_backup_step(copy, 10000);
    if (!mainApp->isNoDebugOutput()) {
      qDebug() << stepRc << "backup" << sqlite3_backup_pagecount(copy)
               << "remain" << sqlite3_backup_remaining(copy);
    }
    if (stepRc == SQLITE_BUSY || stepRc == SQLITE_LOCKED) {
      if (timeout.elapsed() >= 10000) break;
      sqlite3_sleep(100);
    }
  } while (stepRc == SQLITE_OK || stepRc == SQLITE_BUSY || stepRc == SQLITE_LOCKED);

  // Finish exactly once after every successful init, including incomplete
  // copies. SQLITE_OK from finish alone does not mean the copy completed.
  const int finishRc = sqlite3_backup_finish(copy);
  const QString detail = QString::fromUtf8(sqlite3_errmsg(target));
  const int closeRc = sqlite3_close(file);
  if (closeRc != SQLITE_OK) sqlite3_close_v2(file);
  if (stepRc != SQLITE_DONE || finishRc != SQLITE_OK || closeRc != SQLITE_OK) {
    return fail(tr("%1 (copy: %2, finish: %3, close: %4)")
                .arg(detail).arg(stepRc).arg(finishRc).arg(closeRc));
  }
  qInfo() << "Database copy completed:" << path << (save ? "saved" : "loaded");
  return true;
}

void Database::setVacuum()
{
  {
    QSqlDatabase dbFile = QSqlDatabase::addDatabase("QSQLITE", "vacuum");
    dbFile.setDatabaseName(mainApp->dbFileName());
    dbFile.open();
    setPragma(dbFile);
    QSqlQuery(dbFile).exec("VACUUM");
    dbFile.close();
  }
  QSqlDatabase::removeDatabase("vacuum");
}

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DATABASEBACKUP_H
#define DATABASEBACKUP_H

#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QDateTime>
class QWidget;

// All user backups and pre-upgrade safety snapshots pass through this service.
class DatabaseBackup : public QObject
{
  Q_OBJECT
public:
  enum class Trigger { Manual, Subscription, Schedule, Exit, Upgrade, Prerelease };
  struct Result { QString directory; QString error; bool skipped = false; };
  static DatabaseBackup *instance();
  static QString directory();
  static void skipUpgradeBackup(); // Explicit startup choice; this session only.
  static Result create(QSqlDatabase db, Trigger trigger, int cleanOverride = -1);
  static void report(const Result &result, bool manual, QWidget *parent = nullptr);
  static void subscriptionsChanged(); // May be called from the import worker.
  void start();
  void stop();
  void manual(QWidget *parent);
private:
  explicit DatabaseBackup(QObject *parent);
  void scheduled();
  QTimer scheduleTimer_;
  QTimer subscriptionTimer_;
  bool stopped_ = false;
  QDateTime nextScheduleCheck_;
};
#endif

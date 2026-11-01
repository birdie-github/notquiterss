// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QDateTime>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QUrl>

class QTreeWidget;
class QDialogButtonBox;

// Uses the wizard's existing RequestFeed worker, one probe at a time.
class FeedDiscovery final : public QDialog
{
  Q_OBJECT
public:
  FeedDiscovery(const QByteArray &html, const QString &charset, const QUrl &page, int scanId,
                QWidget *parent);
  QStringList selection() const;

  void received(int result, int id, QString requested, QString error, QByteArray data,
                QDateTime date, QString charset, QUrl effective);
signals:
  void stopRequests();
  void requestUrl(int id, QString url, QDateTime date, QString credentials);

private:
  void next();
  void finishScan();
  const int scanId_;
  QList<QUrl> pending_;
  QSet<QUrl> found_;
  QTreeWidget *list_;
  QDialogButtonBox *buttons_;
  class QLabel *status_;
  bool incomplete_ = false;
  bool finished_ = false;
};

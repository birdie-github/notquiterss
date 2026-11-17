#ifndef STATUSBARCONTROLLER_H
#define STATUSBARCONTROLLER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>

class QAction;
class QLabel;
class FeedProgressBar;
class QWidget;
class QStatusBar;

class StatusBarController : public QObject
{
  Q_OBJECT
public:
  StatusBarController(QStatusBar *statusBar, QAction *stopUpdateAction,
                      QAction *loadImagesAction, QAction *fullScreenAction,
                      QObject *parent = 0);

  void queueFeed(int feedId, const QString &name);
  void setFeedStage(int feedId, const QString &stage);
  void finishFeed(int feedId);
  void hideProgress();

  void showMessage(const QString &message, int timeout = 0);
  void setCountTexts(const QString &unreadText, const QString &allText);
  QString unreadText() const;
  QString allText() const;
  void setCountsVisible(bool unreadVisible, bool allVisible);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void refreshProgress();
  struct FeedProgress {
    QString name;
    QString stage;
  };
  QMap<int, FeedProgress> pendingFeeds_;
  QList<int> activeFeeds_;
  int totalFeeds_ = 0;
  int completedFeeds_ = 0;
  QWidget *progressWidget_;
  QStatusBar *statusBar_;
  FeedProgressBar *progressBar_;
  QLabel *statusUnread_;
  QLabel *statusAll_;
};

#endif // STATUSBARCONTROLLER_H

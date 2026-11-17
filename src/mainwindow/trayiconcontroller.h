#ifndef TRAYICONCONTROLLER_H
#define TRAYICONCONTROLLER_H

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QWidget;

class TrayIconController : public QObject
{
public:
  TrayIconController(QWidget *menuParent, QAction *showWindowAction,
                     QAction *addFeedAction, QAction *updateAllFeedsAction,
                     QAction *markAllFeedsReadAction, QAction *optionsAction,
                     QAction *exitAction, QObject *parent = 0);

  QSystemTrayIcon *systemTrayIcon() const;
  void activateMenu();
  QString toolTip() const;
  void setToolTip(const QString &text);
  void show();
  void hide();
  void showMessage(const QString &title, const QString &message);
  void showDefaultIcon();
  void showNewNewsIcon();
  void showCountIcon(int count);

private:
  QSystemTrayIcon *trayIcon_;
  QMenu *trayMenu_;
};

#endif // TRAYICONCONTROLLER_H

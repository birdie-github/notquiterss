// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEARCHLINEEDIT_H
#define SEARCHLINEEDIT_H

#include <QActionGroup>
#include <QHash>
#include <QLineEdit>

class QAction;
class QFocusEvent;
class QKeyEvent;
class QLabel;
class QMenu;
class QResizeEvent;
class QToolButton;

/**
 * Single-line search input with a mode-selector dropdown button on the left,
 * an inline placeholder label, and a clear button on the right.
 */
class SearchLineEdit : public QLineEdit
{
  Q_OBJECT
public:
  explicit SearchLineEdit(const QString &defaultButtonIcon,
                          bool clearOnEscape,
                          QWidget *parent = nullptr);

  QActionGroup *findGroup_ = nullptr;

signals:
  void signalClear();
  void signalSelectFind();
  void signalVisible(bool);

protected:
  QAction *addSearchAction(const QString &objectName,
                           const QString &menuIconPath = QString(),
                           const QString &buttonIconPath = QString(),
                           bool checked = false,
                           bool separatorBefore = false);
  void setPlaceholderLabel(const QString &text);
  void refreshLabel();

  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;

private slots:
  void updateClearButton(const QString &text);
  void slotClear();
  void slotMenuFind();
  void slotSelectFind(QAction *act);

private:
  QString defaultButtonIcon_;
  bool clearOnEscape_ = false;
  QMenu *findMenu_ = nullptr;
  QLabel *findLabel_ = nullptr;
  QToolButton *findButton_ = nullptr;
  QToolButton *clearButton_ = nullptr;
  QHash<QAction *, QString> buttonIcons_;
};

#endif // SEARCHLINEEDIT_H

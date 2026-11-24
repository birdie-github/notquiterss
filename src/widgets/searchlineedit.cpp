// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchlineedit.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>

SearchLineEdit::SearchLineEdit(const QString &defaultButtonIcon,
                               bool clearOnEscape,
                               QWidget *parent)
  : QLineEdit(parent)
  , defaultButtonIcon_(defaultButtonIcon)
  , clearOnEscape_(clearOnEscape)
{
  findGroup_ = new QActionGroup(this);
  findGroup_->setExclusive(true);
  findMenu_ = new QMenu(this);

  findButton_ = new QToolButton(this);
  findButton_->setFocusPolicy(Qt::NoFocus);
  const QPixmap findPixmap(defaultButtonIcon_);
  findButton_->setIcon(QIcon(findPixmap));
  findButton_->setIconSize(findPixmap.size());
  findButton_->setCursor(Qt::ArrowCursor);
  findButton_->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0px; background: none; }"));

  connect(findButton_, &QAbstractButton::clicked, this, &SearchLineEdit::slotMenuFind);
  connect(findGroup_, &QActionGroup::triggered, this, &SearchLineEdit::slotSelectFind);

  clearButton_ = new QToolButton(this);
  clearButton_->setFocusPolicy(Qt::NoFocus);
  const QPixmap clearPixmap(QStringLiteral(":/images/editClear"));
  clearButton_->setIcon(QIcon(clearPixmap));
  clearButton_->setIconSize(clearPixmap.size());
  clearButton_->setCursor(Qt::ArrowCursor);
  clearButton_->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0px; background: none; }"));
  clearButton_->hide();
  connect(clearButton_, &QAbstractButton::clicked, this, &SearchLineEdit::slotClear);
  connect(this, &QLineEdit::textChanged, this, &SearchLineEdit::updateClearButton);

  findLabel_ = new QLabel(this);
  findLabel_->setStyleSheet(QStringLiteral("QLabel { color: gray; background: none; }"));

  const int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
  setStyleSheet(QStringLiteral("QLineEdit { padding-right: %1px; padding-left: %2px; }")
                    .arg(clearButton_->sizeHint().width() + frameWidth + 1)
                    .arg(findButton_->sizeHint().width() + frameWidth + 1));
  const QSize msz = minimumSizeHint();
  setMinimumSize(
      qMax(msz.width(), clearButton_->sizeHint().height() + findButton_->sizeHint().height() + frameWidth * 2 + 2),
      qMax(msz.height(), clearButton_->sizeHint().height() + frameWidth * 2 + 2));
}

QAction *SearchLineEdit::addSearchAction(const QString &objectName,
                                         const QString &menuIconPath,
                                         const QString &buttonIconPath,
                                         bool checked,
                                         bool separatorBefore)
{
  if (separatorBefore) findMenu_->addSeparator();
  auto *action = new QAction(this);
  action->setObjectName(objectName);
  if (!menuIconPath.isEmpty()) action->setIcon(QIcon(menuIconPath));
  action->setCheckable(true);
  if (checked) action->setChecked(true);
  if (!buttonIconPath.isEmpty()) buttonIcons_.insert(action, buttonIconPath);
  findGroup_->addAction(action);
  findMenu_->addAction(action);
  return action;
}

void SearchLineEdit::setPlaceholderLabel(const QString &text)
{
  findLabel_->setText(text);
  if (findLabel_->isVisible()) {
    findLabel_->hide();
    findLabel_->show();
  }
}

void SearchLineEdit::refreshLabel()
{
  if (QAction *checked = findGroup_->checkedAction()) {
    setPlaceholderLabel(checked->text());
  }
}

void SearchLineEdit::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    if (clearOnEscape_) slotClear();
    emit signalVisible(false);
  }
  QLineEdit::keyPressEvent(event);
}

void SearchLineEdit::resizeEvent(QResizeEvent *event)
{
  QLineEdit::resizeEvent(event);
  const int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
  QSize sz = findButton_->sizeHint();
  findButton_->move(frameWidth + 3, (rect().bottom() + 1 - sz.height()) / 2);
  sz = findLabel_->sizeHint();
  findLabel_->move(frameWidth + findButton_->sizeHint().width() + 5,
                   (rect().bottom() + 1 - sz.height()) / 2);
  sz = clearButton_->sizeHint();
  clearButton_->move(rect().right() - frameWidth - sz.width(),
                     (rect().bottom() + 1 - sz.height()) / 2);
}

void SearchLineEdit::focusInEvent(QFocusEvent *event)
{
  findLabel_->setVisible(false);
  QLineEdit::focusInEvent(event);
}

void SearchLineEdit::focusOutEvent(QFocusEvent *event)
{
  if (text().isEmpty())
    findLabel_->setVisible(true);
  QLineEdit::focusOutEvent(event);
}

void SearchLineEdit::updateClearButton(const QString &text)
{
  clearButton_->setVisible(!text.isEmpty());
  if (!hasFocus())
    findLabel_->setVisible(text.isEmpty());
}

void SearchLineEdit::slotClear()
{
  clear();
  emit signalClear();
}

void SearchLineEdit::slotMenuFind()
{
  findMenu_->popup(mapToGlobal(QPoint(0, height() - 1)));
}

void SearchLineEdit::slotSelectFind(QAction *act)
{
  findButton_->setIcon(QIcon(buttonIcons_.value(act, defaultButtonIcon_)));
  setPlaceholderLabel(act->text());
  emit signalSelectFind();
}

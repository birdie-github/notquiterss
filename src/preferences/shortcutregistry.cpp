#include "shortcutregistry.h"

#include <QAction>
#include <QKeySequence>

#include "settings.h"

void ShortcutRegistry::load()
{
  Settings settings("/Shortcuts");

  QListIterator<QAction *> iter(actions_);
  while (iter.hasNext()) {
    QAction *pAction = iter.next();
    if (pAction->objectName().isEmpty())
      continue;

    defaults_.append(pAction->shortcut().toString());

    const QString& sKey = '/' + pAction->objectName();
    const QString& sValue = settings.value('/' + sKey, pAction->shortcut().toString()).toString();
    pAction->setShortcut(QKeySequence(sValue));
  }
}

void ShortcutRegistry::save() const
{
  Settings settings("/Shortcuts/");

  QListIterator<QAction *> iter(actions_);
  while (iter.hasNext()) {
    QAction *pAction = iter.next();
    if (pAction->objectName().isEmpty())
      continue;

    const QString& sKey = '/' + pAction->objectName();
    const QString& sValue = QString(pAction->shortcut().toString());
    settings.setValue(sKey, sValue);
  }
}

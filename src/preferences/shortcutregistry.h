#ifndef SHORTCUTREGISTRY_H
#define SHORTCUTREGISTRY_H

#include <QList>
#include <QStringList>

class QAction;

// Non-owning registry: action creation, connections and deletion stay with
// the UI coordinator. Keep order and duplicates for shortcut-editor rows.
class ShortcutRegistry
{
public:
  void append(QAction *action) { actions_.append(action); }
  void append(const QList<QAction *> &actions) { actions_.append(actions); }
  void removeOne(QAction *action) { actions_.removeOne(action); }
  const QList<QAction *> &actions() const { return actions_; }
  const QStringList &defaults() const { return defaults_; }

  // Called once, after built-in shortcuts and registration are complete.
  void load();
  void save() const;

private:
  QList<QAction *> actions_;
  QStringList defaults_;
};

#endif // SHORTCUTREGISTRY_H

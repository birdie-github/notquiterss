#ifndef APPLICATIONSTYLE_H
#define APPLICATIONSTYLE_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QPalette>
#include <QString>

struct ApplicationStyle {
  enum Mode {
    System,
    Fixed
  };

  QString id;
  QString name;
  QString fileName;
  QString sheet;
  Mode mode = System;
  QHash<int, QColor> paletteColors;
  bool isDefault = false;
  bool builtIn = false;

  bool followsSystem() const { return mode == System; }
};

// Reads the runtime directory each time; no cached file list or widget ownership.
namespace ApplicationStyles {
QList<ApplicationStyle> discover(const QString &directory);
// Built-ins first; external files replace matching IDs without duplicate entries.
QList<ApplicationStyle> available(const QString &directory);
ApplicationStyle automaticDefault();
QPalette palette(const ApplicationStyle &style, const QPalette &fallback);
// Fixed themes prepend shared, palette-derived controls; System keeps its QSS.
QString styleSheet(const ApplicationStyle &style, const QPalette &palette);
QString automaticId();
}

#endif

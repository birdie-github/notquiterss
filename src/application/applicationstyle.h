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

  bool followsSystem() const { return mode == System; }
};

// Reads the runtime directory each time; no cached file list or widget ownership.
namespace ApplicationStyles {
QList<ApplicationStyle> discover(const QString &directory);
ApplicationStyle automaticDefault();
QPalette palette(const ApplicationStyle &style, const QPalette &fallback);
QString automaticId();
}

#endif

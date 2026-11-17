#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include <QStringList>

namespace CommandLine {
struct Options {
  bool debug = false;
  bool help = false;
  bool version = false;
  QStringList messages;
  QString error;
};
Options parse(const QStringList &arguments);
QString helpText();
}

#endif

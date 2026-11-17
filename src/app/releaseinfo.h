#ifndef RELEASEINFO_H
#define RELEASEINFO_H

#include <QByteArray>
#include <QString>

struct ReleaseInfo {
  bool valid = false;
  bool newer = false;
  QString version;
  QString notes;
};

// Accept only stable releases with numeric version tags; never interpret an API
// error object or an unparseable version as an available update.
ReleaseInfo parseReleaseInfo(const QByteArray &json, const QString &installedVersion);

#endif

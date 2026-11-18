/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#ifndef LANCZOSSCALER_H
#define LANCZOSSCALER_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <memory>

class ArticleView;

class LanczosScaler : public QObject {
  Q_OBJECT
public:
  explicit LanczosScaler(ArticleView *view);
  ~LanczosScaler() override;

  static QImage scale(const QImage &src, const QSize &targetSize);

  void notifyLayoutChange();
  bool lookupOrSchedule(const QUrl &url, const QImage &source,
                        const QSize &targetPhysicalSize, qreal dpr, QPixmap *out);
  void invalidateUrl(const QUrl &url);
  void reset();

private:
  struct CompletionState;
  static QImage scaleCancellable(const QImage &src, const QSize &targetSize,
                                 const std::atomic_bool *canceled);
  void flushScheduled();
  static QString cacheKey(qint64 sourceKey, const QSize &targetSize);

  struct Job {
    QUrl url;
    QImage source;
    QSize targetSize;
    qreal dpr = 1.0;
    QString key;
  };

  ArticleView *view_;
  QTimer *debounceTimer_;
  QHash<QString, Job> scheduled_;
  QSet<QString> attempted_;
  std::shared_ptr<CompletionState> completion_;
  std::shared_ptr<std::atomic_bool> activeCancellation_;
  QString activeKey_;
  bool running_ = false;
  int generation_ = 0;
};

#endif // LANCZOSSCALER_H

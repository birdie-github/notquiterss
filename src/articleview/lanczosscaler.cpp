/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
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
#include "lanczosscaler.h"
#include "articleview.h"

#include <QMetaObject>
#include <QPixmapCache>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QVector>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLanczosA = 3.0;
constexpr int kFixedShift = 14;
constexpr int kFixedOne = 1 << kFixedShift;

inline double sinc(double x) {
  if (std::abs(x) < 1e-8) return 1.0;
  const double pix = kPi * x;
  return std::sin(pix) / pix;
}

inline double lanczos3(double x) {
  x = std::abs(x);
  if (x >= kLanczosA) return 0.0;
  return sinc(x) * sinc(x / kLanczosA);
}

struct KernelTable {
  QVector<int> offsets;
  QVector<int> counts;
  QVector<int> weightsOffset;
  QVector<qint16> weights;

  KernelTable(int srcSize, int dstSize)
    : offsets(dstSize), counts(dstSize), weightsOffset(dstSize)
  {
    const double scale = double(srcSize) / double(dstSize);
    const double support = std::max(1.0, scale) * kLanczosA;
    const double invSupportScale = 1.0 / std::max(1.0, scale);
    const int maxTaps = int(std::ceil(support * 2.0)) + 2;
    weights.reserve(dstSize * maxTaps);

    QVector<double> rawWeights;
    rawWeights.reserve(maxTaps);

    for (int dst = 0; dst < dstSize; ++dst) {
      const double center = (dst + 0.5) * scale - 0.5;
      int left = int(std::floor(center - support));
      int right = int(std::ceil(center + support));
      left = qBound(0, left, srcSize - 1);
      right = qBound(0, right, srcSize - 1);

      rawWeights.clear();
      double sum = 0.0;
      for (int src = left; src <= right; ++src) {
        const double w = lanczos3((src - center) * invSupportScale);
        rawWeights.append(w);
        sum += w;
      }

      const int count = right - left + 1;
      offsets[dst] = left;
      counts[dst] = count;
      weightsOffset[dst] = weights.size();

      int fixedSum = 0;
      int maxIdx = 0;
      double maxWeight = -1.0;
      for (int i = 0; i < count; ++i) {
        const double normalized = (sum != 0.0) ? (rawWeights[i] / sum) : (1.0 / count);
        const qint16 fw = qint16(qBound(-32768, int(std::lround(normalized * kFixedOne)), 32767));
        weights.append(fw);
        fixedSum += fw;
        if (rawWeights[i] > maxWeight) {
          maxWeight = rawWeights[i];
          maxIdx = i;
        }
      }
      if (count > 0 && fixedSum != kFixedOne) {
        weights[weightsOffset[dst] + maxIdx] =
            qint16(weights[weightsOffset[dst] + maxIdx] + (kFixedOne - fixedSum));
      }
    }
  }
};
} // namespace

QImage LanczosScaler::scale(const QImage &input, const QSize &targetSize) {
  if (input.isNull() || !targetSize.isValid() ||
      targetSize.width() <= 0 || targetSize.height() <= 0) {
    return QImage();
  }
  if (input.size() == targetSize)
    return input;

  const QImage::Format targetFormat = input.hasAlphaChannel()
      ? QImage::Format_ARGB32_Premultiplied
      : QImage::Format_RGB32;
  const QImage src = (input.format() == targetFormat)
      ? input
      : input.convertToFormat(targetFormat);

  const int srcW = src.width();
  const int srcH = src.height();
  const int dstW = targetSize.width();
  const int dstH = targetSize.height();

  const KernelTable hTable(srcW, dstW);
  const KernelTable vTable(srcH, dstH);

  // Intermediate buffer after horizontal pass: srcH rows of dstW pixels.
  QVector<QRgb> temp(size_t(srcH) * size_t(dstW));
  const int *hOffsets = hTable.offsets.constData();
  const int *hCounts = hTable.counts.constData();
  const int *hWeightOffsets = hTable.weightsOffset.constData();
  const qint16 *hWeights = hTable.weights.constData();

  for (int y = 0; y < srcH; ++y) {
    const QRgb *srcRow = reinterpret_cast<const QRgb *>(src.constScanLine(y));
    QRgb *dstRow = temp.data() + size_t(y) * size_t(dstW);

    for (int x = 0; x < dstW; ++x) {
      const QRgb *s = srcRow + hOffsets[x];
      const qint16 *w = hWeights + hWeightOffsets[x];
      const int count = hCounts[x];

      int r = 1 << (kFixedShift - 1);
      int g = 1 << (kFixedShift - 1);
      int b = 1 << (kFixedShift - 1);
      int a = 1 << (kFixedShift - 1);

      for (int i = 0; i < count; ++i) {
        const QRgb px = s[i];
        const int weight = w[i];
        a += int((px >> 24) & 0xff) * weight;
        r += int((px >> 16) & 0xff) * weight;
        g += int((px >> 8) & 0xff) * weight;
        b += int(px & 0xff) * weight;
      }

      const int outA = qBound(0, a >> kFixedShift, 255);
      const int outR = qBound(0, r >> kFixedShift, outA);
      const int outG = qBound(0, g >> kFixedShift, outA);
      const int outB = qBound(0, b >> kFixedShift, outA);
      dstRow[x] = (uint(outA) << 24) | (uint(outR) << 16) | (uint(outG) << 8) | uint(outB);
    }
  }

  QImage dst(dstW, dstH, targetFormat);
  const int *vOffsets = vTable.offsets.constData();
  const int *vCounts = vTable.counts.constData();
  const int *vWeightOffsets = vTable.weightsOffset.constData();
  const qint16 *vWeights = vTable.weights.constData();
  const QRgb *tempData = temp.constData();

  for (int y = 0; y < dstH; ++y) {
    QRgb *dstRow = reinterpret_cast<QRgb *>(dst.scanLine(y));
    const int top = vOffsets[y];
    const int count = vCounts[y];
    const qint16 *w = vWeights + vWeightOffsets[y];

    for (int x = 0; x < dstW; ++x) {
      int r = 1 << (kFixedShift - 1);
      int g = 1 << (kFixedShift - 1);
      int b = 1 << (kFixedShift - 1);
      int a = 1 << (kFixedShift - 1);

      const QRgb *colPtr = tempData + size_t(top) * size_t(dstW) + x;
      for (int i = 0; i < count; ++i) {
        const QRgb px = *colPtr;
        colPtr += dstW;
        const int weight = w[i];
        a += int((px >> 24) & 0xff) * weight;
        r += int((px >> 16) & 0xff) * weight;
        g += int((px >> 8) & 0xff) * weight;
        b += int(px & 0xff) * weight;
      }

      const int outA = qBound(0, a >> kFixedShift, 255);
      const int outR = qBound(0, r >> kFixedShift, outA);
      const int outG = qBound(0, g >> kFixedShift, outA);
      const int outB = qBound(0, b >> kFixedShift, outA);
      dstRow[x] = (uint(outA) << 24) | (uint(outR) << 16) | (uint(outG) << 8) | uint(outB);
    }
  }

  return dst;
}

LanczosScaler::LanczosScaler(ArticleView *view)
  : QObject(view)
  , view_(view)
  , debounceTimer_(new QTimer(this))
{
  debounceTimer_->setSingleShot(true);
  debounceTimer_->setInterval(80);
  connect(debounceTimer_, &QTimer::timeout, this, &LanczosScaler::flushScheduled);
}

LanczosScaler::~LanczosScaler() = default;

QString LanczosScaler::cacheKey(const QUrl &url, qint64 sourceKey, const QSize &targetSize) {
  return QStringLiteral("nqrss-lanczos:%1:%2:%3x%4")
      .arg(url.toString())
      .arg(sourceKey)
      .arg(targetSize.width())
      .arg(targetSize.height());
}

void LanczosScaler::notifyResize() {
  scheduled_.clear();
  debounceTimer_->start();
}

void LanczosScaler::invalidateUrl(const QUrl &url) {
  for (auto it = scheduled_.begin(); it != scheduled_.end();) {
    if (it.value().url == url) it = scheduled_.erase(it);
    else ++it;
  }
}

void LanczosScaler::reset() {
  ++generation_;
  debounceTimer_->stop();
  scheduled_.clear();
  inflight_.clear();
}

bool LanczosScaler::lookupOrSchedule(const QUrl &url, const QImage &source,
                                     const QSize &targetPhysicalSize, qreal dpr, QPixmap *out) {
  if (source.isNull() || !targetPhysicalSize.isValid() ||
      targetPhysicalSize.width() <= 0 || targetPhysicalSize.height() <= 0) {
    return false;
  }
  const QString key = cacheKey(url, source.cacheKey(), targetPhysicalSize);
  if (QPixmapCache::find(key, out)) {
    out->setDevicePixelRatio(dpr);
    return true;
  }
  if (inflight_.contains(key))
    return false;

  scheduled_.insert(key, {url, source, targetPhysicalSize, dpr, key});
  if (!debounceTimer_->isActive())
    flushScheduled();
  return false;
}

void LanczosScaler::flushScheduled() {
  if (scheduled_.isEmpty()) return;
  const QList<Job> jobs = scheduled_.values();
  scheduled_.clear();

  const int gen = generation_;
  QPointer<LanczosScaler> self(this);

  for (const Job &job : jobs) {
    if (inflight_.contains(job.key)) continue;
    inflight_.insert(job.key);

    QThreadPool::globalInstance()->start(QRunnable::create([self, gen, job]() {
      QImage scaled = LanczosScaler::scale(job.source, job.targetSize);
      scaled.setDevicePixelRatio(job.dpr);
      if (!self) return;
      QMetaObject::invokeMethod(self.data(), [self, gen, job, scaled]() {
        if (!self) return;
        self->inflight_.remove(job.key);
        if (gen != self->generation_ || scaled.isNull()) return;
        // Raise cache limit if needed so large article photos are not evicted immediately.
        if (QPixmapCache::cacheLimit() < 65536)
          QPixmapCache::setCacheLimit(65536);
        QPixmap pixmap = QPixmap::fromImage(scaled);
        pixmap.setDevicePixelRatio(job.dpr);
        QPixmapCache::insert(job.key, pixmap);
        if (self->view_ && self->view_->viewport())
          self->view_->viewport()->update();
      }, Qt::QueuedConnection);
    }));
  }
}

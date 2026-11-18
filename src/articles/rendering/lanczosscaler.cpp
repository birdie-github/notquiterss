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
#include "lanczosscaler.h"
#include "articleview.h"

#include <QMetaObject>
#include <QPixmapCache>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLanczosA = 3.0;
constexpr int kFixedShift = 14;
constexpr int kFixedOne = 1 << kFixedShift;
constexpr qint64 kMaxWorkingBytes = 64LL * 1024 * 1024;
constexpr qint64 kPixelBytes = sizeof(QRgb);

// Signed channels retain Lanczos undershoot and overshoot between passes.
struct IntermediatePixel { qint16 r, g, b, a; };
constexpr int kIntermediateShift = 4;

double kernelSupport(int srcSize, int dstSize) {
  return std::max(1.0, double(srcSize) / dstSize) * kLanczosA;
}

qint64 kernelMaxTaps(double support) {
  return qint64(std::ceil(support * 2.0)) + 2;
}

qint64 kernelBytes(int srcSize, int dstSize) {
  const qint64 taps = kernelMaxTaps(kernelSupport(srcSize, dstSize));
  return qint64(dstSize) * (3 * qint64(sizeof(int)) + taps * qint64(sizeof(qint16))) +
      taps * qint64(sizeof(double));
}

QImage::Format workingFormat(const QImage &source) {
  return source.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
}

bool fitsWorkingBudget(const QImage &source, const QSize &targetSize) {
  if (source.isNull() || targetSize.isEmpty()) return false;

  const qint64 intermediatePixels = qint64(source.height()) * targetSize.width();
  const qint64 outputPixels = qint64(targetSize.width()) * targetSize.height();
  const qint64 conversionPixels = source.format() == workingFormat(source)
      ? 0 : qint64(source.width()) * source.height();
  // Check each plane before multiplying by bytes per pixel. The bound also
  // keeps vector sizes and kernel offsets representable by int on Qt 5.
  constexpr qint64 maxPixels = kMaxWorkingBytes / kPixelBytes;
  if (intermediatePixels > maxPixels || outputPixels > maxPixels || conversionPixels > maxPixels)
    return false;

  const qint64 pixelBytes = intermediatePixels * qint64(sizeof(IntermediatePixel)) +
      (outputPixels + conversionPixels) * kPixelBytes;
  // Include both table reservations and each axis's temporary weight buffer
  // for a conservative estimate of the worker's additional allocations.
  return pixelBytes +
      kernelBytes(source.width(), targetSize.width()) +
      kernelBytes(source.height(), targetSize.height()) <= kMaxWorkingBytes;
}

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
    const double support = kernelSupport(srcSize, dstSize);
    const double invSupportScale = 1.0 / std::max(1.0, scale);
    const int maxTaps = int(kernelMaxTaps(support));
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

struct LanczosScaler::CompletionState {
  QMutex mutex;
  LanczosScaler *receiver = nullptr; // Access only while holding mutex.
};

QImage LanczosScaler::scale(const QImage &input, const QSize &targetSize) {
  return scaleCancellable(input, targetSize, nullptr);
}

QImage LanczosScaler::scaleCancellable(const QImage &input, const QSize &targetSize,
                                      const std::atomic_bool *canceled) {
  const auto isCanceled = [canceled] {
    return canceled && canceled->load(std::memory_order_relaxed);
  };
  if (isCanceled() || input.isNull() || targetSize.isEmpty()) return QImage();
  if (input.size() == targetSize)
    return input;
  if (!fitsWorkingBudget(input, targetSize)) return QImage();

  const QImage::Format targetFormat = workingFormat(input);
  const QImage src = (input.format() == targetFormat)
      ? input
      : input.convertToFormat(targetFormat);

  const int srcW = src.width();
  const int srcH = src.height();
  const int dstW = targetSize.width();
  const int dstH = targetSize.height();

  if (isCanceled() || src.isNull()) return QImage();
  const KernelTable hTable(srcW, dstW);
  const KernelTable vTable(srcH, dstH);

  // Intermediate buffer after horizontal pass: srcH rows of dstW pixels.
  if (isCanceled()) return QImage();
  const int intermediatePixels = int(qint64(srcH) * dstW);
  QVector<IntermediatePixel> temp(intermediatePixels);
  const int *hOffsets = hTable.offsets.constData();
  const int *hCounts = hTable.counts.constData();
  const int *hWeightOffsets = hTable.weightsOffset.constData();
  const qint16 *hWeights = hTable.weights.constData();

  for (int y = 0; y < srcH; ++y) {
    if (isCanceled()) return QImage();
    const QRgb *srcRow = reinterpret_cast<const QRgb *>(src.constScanLine(y));
    IntermediatePixel *dstRow = temp.data() + size_t(y) * size_t(dstW);

    for (int x = 0; x < dstW; ++x) {
      const QRgb *s = srcRow + hOffsets[x];
      const qint16 *w = hWeights + hWeightOffsets[x];
      const int count = hCounts[x];

      int r = 1 << (kFixedShift - kIntermediateShift - 1);
      int g = 1 << (kFixedShift - kIntermediateShift - 1);
      int b = 1 << (kFixedShift - kIntermediateShift - 1);
      int a = 1 << (kFixedShift - kIntermediateShift - 1);

      for (int i = 0; i < count; ++i) {
        const QRgb px = s[i];
        const int weight = w[i];
        a += int((px >> 24) & 0xff) * weight;
        r += int((px >> 16) & 0xff) * weight;
        g += int((px >> 8) & 0xff) * weight;
        b += int(px & 0xff) * weight;
      }

      constexpr int shift = kFixedShift - kIntermediateShift;
      dstRow[x] = {qint16(r >> shift), qint16(g >> shift),
                   qint16(b >> shift), qint16(a >> shift)};
    }
  }

  if (isCanceled()) return QImage();
  QImage dst(dstW, dstH, targetFormat);
  if (dst.isNull()) return QImage();
  const int *vOffsets = vTable.offsets.constData();
  const int *vCounts = vTable.counts.constData();
  const int *vWeightOffsets = vTable.weightsOffset.constData();
  const qint16 *vWeights = vTable.weights.constData();
  const IntermediatePixel *tempData = temp.constData();

  for (int y = 0; y < dstH; ++y) {
    if (isCanceled()) return QImage();
    QRgb *dstRow = reinterpret_cast<QRgb *>(dst.scanLine(y));
    const int top = vOffsets[y];
    const int count = vCounts[y];
    const qint16 *w = vWeights + vWeightOffsets[y];

    for (int x = 0; x < dstW; ++x) {
      constexpr int shift = kFixedShift + kIntermediateShift;
      qint64 r = 1 << (shift - 1);
      qint64 g = r, b = r, a = r;

      for (int i = 0; i < count; ++i) {
        const IntermediatePixel &px = tempData[size_t(top + i) * dstW + x];
        const int weight = w[i];
        r += px.r * weight;
        g += px.g * weight;
        b += px.b * weight;
        a += px.a * weight;
      }

      const int outA = qBound(0, int(a >> shift), 255);
      const int outR = qBound(0, int(r >> shift), outA);
      const int outG = qBound(0, int(g >> shift), outA);
      const int outB = qBound(0, int(b >> shift), outA);
      dstRow[x] = (uint(outA) << 24) | (uint(outR) << 16) | (uint(outG) << 8) | uint(outB);
    }
  }

  return dst;
}

LanczosScaler::LanczosScaler(ArticleView *view)
  : QObject(view)
  , view_(view)
  , debounceTimer_(new QTimer(this))
  , completion_(std::make_shared<CompletionState>())
{
  completion_->receiver = this;
  debounceTimer_->setSingleShot(true);
  debounceTimer_->setInterval(80);
  connect(debounceTimer_, &QTimer::timeout, this, &LanczosScaler::flushScheduled);
}

LanczosScaler::~LanczosScaler() {
  if (activeCancellation_)
    activeCancellation_->store(true, std::memory_order_relaxed);
  // Serialize destruction with workers posting completion events. Workers own
  // this small state independently and never dereference a destroyed receiver.
  QMutexLocker lock(&completion_->mutex);
  completion_->receiver = nullptr;
}

QString LanczosScaler::cacheKey(qint64 sourceKey, const QSize &targetSize) {
  return QStringLiteral("nqrss-lanczos:%1:%2x%3")
      .arg(sourceKey)
      .arg(targetSize.width())
      .arg(targetSize.height());
}

void LanczosScaler::notifyLayoutChange() {
  reset();
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
  attempted_.clear();
  activeKey_.clear();
  if (activeCancellation_)
    activeCancellation_->store(true, std::memory_order_relaxed);
}

bool LanczosScaler::lookupOrSchedule(const QUrl &url, const QImage &source,
                                     const QSize &targetPhysicalSize, qreal dpr, QPixmap *out) {
  if (!fitsWorkingBudget(source, targetPhysicalSize)) {
    return false;
  }
  const QString key = cacheKey(source.cacheKey(), targetPhysicalSize);
  if (QPixmapCache::find(key, out)) {
    out->setDevicePixelRatio(dpr);
    return true;
  }
  if (attempted_.contains(key) || activeKey_ == key)
    return false;

  // A successful cache insertion repaints the viewport, allowing visible
  // images to request work omitted by the queue limit.
  constexpr int maxPendingJobs = 16;
  if (!scheduled_.contains(key) && scheduled_.size() >= maxPendingJobs)
    return false;
  scheduled_.insert(key, {url, source, targetPhysicalSize, dpr, key});
  if (!debounceTimer_->isActive())
    flushScheduled();
  return false;
}

void LanczosScaler::flushScheduled() {
  if (running_ || debounceTimer_->isActive() || scheduled_.isEmpty()) return;
  auto it = scheduled_.begin();
  const Job job = it.value();
  scheduled_.erase(it);
  running_ = true;
  activeKey_ = job.key;
  activeCancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = activeCancellation_;
  const auto completion = completion_;
  const int gen = generation_;

  QThreadPool::globalInstance()->start(QRunnable::create([completion, canceled, gen, job]() {
    QImage scaled = LanczosScaler::scaleCancellable(job.source, job.targetSize, canceled.get());
    scaled.setDevicePixelRatio(job.dpr);
    QMutexLocker lock(&completion->mutex);
    LanczosScaler *receiver = completion->receiver;
    if (!receiver) return;
    // The lock keeps receiver alive until the event is posted. QObject removes
    // pending events if it is destroyed before delivery on the GUI thread.
    QMetaObject::invokeMethod(receiver, [receiver, gen, job, scaled]() {
      receiver->running_ = false;
      receiver->activeKey_.clear();
      receiver->activeCancellation_.reset();
      if (gen == receiver->generation_) {
        // A failed result or a later eviction must not turn the next paint
        // into another attempt at the same resize.
        receiver->attempted_.insert(job.key);
        if (!scaled.isNull()) {
          if (QPixmapCache::cacheLimit() < 65536)
            QPixmapCache::setCacheLimit(65536);
          QPixmap pixmap = QPixmap::fromImage(scaled);
          pixmap.setDevicePixelRatio(job.dpr);
          if (!pixmap.isNull() && QPixmapCache::insert(job.key, pixmap) &&
              receiver->view_ && receiver->view_->viewport()) {
            receiver->view_->viewport()->update();
          }
        }
      }
      receiver->flushScheduled();
    }, Qt::QueuedConnection);
  }));
}

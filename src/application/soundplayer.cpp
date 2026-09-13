// Compile the implementation once, before Qt's macros.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_NULL // Do not fall back to miniaudio's silent output backend.
#if defined(__APPLE__)
#define MA_NO_RUNTIME_LINKING // Use system frameworks for normal signing/notarization.
#endif
#include <miniaudio.h>

#if MA_VERSION_MAJOR != 0 || MA_VERSION_MINOR != 11 || MA_VERSION_REVISION < 25
#error "Use miniaudio 0.11.25 or a compatible 0.11.x update; see INSTALL."
#endif

#include "soundplayer.h"

#include <QDebug>
#include <QFile>
#include <QQueue>
#include <QTimer>
#include <memory>

// Control and file opening run here. Miniaudio's audio thread only renders;
// no Qt UI calls or sound destruction occur in its callbacks.
class SoundWorker : public QObject
{
  Q_OBJECT
public:
  SoundWorker()
    : pollTimer_(new QTimer(this)), idleTimer_(new QTimer(this))
  {
    pollTimer_->setInterval(50);
    connect(pollTimer_, &QTimer::timeout, this, &SoundWorker::poll);
    idleTimer_->setSingleShot(true);
    // EOF is rendered before the last device buffer is heard. Allow a tail
    // before releasing the device and its threads instead of cutting it short.
    idleTimer_->setInterval(500);
    connect(idleTimer_, &QTimer::timeout, this, [this]() { releaseEngine(); });
  }

  ~SoundWorker() override
  {
    pollTimer_->stop();
    idleTimer_->stop();
    releaseSound();
    releaseEngine();
  }

  void enqueue(const QString &path)
  {
    if (QThread::currentThread()->isInterruptionRequested()) return;
    pending_.enqueue(path);
    idleTimer_->stop();
    if (!sound_) startNext();
  }

signals:
  void started();
  void failed(const QString &path, const QString &errorText);

private:
  void report(const QString &operation, ma_result result)
  {
    emit failed(currentPath_, QStringLiteral("%1: %2 (%3)")
                .arg(operation, QString::fromUtf8(ma_result_description(result))).arg(result));
  }

  void releaseSound()
  {
    if (sound_) {
      ma_sound_uninit(sound_.get());
      sound_.reset();
    }
  }

  void releaseEngine()
  {
    if (engine_) {
      ma_engine_uninit(engine_.get());
      engine_.reset();
    }
  }

  void startNext()
  {
    while (!pending_.isEmpty() && !QThread::currentThread()->isInterruptionRequested()) {
      currentPath_ = pending_.dequeue();
      if (!engine_) {
        auto engine = std::make_unique<ma_engine>();
        ma_engine_config config = ma_engine_config_init();
        config.noAutoStart = MA_TRUE;
        ma_result result = ma_engine_init(&config, engine.get());
        if (result != MA_SUCCESS) {
          // A disconnected remote session may have no audio backend. Skip this
          // sound silently; the next request will retry initialization normally.
          if (result != MA_NO_BACKEND)
            report(QStringLiteral("Initialize audio output"), result);
          continue;
        }
        engine_ = std::move(engine);
      }

      auto sound = std::make_unique<ma_sound>();
      const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_WAIT_INIT |
                              MA_SOUND_FLAG_NO_SPATIALIZATION;
#if defined(Q_OS_WIN)
      const std::wstring fileName = currentPath_.toStdWString();
      ma_result result = ma_sound_init_from_file_w(engine_.get(), fileName.c_str(),
                                                   flags, nullptr, nullptr, sound.get());
#else
      const QByteArray fileName = QFile::encodeName(currentPath_);
      ma_result result = ma_sound_init_from_file(engine_.get(), fileName.constData(),
                                                 flags, nullptr, nullptr, sound.get());
#endif
      if (result != MA_SUCCESS) {
        report(QStringLiteral("Open/decode sound"), result);
        continue;
      }
      sound_ = std::move(sound);
      result = ma_sound_start(sound_.get());
      if (result == MA_SUCCESS)
        result = ma_engine_start(engine_.get());
      if (result != MA_SUCCESS) {
        report(QStringLiteral("Start playback"), result);
        releaseSound();
        releaseEngine();
        continue;
      }
      emit started();
      pollTimer_->start();
      return;
    }
    pollTimer_->stop();
    if (engine_) idleTimer_->start();
  }

  void poll()
  {
    const ma_result result = ma_resource_manager_data_source_result(
        sound_->pResourceManagerDataSource);
    if (result != MA_SUCCESS && result != MA_BUSY && result != MA_AT_END) {
      report(QStringLiteral("Decode sound"), result);
    } else if (!ma_device_is_started(ma_engine_get_device(engine_.get()))) {
      report(QStringLiteral("Audio output stopped"), MA_DEVICE_NOT_STARTED);
      releaseSound();
      releaseEngine();
      startNext();
      return;
    } else if (!ma_sound_at_end(sound_.get())) {
      return;
    }
    releaseSound();
    startNext();
  }

  QTimer *pollTimer_;
  QTimer *idleTimer_;
  QQueue<QString> pending_;
  QString currentPath_;
  std::unique_ptr<ma_engine> engine_;
  std::unique_ptr<ma_sound> sound_;
};

SoundPlayer::SoundPlayer(QObject *parent)
  : QObject(parent), worker_(new SoundWorker)
{
  worker_->moveToThread(&workerThread_);
  connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(worker_, &SoundWorker::started, this, [this]() { errorReported_ = false; });
  connect(worker_, &SoundWorker::failed, this,
          [this](const QString &path, const QString &errorText) {
    qWarning().noquote() << "Sound playback failed:" << path << errorText;
    // Log every failure, but avoid repeated popups during a failed feed update.
    if (!errorReported_) {
      errorReported_ = true;
      emit playbackFailed(path, errorText);
    }
  });
}

SoundPlayer::~SoundPlayer()
{
  // Drop pending requests. finished/deleteLater cleans up on the worker thread
  // before wait() returns, joining miniaudio's threads as part of engine teardown.
  if (workerThread_.isRunning()) {
    workerThread_.requestInterruption();
    workerThread_.quit();
    workerThread_.wait();
  } else {
    // No request has ever started the thread or initialized audio.
    delete worker_;
  }
}

void SoundPlayer::play(const QString &soundPath)
{
  if (!workerThread_.isRunning()) workerThread_.start();
  QMetaObject::invokeMethod(worker_, [worker = worker_, soundPath]() {
    worker->enqueue(soundPath);
  }, Qt::QueuedConnection);
}

#include "soundplayer.moc"

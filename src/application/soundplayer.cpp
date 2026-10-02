// Simple queued audio player using system tools (paplay/afplay/aplay).
// No external audio library dependency.

#include "soundplayer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QQueue>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>

namespace {

// Select the best available system audio player.
QString findPlayer()
{
  // Linux: PulseAudio → ALSA
#if defined(Q_OS_LINUX)
  if (!QStandardPaths::findExecutable("paplay").isEmpty())
    return "paplay";
  if (!QStandardPaths::findExecutable("aplay").isEmpty())
    return "aplay";
#endif
  // macOS
#if defined(Q_OS_MAC)
  if (!QStandardPaths::findExecutable("afplay").isEmpty())
    return "afplay";
#endif
  // Windows: PowerShell with .NET SoundPlayer (WAV/MP3)
#if defined(Q_OS_WIN)
  if (!QStandardPaths::findExecutable("powershell").isEmpty())
    return "powershell";
#endif
  return {};
}

// Format the command line for the selected player.
QString formatCommand(const QString &player, const QString &path)
{
#if defined(Q_OS_WIN)
  if (player == "powershell") {
    // Use .NET System.Media.SoundPlayer — supports WAV and MP3.
    // -NoProfile avoids slow profile loading; -Command runs the script.
    // Escape single quotes in the path for PowerShell.
    QString escaped = path.replace("'", "''");
    return QString("-NoProfile -Command \"(New-Object Media.SoundPlayer '%1').PlaySync()\"")
               .arg(escaped);
  }
#endif
  return {}; // Default: player is used as-is, path as argument.
}

} // namespace

class SoundWorker : public QObject
{
  Q_OBJECT
public:
  explicit SoundWorker(const QString &player, QObject *parent = nullptr)
    : QObject(parent)
    , player_(player)
    , runningProcess_(nullptr)
  {}

  void enqueue(const QString &path)
  {
    if (QThread::currentThread()->isInterruptionRequested()) return;
    pending_.enqueue(path);
    if (runningProcess_ == nullptr) startNext();
  }

signals:
  void started();
  void failed(const QString &path, const QString &errorText);

private slots:
  void onProcessFinished(int code, QProcess::ExitStatus)
  {
    if (runningProcess_) {
      runningProcess_->deleteLater();
      runningProcess_ = nullptr;
    }
    if (code != 0) {
      emit failed(currentPath_, QStringLiteral("Exit code %1").arg(code));
    } else {
      emit started();
    }
    startNext();
  }

  void onProcessError(QProcess::ProcessError error)
  {
    if (runningProcess_) {
      runningProcess_->deleteLater();
      runningProcess_ = nullptr;
    }
    QString msg;
    switch (error) {
      case QProcess::FailedToStart:
        msg = "Failed to start"; break;
      case QProcess::Crashed:
        msg = "Crashed"; break;
      case QProcess::Timedout:
        msg = "Timed out"; break;
      case QProcess::WriteError:
        msg = "Write error"; break;
      case QProcess::ReadError:
        msg = "Read error"; break;
      default:
        msg = "Unknown error"; break;
    }
    emit failed(currentPath_, msg);
    startNext();
  }

private:
  void startNext()
  {
    while (!pending_.isEmpty() && !QThread::currentThread()->isInterruptionRequested()) {
      currentPath_ = pending_.dequeue();
      if (player_.isEmpty()) {
        emit failed(currentPath_, "No audio player available");
        continue;
      }
      runningProcess_ = new QProcess(this);
      runningProcess_->setProcessChannelMode(QProcess::MergedChannels);
      runningProcess_->setReadChannel(QProcess::StandardError);
      QString cmdArgs = formatCommand(player_, currentPath_);
      if (!cmdArgs.isEmpty()) {
        runningProcess_->start(player_, {cmdArgs});
      } else {
        runningProcess_->start(player_, {currentPath_});
      }
      if (!runningProcess_->waitForStarted(5000)) {
        emit failed(currentPath_, "Failed to start player");
        runningProcess_->deleteLater();
        runningProcess_ = nullptr;
        continue;
      }
      connect(runningProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
              this, &SoundWorker::onProcessFinished);
      connect(runningProcess_, &QProcess::errorOccurred,
              this, &SoundWorker::onProcessError);
      return;
    }
  }

  QString player_;
  QQueue<QString> pending_;
  QString currentPath_;
  QProcess *runningProcess_;
};

SoundPlayer::SoundPlayer(QObject *parent)
  : QObject(parent)
  , worker_(new SoundWorker(findPlayer(), this))
{
  worker_->moveToThread(&workerThread_);
  connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(worker_, &SoundWorker::started, this, [this]() { errorReported_ = false; });
  connect(worker_, &SoundWorker::failed, this,
          [this](const QString &path, const QString &errorText) {
    qWarning().noquote() << "Sound playback failed:" << path << errorText;
    if (!errorReported_) {
      errorReported_ = true;
      emit playbackFailed(path, errorText);
    }
  });
}

SoundPlayer::~SoundPlayer()
{
  if (workerThread_.isRunning()) {
    workerThread_.requestInterruption();
    workerThread_.quit();
    workerThread_.wait();
  } else {
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

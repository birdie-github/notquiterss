#ifndef SOUNDPLAYER_H
#define SOUNDPLAYER_H

#include <QObject>
#include <QThread>

class SoundWorker;

class SoundPlayer : public QObject
{
  Q_OBJECT
public:
  explicit SoundPlayer(QObject *parent = nullptr);
  ~SoundPlayer() override;
  void play(const QString &soundPath);

signals:
  void playbackFailed(const QString &soundPath, const QString &errorText);

private:
  QThread workerThread_;
  SoundWorker *worker_;
  bool errorReported_ = false;
};

#endif // SOUNDPLAYER_H

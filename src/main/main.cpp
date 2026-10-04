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
#include "globals.h"
#include "mainapplication.h"
#include "logfile.h"
#include "commandline.h"
#include "projectmetadata.h"
#include <cstdio>
#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t sigtermWriteFd = -1;

void notifySigterm(int)
{
  const int savedErrno = errno;
  const int fd = sigtermWriteFd;
  if (fd >= 0) {
    const char byte = 0;
    ssize_t result;
    do {
      result = ::write(fd, &byte, 1);
    } while (result < 0 && errno == EINTR);
    // A full nonblocking pipe already contains a termination notification.
  }
  errno = savedErrno;
}

bool installSigtermHandler(int (&fds)[2])
{
  if (::pipe(fds) != 0) return false;
  for (int fd : fds) {
    if (::fcntl(fd, F_SETFL, O_NONBLOCK) == -1 ||
        ::fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
      ::close(fds[0]);
      ::close(fds[1]);
      return false;
    }
  }
  struct sigaction action {};
  action.sa_handler = notifySigterm;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigtermWriteFd = fds[1];
  if (::sigaction(SIGTERM, &action, nullptr) != 0) {
    sigtermWriteFd = -1;
    ::close(fds[0]);
    ::close(fds[1]);
    return false;
  }
  return true;
}
}
#endif

int main(int argc, char **argv)
{
  QStringList arguments;
  for (int i = 0; i < argc; ++i) arguments.append(QString::fromLocal8Bit(argv[i]));
  const auto options = CommandLine::parse(arguments);
  if (options.help || options.version) {
    LogFile::prepareConsole();
    const QByteArray text = (options.help ? CommandLine::helpText() :
        ProjectMetadata::name() + " " + ProjectMetadata::version() + "\n").toUtf8();
    std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
    return 0;
  }
  if (options.debug) LogFile::enableConsole();
  qInstallMessageHandler(LogFile::msgHandler);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

  MainApplication app(argc, argv);

  if (app.isClosing())
    return app.startupExitCode();

#ifdef Q_OS_UNIX
  int signalPipe[2] = {-1, -1};
  if (installSigtermHandler(signalPipe)) {
    QSocketNotifier notifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&app, &notifier] {
      // Disable before requesting exit; repeated signals cannot dispatch it again.
      notifier.setEnabled(false);
      // Enter normal shutdown, including modal unwinding and memory-DB saving.
      app.mainWindow()->quitApp();
    });
    const int result = app.exec();
    notifier.setEnabled(false);
    // Keep the handler and both pipe ends alive through application destruction.
    // A handler may still be running on another thread after exec() returns;
    // closing the descriptors here could race with its write. The OS closes
    // them at process exit, and FD_CLOEXEC prevents inheritance across exec.
    return result;
  }
  qWarning() << "Could not install graceful SIGTERM handling";
#endif
  return app.exec();
}

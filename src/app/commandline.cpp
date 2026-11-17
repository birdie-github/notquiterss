#include "commandline.h"
#include "projectmetadata.h"

CommandLine::Options CommandLine::parse(const QStringList &arguments)
{
  Options result;
  bool positional = false;
  for (int i = 1; i < arguments.size(); ++i) {
    const QString &arg = arguments.at(i);
    if (!positional && arg == "--") { positional = true; continue; }
    if (!positional && (arg == "--help" || arg == "-h")) result.help = true;
    else if (!positional && (arg == "--version" || arg == "-v")) result.version = true;
    else if (!positional && arg == "--debug") result.debug = true;
    else if (!positional && (arg == "--show" || arg == "--exit")) result.messages.append(arg);
    else if (arg.startsWith("feed:", Qt::CaseInsensitive) && !arg.contains('\n') && !arg.contains('\r'))
      result.messages.append(arg);
    else if (result.error.isEmpty()) result.error = QStringLiteral("Unrecognized argument: %1").arg(arg);
  }
  return result;
}

QString CommandLine::helpText()
{
  return QStringLiteral(
      "Usage: %1 [options] [feed:URL ...]\n\n"
      "Options:\n"
      "  -h, --help     Show this help and exit.\n"
      "  -v, --version  Show the application version and exit.\n"
      "  --debug        Print all application log levels to the console.\n"
      "                 File logging is controlled separately in Settings.\n"
      "  --show         Show the main window, including an existing instance.\n"
      "  --exit         Exit an existing instance; do not start a new one.\n"
      "  --             Treat subsequent arguments as feed URLs.\n\n"
      "feed:URL opens the Add Feed dialog (for example feed:https://example.org/rss).\n"
      "With an instance running, feed URLs and --show/--exit are forwarded to it.\n"
      "--debug is local to the newly launched process: restart the application\n"
      "with --debug to enable console logging for the running UI.\n\n"
      "Qt's own platform options (such as -platform, -style and -stylesheet)\n"
      "are also accepted and processed by Qt.\n").arg(ProjectMetadata::executable());
}

![Build Status](https://github.com/birdie-github/notquiterss/actions/workflows/ci.yml/badge.svg?branch=master)

© 2011-2020 QuiteRSS Project  
© 2026 Artem S. Tashkinov and ChatGPT

NotQuiteRSS is a fork of QuiteRSS, an open-source, cross-platform RSS/Atom news feed reader written in C++/Qt.

The application renders articles with QTextBrowser, without QtWebKit or
QtWebEngine. See [INSTALL](INSTALL) for dependencies and [ARTICLE_VIEW.md](ARTICLE_VIEW.md)
for rendering behavior, image-format plugins and validation notes.

Notification sounds use system audio players (paplay on Linux, afplay on macOS, aplay as fallback, PowerShell on Windows).

Links:
* Git repository: https://github.com/birdie-github/notquiterss
* Issue tracker: https://github.com/birdie-github/notquiterss/issues

Original project, QuiteRSS:
* Website: https://quiterss.org
* GitHub: https://github.com/QuiteRSS/quiterss
* Translations: https://explore.transifex.com/quiterss_team/quiterss/

See [PROJECT_METADATA.md](PROJECT_METADATA.md) for the central project definition,
new paths, packaging generation and update-check behavior.

See [LOGGING.md](LOGGING.md) for file logging, `--debug`, command-line help,
and the log-location control in General settings.

Resource organization and platform deployment are documented in [resources/README.md](resources/README.md).

For feeds requiring browser verification, see [Website cookies](COOKIES.md).

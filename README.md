![Build Status](https://github.com/birdie-github/notquiterss/actions/workflows/ci.yml/badge.svg?branch=master)

© 2011-2020 QuiteRSS Project  
© 2026 Artem S. Tashkinov and ChatGPT

NotQuiteRSS is a fork of QuiteRSS, an open-source, cross-platform RSS/Atom news feed reader written in C++/Qt.

The application renders articles with QTextBrowser, without QtWebKit or
QtWebEngine. See [INSTALL](INSTALL) for dependencies and [ARTICLE_VIEW.md](ARTICLE_VIEW.md)
for rendering behavior, image-format plugins and validation notes.

Notification sounds use miniaudio with built-in decoding, without Qt Multimedia
or GStreamer codec plugins. See INSTALL for the pinned header preparation step.

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

### Dependencies and design choices explained

NotQuiteRSS keeps QtSingleApplication because reliably enforcing a single running instance requires more than checking whether a local socket responds. Simultaneous launches, unresponsive processes, and crash recovery need careful handling—especially when two instances could overwrite the same user data. Removing this small dependency would make the application responsible for maintaining that machinery, without a clear benefit to users.

Notification sounds use miniaudio, compiled into the application, with support for WAV, MP3, and FLAC. Users do not need to install or configure an external player. Launching system commands would introduce platform-dependent format support and require managing hung processes, overlapping playback, and shutdown cleanup. For a simple RSS reader, keeping a consistent built-in audio backend is the more practical choice.

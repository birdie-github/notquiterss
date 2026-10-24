![Build Status](https://github.com/birdie-github/notquiterss/actions/workflows/ci.yml/badge.svg?branch=master)

© 2011-2020 QuiteRSS Project  
© 2026 Artem S. Tashkinov and ChatGPT

NotQuiteRSS is a fork of QuiteRSS, an open-source, cross-platform RSS/Atom news feed reader written in C++/Qt.

The application renders articles with QTextBrowser, without QtWebKit or
QtWebEngine. See [INSTALL](INSTALL) for dependencies and [ARTICLE_VIEW.md](docs/ARTICLE_VIEW.md)
for rendering behavior, image-format plugins and validation notes.

Notification sounds use miniaudio with built-in decoding, without Qt Multimedia
or GStreamer codec plugins. See INSTALL for the pinned header preparation step.

Links:
* Git repository: you are here 😉️
* Issue tracker: https://github.com/birdie-github/notquiterss/issues
* Translations: https://hosted.weblate.org/projects/notquiterss

Original project, QuiteRSS (dormant since early 2021):
* Website: https://web.archive.org/web/20220201032745/https://quiterss.org/
* GitHub: https://github.com/QuiteRSS/quiterss

See [PROJECT_METADATA.md](docs/PROJECT_METADATA.md) for the central project definition,
new paths, packaging generation and update-check behavior.

See [LOGGING.md](docs/LOGGING.md) for file logging, `--debug`, command-line help,
and the log-location control in General settings.

Resource organization and platform deployment are documented in [resources/README.md](resources/README.md).

For feeds requiring browser verification, see [Website cookies](docs/COOKIES.md).

## Dependencies and design choices explained

NotQuiteRSS keeps QtSingleApplication because reliably enforcing a single running
instance requires more than checking whether a local socket responds. Simultaneous
launches, unresponsive processes, and crash recovery need careful handling—especially
when two instances could overwrite the same user data. Removing this small dependency
would make the application responsible for maintaining that machinery, without a clear
benefit to users.

Notification sounds use miniaudio, compiled into the application, with support for
WAV, MP3, and FLAC. Users do not need to install or configure an external player.
Launching system commands would introduce platform-dependent format support and require
managing hung processes, overlapping playback, and shutdown cleanup. For a simple RSS
reader, keeping a consistent built-in audio backend is the more practical choice.

## Migrating your data from QuiteRSS

The new application is fully compatible with QuiteRSS data, but you will need to copy
the files manually. To achieve the best results, it is recommended that you export your
feeds and then import them into the new application.

On Linux:
* The new INI file is `~/.config/NotQuiteRSS/NotQuiteRSS.ini` vs. `~/.config/QuiteRss/QuiteRss.ini`
  before.
* The new data path is `~/.local/share/NotQuiteRSS` vs `~/.local/share/QuiteRss/QuiteRss`
  before.
* Cookies are saved in an entirely new format. They cannot be migrated.

On Windows:
 * INI, before: `AppData\Roaming\QuiteRss\QuiteRSS.ini`,
   now `AppData\Roaming\NotQuiteRss\NotQuiteRSS.ini`.
 * Data, before: `AppData\Local\QuiteRss\QuiteRss`,
   now `AppData\Local\NotQuiteRSS`.
   

## CPU Requirements 

### Windows x86-64

The Windows 64-bit version of NotQuiteRSS targets the **x86-64-v2** architecture,
requiring a processor with SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, and
LAHF/SAHF support. This requirement reflects the CPU optimizations used not just by
NotQuiteRSS but by its underlying Qt 6 framework and bundled libraries. Most Intel
CPUs manufactured since 2009 and AMD CPUs since 2011 meet these requirements, but
older models, including AMD Phenom and Phenom II, are **unsupported**. Attempting
to run NotQuiteRSS on an incompatible CPU may result in an immediate crash with an
*Illegal Instruction* exception (`0xC000001D`).

### Windows 32bit

Minimally tested Windows 32-bit builds targeting SSE2 CPUs and supporting Windows 7
and later are available [here](https://github.com/birdie-github/notquiterss/releases/tag/0.95.5-alpha-win32).
Bug reports specific to these builds, unless you can reproduce them on a standard
Qt6-based Windows 64 build, will **not be accepted**.

[![Linux Automated](https://github.com/birdie-github/notquiterss/actions/workflows/linux-automated.yml/badge.svg?branch=master)](https://github.com/birdie-github/notquiterss/actions/workflows/linux-automated.yml)
[![Linux AppImage](https://github.com/birdie-github/notquiterss/actions/workflows/linux-appimage.yml/badge.svg?branch=master)](https://github.com/birdie-github/notquiterss/actions/workflows/linux-appimage.yml)
[![Windows](https://github.com/birdie-github/notquiterss/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/birdie-github/notquiterss/actions/workflows/windows.yml)
[![MacOS](https://github.com/birdie-github/notquiterss/actions/workflows/macos.yml/badge.svg?branch=master)](https://github.com/birdie-github/notquiterss/actions/workflows/macos.yml)

© 2011-2020 QuiteRSS Project  
© 2026 Artem S. Tashkinov and LLMs

NotQuiteRSS is a fork of QuiteRSS, an open-source, cross-platform RSS/Atom news feed reader written in C++/Qt.

The application renders articles with QTextBrowser, without QtWebKit or
QtWebEngine. See [INSTALL](INSTALL) for dependencies and [ARTICLE_VIEW.md](docs/ARTICLE_VIEW.md)
for rendering behavior, image-format plugins and validation notes.

Notification sounds use miniaudio with built-in decoding, without Qt Multimedia
or GStreamer codec plugins. See INSTALL for the pinned header preparation step.

See [Windows and CPU requirements](#windows-and-cpu-requirements) before choosing a Windows build.

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
   

## Windows and CPU requirements

| Build | Windows | CPU |
| --- | --- | --- |
| Standard 64-bit (Qt 6) | Windows 10 version 1809 or later, or Windows 11 | x86-64-v2 |
| Experimental 32-bit (Qt 5) | Targets Windows 7 and later. Compatibility on older Windows is unverified. | x86 with SSE2 |

**Windows 8 and 8.1 are unsupported by the standard Qt 6 build.**
The [experimental 32-bit Qt 5 build](https://github.com/birdie-github/notquiterss/releases/tag/0.95.5-alpha-win32)
is available as-is for users who want to try it on an older Windows version or
an SSE2-capable CPU that falls short of x86-64-v2. Please reproduce any issue
on the standard build before filing a bug report.

The standard build requires all **x86-64-v2** CPU features, including SSE3,
SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, and LAHF/SAHF. Check the CPU model
even if it runs 64-bit Windows. For example, AMD Phenom and Phenom II CPUs lack
SSE4.1 and SSE4.2. Attempting to run the standard build on an incompatible CPU
may cause an *Illegal Instruction* exception (`0xC000001D`).

## HTTPS and certificate trust on older Windows
--------------------------------------------
The legacy package uses Qt 5 with bundled OpenSSL for TLS 1.2 and later.
NotQuiteRSS gets its default CA certificates from the Windows certificate
store through Qt. Windows 7 SP1 and Vista SP2 compatibility with the complete
package needs testing on those systems.

If HTTPS feeds fail certificate validation, check the system clock and make
sure Windows automatic root updates are enabled, subject to your administrator's
policy. Restart NotQuiteRSS after updating the store.

For offline systems, Microsoft KB2813430 documents how administrators can
refresh both the trusted and disallowed certificate trust lists (CTLs) on
Windows 7 SP1 and Vista SP2. Use an up-to-date Windows computer to download
both lists with certutil -syncWithWU, then follow Microsoft's CTL deployment
instructions for the legacy computer. Importing a trusted-root bundle alone
does not refresh the disallowed list. Keep certificate verification enabled.
https://support.microsoft.com/help/2813430

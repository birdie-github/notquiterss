# Translations

Translations are external files, loaded at runtime by both Qt5 and Qt6 builds.
English is always available without a translation file.

## Add a translation

Place `NotQuiteRSS_<locale>.qm` in either:

* The installed resource directory's `translations/` subdirectory: normally
  `/usr/share/NotQuiteRSS/translations` (or `/usr/local/share/NotQuiteRSS/translations`) on Linux,
  `resources/translations` beside notquiterss.exe on Windows, or `Contents/Resources/translations` in the
  macOS application bundle.
* The application's writable data directory's `translations/` subdirectory. This is
  `QStandardPaths::GenericDataLocation` plus the application name
  (`NotQuiteRSS`): normally
  `~/.local/share/NotQuiteRSS/translations` on Linux,
  `%LOCALAPPDATA%/NotQuiteRSS/translations` on Windows, and
  `~/Library/Application Support/NotQuiteRSS/translations` on macOS.
  Portable builds use `lang` beside the executable instead.

The writable directory wins when both contain the same locale. Invalid QM
files are skipped with a log warning; an invalid override does not hide a
working installed translation. Only `NotQuiteRSS_*.qm` files enter the language
list, so Qt's `qtbase_*.qm` files are not advertised as application languages.

Reopen Settings to discover newly added files, select the language, and apply
it. Restart after applying if an existing window still contains old text;
this change does not add live retranslation of existing widgets.
No application rebuild is needed. If the selected file disappears, the next
load falls back to English. Startup automatically selects an available language
from the system's preferred UI languages when no language has been saved.

Locale identifiers use a language code with optional script and region, for
example `ru`, `pt_BR`, or `sr_Latn_RS`. Hyphenated filenames are also accepted.
The native language name and locale code are displayed when metadata is absent.
Unknown language codes display their identifier instead of an unrelated name.

## Optional metadata

`languages.ini` is UTF-8 and uses one group per locale:

```ini
[ru]
name=Русский
author=Translator name
contact=translator@example.org
emoji=🇷🇺
```

The installed INI holds the existing translator credits. A user INI in the
writable `lang` directory overrides individual keys; omitted keys inherit,
and empty values clear them. A metadata entry without a QM file does not
register a language (except built-in English).

All fields are optional. There is no translation-version field. `emoji` is
ordinary Unicode text, not an image path. Existing country associations are
retained where possible, but languages need not have flags: omit the key or
set `emoji=`. Rendering depends on Qt, the OS and installed fonts; unsupported
flags may appear as letters or missing glyphs. Language names remain visible.

## Build and install

Restore `.ts` files into this directory, then rerun qmake. `lang.pri` discovers
`NotQuiteRSS_*.ts`, uses the selected Qt installation's `lrelease`, and installs
its generated QM files with `languages.ini`. No language list needs editing
in C++ or qmake. Precompiled QM files without corresponding TS files can also
be shipped. Nothing is embedded in the application resources.

For a user translation, Qt Linguist's Release action or the matching Qt
`lrelease` tool converts the editable TS source into a runtime QM file:

```
lrelease NotQuiteRSS_ru.ts -qm NotQuiteRSS_ru.qm
```

Linux/Windows installation uses `make install` (or `mingw32-make install`);
macOS copies these files into the application bundle during the build.
On Windows, CI also deploys Qt's own translations using windeployqt.
Optional `qtbase_<locale>.qm` files are searched in the writable `lang`,
installed `lang`, deployed `translations`, then Qt's translation directory.
They must match the application's Qt major version.

## Targeted verification

* Build/install once with Qt5 and once with Qt6 after restoring a TS file.
  Check that `languages.ini` and its QM file reach the platform's resource
  directory, and that the language loads on startup.
* With no application QM files, Settings must offer English only.
* Add a user QM without metadata while the application runs. Reopen Settings;
  it must appear with its locale code and be selectable.
* Add a user `languages.ini` entry overriding only the name and emoji. Check
  that installed author/contact fields are retained and `emoji=` removes the flag.
* Override an installed QM with a valid user QM, then an invalid user QM.
  Confirm user precedence in the first case and installed fallback in the second.
* Switch back to English, then restart. Remove a selected QM and restart;
  English must be used when no installed copy remains.
* Check a regional locale, a right-to-left language, and native-script names.
  Emoji rendering may differ across platforms; names must remain readable.

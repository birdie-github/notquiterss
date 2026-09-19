# Project metadata and paths

Edit `project.json`, then rerun qmake using `app.pro`. Python 3 is required at
configure time (override its executable with `PYTHON=/path/to/python3`). No
Python or JSON file is needed to run the installed application. Keep generated
files out of source control; qmake writes them under its build directory's
`generated/` directory. Source archives do not require Git.

`identity.name` is the single application name. It supplies the display name,
Qt application/organization names, resource/configuration/data directory
component, desktop filename and icon name, macOS bundle name, and translation
filename prefix. Only the executable is lowercased, with the normal `.exe`
suffix on Windows. The bundle namespace is separately editable.

For the supplied definition this means:

| Item | Name/path |
| --- | --- |
| Executable | `notquiterss` / `notquiterss.exe` |
| macOS bundle | `NotQuiteRSS.app` (contains `Contents/MacOS/notquiterss`) |
| Linux installed resources | `<PREFIX>/share/NotQuiteRSS` |
| Linux settings | `~/.config/NotQuiteRSS/NotQuiteRSS.ini` |
| Linux data | `~/.local/share/NotQuiteRSS` |
| Linux cache | `~/.cache/NotQuiteRSS` |
| Desktop entry / icon | `NotQuiteRSS.desktop` / `NotQuiteRSS` |
| Translation catalogs | `NotQuiteRSS_<locale>.qm` |

Linux paths respect XDG environment overrides. Other platforms use Qt's
GenericConfigLocation, GenericDataLocation and GenericCacheLocation, appending
the same application name once. Windows portable mode continues to use the
executable directory and `portable.dat`, with `NotQuiteRSS.ini` beside it.
Installed Windows resources remain beside the executable; macOS resources
remain in the bundle's `Contents/Resources` directory.

There is no automatic migration or lookup in the old application's directories.
When copying settings manually, rename the INI to `NotQuiteRSS.ini`; saved
absolute resource paths may also need adjusting. Rename restored TS/QM files
to use the new prefix. Licensing/provenance, private article HTML/CSS protocols,
and original artwork source filenames are not application identity settings.

`release.version` is a three-component numeric version; `release.date` is an
ISO date. The generator supplies C++, qmake, Windows VERSIONINFO, macOS plist,
Linux desktop/AppStream, and Doxygen metadata. Windows limits each version
component to 65535. The version is unchanged by this rebranding patch.

`project.repository` is the actual GitHub repository, regardless of the app's
name. Homepage, issues, releases, translator and latest-release API URLs default
to this repository. Optional `homepage`, `issues`, `releases`, `translations`,
and `update_endpoint` fields override individual URLs. Overrides of the update
endpoint must serve GitHub-compatible release JSON. Attribution and contact
strings live here too; original source license notices remain intact.
`project.original_project` supplies the original project's HTTPS URL. About links
to this project and the configured releases page; legacy history files are no
longer embedded or packaged. `CHANGELOG` records NotQuiteRSS releases only.

The checker reads stable, published release JSON, compares numeric version
segments, and displays release notes as plain text. A leading `v` is accepted.
Equal/older versions, drafts, prereleases, malformed replies and network errors
do not trigger update notifications. HTTP 404 reports no published release.
The download link opens the configured releases page; the old upstream XML,
history requests and Windows updater-launch path have been removed.

`resources` contains shared directory/file names, not lists of resources.
Translations and sharing remain runtime-discoverable external resources;
existing styles and sounds retain their current installation/loading behavior.
Application Theme discovers QSS files at runtime; see `resources/external/themes/README.md`. Renaming a
resource directory in the definition requires renaming its source directory too.
User choices and preference keys remain in the settings system.

# Verification

Run generator tests without Qt:

```
python3 tests/projectmetadata/test_generator.py
```

The optional Qt Test project `tests/releaseinfo/releaseinfo.pro` exercises numeric
comparison and invalid/draft/prerelease replies with either Qt5 or Qt6. It is
separate from the application build.

After building, verify About/title/tray branding; Homepage/Issues/Translations
links; manual and background update checks, offline and no-release responses;
new settings/data/cache locations; Windows portable mode and startup entry;
Linux desktop launch/icon; and the macOS bundle's executable and version.
Check that translations, styles, sharing icons and notification sounds still
load from installed resources. No application build/runtime tests were performed
when preparing this patch.

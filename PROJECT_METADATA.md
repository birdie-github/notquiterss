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

## Initial subscriptions

`default_feeds` is a build-time array in `project.json`. Each entry requires
`enabled` (a JSON boolean), `title`, and `feed_url`. `website_url` is optional.
URLs must be absolute HTTP(S) URLs without credentials. For example:

```json
"default_feeds": [
  {
    "enabled": true,
    "title": "Hacker News: Best",
    "feed_url": "https://hnrss.org/best",
    "website_url": "https://news.ycombinator.com/best"
  }
]
```

The generator omits disabled entries from the compiled list. Enabled entries
are inserted in array order at the root only when the database file was absent
at startup. An empty array, or disabling every entry, creates no subscriptions.
Existing databases, including those with no subscriptions, are left alone.
Rebuild after changing the definitions; the application does not read this JSON
at runtime. Duplicate enabled feed URLs are rejected at generation time.

Icons are not configured here. After insertion, the existing favicon worker
asynchronously inspects `website_url` (or `feed_url` when omitted). Feeds use the
generic icon until discovery succeeds; failures do not block startup. No icon
retry mechanism or database-to-disk save trigger is added.

## Subscription URL policy

New scheme-less URLs and `feed://` links default to HTTPS. Explicit HTTP URLs
require confirmation in Add Feed or when changing a subscription URL in
Properties. Keeping an existing HTTP URL while editing other properties does
not prompt again. The saved URL records the choice; background updates never
prompt or fall back to HTTP after an HTTPS error.

OPML import previews the number of HTTP URLs, with upgrading enabled by default.
Disabling upgrading requires one confirmation for the whole import. Only feed
URLs change, not website metadata or article links. Port 80 is removed during
upgrading; other explicit ports are preserved. Malformed OPML is rejected before
writes, after the existing compatibility repairs. Duplicate detection compares
normalized HTTP/HTTPS identities, within the import and against subscriptions;
existing subscriptions are neither changed nor upgraded by import.

An HTTP feed discovered inside a webpage also requires confirmation before it
is fetched. There is no new persistent HTTP permission flag and no TLS bypass.

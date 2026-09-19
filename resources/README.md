# Application resources

All source assets live here. `project.json` defines runtime directory names;
`resources.pri` and `scripts/stage-resources.py` provide the common deployment.

- `embedded/html/`: embedded article templates.
- `embedded/icons/`: UI artwork, embedded through `app.qrc`.
- `embedded/styles/`: mandatory System QSS, indicator CSS and article CSS.
  These files are embedded only. Qt resource aliases remain unchanged.
- `external/icons/`: native `.ico`/`.icns` and desktop application icons.
  The same source PNGs are also referenced by QRC where needed. Linux desktop
  icons continue to install into the standard hicolor directories.
- `external/themes/`: editable application QSS, staged into the existing
  runtime `styles/` directory. No theme IDs, metadata or fallback behavior change.
- `external/sounds/`: notification sounds.
- `external/social-networks/`: `configuration.ini` and service icons together.
- `external/overrides.ini.sample`: annotated sample, never loaded automatically.
- `external/translations/`: language metadata and catalogs. `NotQuiteRSS_*.ts` is compiled
  with the selected Qt's lrelease; prebuilt `.qm` files are accepted too.
  Translation sources, build helpers and documentation are not deployed.

Installed external roots:

| Platform | Root |
| --- | --- |
| Linux | `<PREFIX>/share/NotQuiteRSS/` |
| Windows (including portable builds) | `resources/` beside `notquiterss.exe` |
| macOS | `NotQuiteRSS.app/Contents/Resources/` |

Each root contains `icons/`, `styles/`, `sounds/`, `social-networks/`,
`translations/`, and `overrides.ini.sample`. No `external/` or `embedded/`
intermediate directory is installed.
HTML and UI images need no external copies because they remain in the executable.
Windows deployment puts Qt's own catalogs in `resources/qt-translations/`; Qt's
normal system catalog directory remains supported on platforms with installed Qt.

On Linux the user sharing configuration is now
`~/.config/NotQuiteRSS/social-networks/configuration.ini`, with icons in that same
folder. Installed configuration still takes precedence, as before. User-added
application translations use `~/.local/share/NotQuiteRSS/translations/`.
No search or migration of the previous paths is performed. Existing user-selected
sound filenames and other explicit preference paths are not rewritten.

## Build and deployment

Run qmake again after adding/removing resource files or translation sources.
The selected debug/release build directory contains a `resources/` stage, with
translation compiler output separately under `qm/`. One build dependency updates
that stage after compiling catalogs. Removed stage files are pruned; source files
are never changed by staging. Use a clean install/package destination when
changing the layout: installation does not remove old files from your system.

All platform install/bundle rules consume the same stage. Windows CI runs the
normal install target and checks every external resource against the stage and
source. There is no Windows list of individual styles, sounds or sharing icons.
Adding files under external directories, including nested auxiliary images,
requires no CI changes. Only `external/` is staged; new directories under
`embedded/` can never become runtime dependencies by accident.
Translation compilation and the source `themes/` to runtime `styles/`
mapping are handled by the shared staging script.

Python-only checks (no Qt build or application launch):

```
python3 tests/projectmetadata/test_generator.py
python3 tests/resources/test_resources.py
```

After building with Qt5/Qt6, check a clean installation/artifact: application and
article styles, notification playback, sharing menus/icons, application and Qt
translations, About/UI images, newspaper/article templates, Linux desktop icons,
and macOS/Windows native icons. Verify Windows away from the source checkout.

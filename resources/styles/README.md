# Application styles

The Application Style menu enumerates `.qss` files in the installed resource
root's style directory. With the supplied project metadata:

- Linux: `<PREFIX>/share/NotQuiteRSS/styles` (normally `/usr/local/share/NotQuiteRSS/styles`).
- Windows, including portable mode: `resources/styles` beside `notquiterss.exe`.
- macOS: `NotQuiteRSS.app/Contents/Resources/styles`.

All three platforms deploy this directory through qmake's normal install/bundle
rules. Article `.css` files remain deployed, but do not appear in Application
Style.

The bundled choices deliberately have simple semantics:

- **System** follows the palette exposed by Qt's platform integration.
- **Light** and **Dark** are fixed application themes and ignore the OS color
  scheme.
- **OLED** and the accent themes are also fixed and self-contained.

The former System Default/System2 alternatives are consolidated into System.

## Metadata

An optional header at the beginning of a QSS file supplies metadata. A style
without a header is treated as a `Mode=system` overlay and therefore may depend
on the current platform palette.

```css
/* ApplicationStyle
Name=My theme
Id=my-theme
Mode=fixed
Palette.Window=#202020
Palette.WindowText=#eeeeee
Palette.Base=#181818
Palette.AlternateBase=#242424
Palette.Text=#eeeeee
Palette.Button=#303030
Palette.ButtonText=#eeeeee
Palette.Highlight=#365f91
Palette.HighlightedText=#ffffff
Palette.Link=#58a6ff
Palette.LinkVisited=#c58af9
Palette.ToolTipBase=#303030
Palette.ToolTipText=#ffffff
Default=false
*/
```

Keys and values are case-sensitive. Each key may appear once. Unknown keys,
empty values, invalid colors, and invalid `Mode`/`Default` values reject the
file with a warning.

`Name` and `Id` default to the filename without `.qss`. Keep an explicit `Id`
stable if a file is renamed. IDs must be unique and nonempty. `Mode` defaults
to `system`; `Default` defaults to `false`.

`Mode=system` must not define `Palette.*` values. It is intended for System
or for an overlay that deliberately follows the current OS/desktop palette.

`Mode=fixed` is independent of the OS theme. It must define these palette roles:

- `Window`, `WindowText`, `Base`, `AlternateBase`, `Text`
- `Button`, `ButtonText`
- `Highlight`, `HighlightedText`
- `Link`, `LinkVisited`
- `ToolTipBase`, `ToolTipText`

`Light`, `Midlight`, `Dark`, `Mid`, `Shadow`, and `BrightText` may also be
specified. When omitted they are derived from the fixed palette, never from the
OS palette. Qt 5.12+ placeholder text and Qt 6.6+ accent are likewise derived.
This gives application code one effective palette without hardcoding theme IDs.

`Default=true` selects a style only when no preference has been saved. The
bundled System style is the fresh-install default. The embedded System
QSS is also used as the always-available fallback when the external file or
style directory is missing.

## Color overrides

Application-specific article/list/notification colors are derived from the
effective theme palette. The C++ code no longer has separate `dark` and
`standard` color tables.

The `[Color]` settings group contains only values that differ from the active
theme defaults. Changing to a different application theme warns first and then
removes those overrides, so stale colors cannot leak from one theme into
another. Resetting an individual color in Settings resets it to the current
theme's derived default.

System is the bundled style that responds to platform palette changes at
runtime. Fixed themes remain unchanged when the OS switches between light and
dark appearance.

## Runtime discovery

Open View > Application Style to refresh the file list. Adding/removing files
requires no restart or rebuild. Selecting a style reloads its current contents
immediately. Use absolute or `:/` resource URLs for images; relative URLs keep
Qt's normal working-directory semantics.

Malformed metadata, duplicate IDs, missing files/directories, invalid UTF-8,
and empty/unbalanced QSS produce warnings. Qt reports its own stylesheet syntax
and unsupported-property warnings when the sheet is applied.

## Fixed-theme widget primitives

Fixed themes use Fusion's standard palette as their starting point. All
supported color roles are initialized for Active, Inactive and Disabled;
disabled foregrounds and selection colors are then muted against the theme's
own backgrounds. System keeps the platform style and palette.

`fixed-indicators.css` is embedded in `app.qrc` and prepended only for fixed
themes. It supplies shared checkbox, checkable group-box, radio and menu
indicators, including checked, mixed, disabled and keyboard-focus states.
Outlines derive from Text/Base; embedded neutral PNG marks are selected for
contrast with Base, without an SVG plugin. Menu item boxes are styled alongside
indicators to avoid Qt's legacy shaded check-column fallback. Theme QSS follows
the shared rules and can override them using normal QSS specificity.

This is needed because Fusion derives some outlines by darkening Window,
which cannot give a visible outline on OLED black. Recent Qt6 Fusion checkbox
borders also depend on the platform color scheme. System does not receive the
shared rules: a native menu may show only a checkmark, with no empty box.

Leave QSpinBox borders/padding to Fusion unless supplying a complete set of
subcontrols. A partial QSS border switches button painting to a legacy fallback
while retaining Fusion geometry, which can leave insufficient room for arrows.
The supplied Dark and OLED themes therefore use Fusion spinboxes directly.

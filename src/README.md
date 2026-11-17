# Source layout

Organize code by feature. Keep each class's header and implementation together;
all `MainWindow` implementation files belong in `mainwindow/`.

| Directory | Responsibility |
| --- | --- |
| `app/` | Entry point, application lifecycle, command line, splash, About and release UI |
| `mainwindow/` | Main-window orchestration, toolbar customization, tray and status controllers |
| `feeds/` | Subscriptions, discovery, properties, feed tree, bulk settings and deletion |
| `feeds/update/` | Feed requests, parsing, update scheduling, favicons, XML and date helpers |
| `feeds/opml/` | Subscription import/export formats |
| `articles/` | Article tabs and lists, navigation, labels, search and read state |
| `articles/rendering/` | Article sanitization, text document rendering, images and scaling |
| `articles/filters/` | Article filter editors |
| `articles/cleanup/` | Cleanup UI and retention policy |
| `preferences/` | Options, settings, styles, language catalogs and shortcuts |
| `database/` | Database connection, schema and pending-database handling |
| `backup/` | Database backups and their settings page |
| `network/` | Shared network access, proxy, cookies, credentials and URL policy |
| `widgets/` | Reusable widgets and delegates |
| `platform/` | File-manager integration and sound playback |
| `downloads/` | Download management and UI |
| `notifications/` | Notification UI |
| `sharing/` | Article sharing |
| `common/` | Logging and shared utilities |

Keep feature-specific dialogs with their feature rather than collecting all
dialogs in a generic directory. These directories describe responsibilities;
they are not separate libraries or enforced dependency layers.

Use local quoted includes for headers in the same directory. For other project
headers, use paths relative to `src/`, such as `"preferences/settings.h"`.
The application and test projects add `src/` to the include path; generated and
external dependency headers retain their own include paths.

List source files explicitly in `app.pro`. When moving a file, also update test
projects, source-reading scripts, documentation and translation source locations.

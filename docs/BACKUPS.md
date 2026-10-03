# Database backups

Application Settings → Database Backup configures backups. File → Create Backup
and Back Up Now always create a new backup, even when automatic backups are off
or the database has not changed. Successful manual backups show a short confirmation
with OK and Show; Show opens the newly created backup set in the file manager.
Show Backups opens the backup directory in the system file manager.

Backups live beneath the application's data directory, in `backup/`. On Linux
with the default XDG paths, a set looks like:

```
~/.local/share/NotQuiteRSS/backup/2026-09-12_23-10-20_v0.90.2/
    feeds.db.backup
    NotQuiteRSS.ini.backup
```

Windows and macOS use their existing application data locations; portable builds
use their existing portable data location. Names use local time and the running
application's version. Same-second collisions get `_2`, `_3`, etc.
The old manual destination preference is no longer used. Existing backups are
left in place; only complete sets following the new convention are retained or
removed by the new subsystem.

## Automatic backups

Automatic backups default to disabled. Exit and subscription changes default to
selected; scheduling defaults to unselected, with daily frequency selected.
Retention defaults to 10 complete sets, shared by manual, automatic and upgrade
backups. Old sets are removed only after both new files have been written and
the temporary directory has been renamed successfully.

Subscription triggers run after adding, removing, editing, moving or sorting
subscriptions/folders, and after OPML import completes. A one-second delay groups
nearby operations. Article downloads, read status, labels and stars do not fire
this trigger. A pending subscription request is completed on exit even if the
separate exit trigger is off.

Schedules use elapsed days (1, 2 or 7) or a calendar month from the last successful
ordinary backup, including manual backups. They run while the application runs,
with a catch-up check after startup. The timer checks once a minute; an unchanged
or failed due backup is retried at most hourly. No OS scheduled task is created.

Automatic backups compare the logical database snapshot and full/filtered mode
with the last successful ordinary backup. No-op SQL writes and SQLite page
counters do not create differences. Filtering happens before comparison so
excluded article rows do not, by themselves, make filtered backups different.
INI-only preference changes do not force an automatic backup. Manual backups
always override comparison. A missing last backup causes a replacement backup.

Automatic successes are silent. Failures are logged and shown in a warning;
repeated identical automatic errors do not stack dialogs. Settings changes take
effect on OK. Back Up Now uses the page's current full/filtered selection.

## Filtered and pre-upgrade backups

“Keep only starred or labelled articles in backups” defaults to off and requires
confirmation when enabled. It keeps articles that are starred OR labelled,
regardless of read status. Subscriptions, folders, filters and settings remain.
Only the snapshot is filtered. Counts and article references are repaired and
excluded article data is removed from the snapshot's free pages by vacuuming.
The live database and its persistence timer are not modified.

Pre-upgrade safety backups are always enabled and always complete. They occur
before schema changes when the stored application version differs from the
running version. A failed safety backup is reported; as before, initialization
continues. Safety backups do not reset the ordinary schedule/comparison state.

## Restoring

Exit the application completely first. Copy `feeds.db.backup` to the application's
normal database location as `feeds.db`. Optionally copy `NotQuiteRSS.ini.backup`
to the normal settings location as `NotQuiteRSS.ini`. Keep the originals until
restoration has been checked. A filtered backup cannot recover excluded articles.

## Verification

`python3 tests/backups/test_filter.py` executes the production filtering SQL on
an isolated SQLite snapshot. It checks starred/labelled/read combinations,
source preservation, orphan removal, counts, empty results, cyclic folders and
removal of excluded payloads from the backup file. This is not a Qt runtime test.

Targeted manual checks on Qt5 and Qt6 builds:

1. With automatic backups off, use both manual entry points twice. Check unique
   set directories, both `.backup` files, compact success dialogs above Settings,
   and their Show button opening the exact newly created set,
   and Show Backups. General → Debug → Show must still reveal the log.
2. Enable subscription-only backups. Add/edit/delete a feed and import several
   feeds: expect one set per operation/batch. Routine refresh/read/star/label
   activity alone must not create one. Exit immediately after a subscription edit.
3. Enable exit backups: closing to tray must not create one. Actual exit must
   snapshot after cleanup. With in-memory storage, manual backups must include
   unsaved articles without updating the normal database file.
4. Check the clean-mode warning's Cancel and Yes paths. Inspect a filtered backup
   with SQLite: only starred or labelled articles remain, with consistent counts.
   Confirm the running database still contains the excluded articles.
5. Set retention to 2 and create three manual sets; keep the newest two. Make the
   backup location unwritable and check useful errors without deleting old sets.
6. For a schedule test on a disposable profile, set `Backup/lastSuccess` to an old
   timestamp while the app is closed. Restart: one due changed backup, none for
   unchanged data. Check clean/full mode changes invalidate comparison.
7. On a disposable profile, change `info.appVersion` to an older version before
   startup. Confirm a complete pre-upgrade set even with clean mode enabled and
   ordinary automatic backups disabled.

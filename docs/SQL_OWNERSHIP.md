# SQL connection ownership

The GUI owns the default SQL connection. Each UpdateFeeds instance (the normal
updater and each Add Feed wizard) has a distinct named connection, created by
UpdateSqlContext on QThread::started, before the worker event loop handles jobs.
Its parser and updater share that connection only within that same thread.

On thread completion the context is deleted in its worker thread. It destroys
the parser/updater and their database references first, then closes and removes
the connection. Closing an Add Feed wizard cannot remove the regular updater's
connection. The GUI connection remains alive while its models are alive.

FeedReadState contains the existing read/count algorithms. The GUI has its own
instance for synchronous navigation/close-to-tray read marking; UpdateObject
inherits the same implementation for background operations. No worker-owned
connection is temporarily used on the GUI thread. Existing direct/queued count
notifications are retained, with one receiver for each action.

## In-memory mode

SQLite 3.36.0 or newer is required, with its memdb VFS available. A process-unique
file:/... URI with vfs=memdb identifies the live RAM database. Independent native
connections share that RAM store using private page caches; they do not share
a QSqlDriver, its result list, or a native SQLite handle. No shared-cache mode,
read_uncommitted setting, or disk-backed replacement is used. SQLite connection
cache flags are set per open instead of changing the process-wide default.

RAM connections now obey SQLite's normal inter-connection locking and transaction
isolation, like disk connections. RAM transactions use BEGIN IMMEDIATE to reserve
the writer before taking read locks, avoiding competing read-to-write upgrades.
NewsModel fully fetches its selected rows in
memory mode so an idle view does not retain a read lock that blocks worker
commits. This can increase initial selection time and memory consumption for
large feeds. The existing SQLite busy timeout remains five seconds; long-running
lock contention and general SQL error recovery are not redesigned here.

Startup loads the disk database into the RAM store once. Worker creation does
not reload it. The existing timer and actual-exit save paths still perform the
memory-to-file copy. Manual cleanup and closing to tray do not persist the live
database. Backup snapshots remain independent of live-database persistence.

This change does not claim to fix every historical Fedora crash. It removes
the demonstrated cross-thread connection/result-list sharing. Worker access to
GUI state outside SQL, legacy persistence error handling, and general shutdown
sequencing are separate work.

## Verification

The optional tests/sqlownership/sqlownership.pro target supports Qt 5.15 and
Qt 6.2+. It exercises independent worker/GUI native handles, shared data, concurrent
query lifetimes, rollback, a model larger than Qt's first fetch batch, explicit
RAM snapshots, connection removal and last-connection RAM-store lifetime.
It is not part of the application build or CI and has not been compiled or run
when preparing this patch. Build it with the matching Qt's qmake, then run the
resulting sqlownership-test executable when runtime testing is authorized.

Manual checks on both Qt versions, in each storage mode:

1. Refresh feeds while deleting/restoring articles and switching feeds/tabs.
   Confirm read, unread, new and category counts remain correct.
2. Add and cancel feeds while normal updates run, then add another feed. Check
   that closing the wizard does not disrupt subsequent updates.
3. Exercise label/category read marking and folder navigation. Check for SQL
   thread-affinity, connection-removal and database-locked diagnostics.
4. In RAM mode, use a feed with more than 256 articles. Leave it displayed while
   refreshing it; new articles and counter changes must commit successfully.
5. Set a long RAM-save interval, update/mark/delete and close to tray or run
   manual cleanup. Confirm feeds.db is unchanged until the timer or actual exit.
   Restart after actual exit and confirm the latest state was restored.
6. Exercise manual/scheduled backups and exit after updates in both modes.

The SQLite memdb sharing feature was introduced in:
https://sqlite.org/releaselog/3_36_0.html

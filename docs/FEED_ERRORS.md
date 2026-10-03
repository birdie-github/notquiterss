# Feed retrieval failures

Failed refreshes now retain their diagnostic in the feed tooltip. Network errors
include an HTTP status where available; XML errors include parser details and
line/column; unsupported document roots and empty HTTP bodies are failures.
Valid RSS/Atom documents with no articles remain successful. Content-Type alone
is not used to reject a feed.

The tooltip is available after the first failure and includes consecutive failed
cycles, the latest failure time and the last successful retrieval. Its text is
escaped before rich-text display. No error dialog is shown.

A separate amber warning triangle appears in the feed-name column after two
consecutive automatic refresh failures, or one failed manual refresh. Updating
selected feeds, folders or all feeds from the UI counts as manual. Startup and
timer refreshes count as automatic. Internal retries still constitute one cycle.
A manual request for an already queued/in-progress feed marks that cycle as
manual without queuing another request.

The scalable triangle simulates a vertical-axis rotation every four seconds using
QPainter horizontal scaling. There is no SVG animation dependency. A single
40 ms single-shot timer repaints only warning rows; when there are no visible
warnings, including when the window is hidden, repaint scheduling stops.
Disabled feeds hide both the error triangle and error tooltip, retaining their
history for re-enabling. Folder disabling already propagates the disabled flag
to all descendants; the same per-feed flag controls refreshes and error visibility.
The existing update overlay remains for healthy feeds. A retry does not erase an
existing error or hide its warning. Successful retrieval (including a successful
unchanged response/HTTP 304) clears the failure streak. Cancellation leaves the
previous outcome intact. Existing Stop semantics are preserved: it cancels queued
requests; requests already running can still complete.

FeedHealth owns outcome encoding, threshold rules and tooltip formatting.
Versioned JSON is stored inside the existing feeds.status text field, retaining
its numeric-prefix convention. No schema migration is needed. Old numeric and
textual error statuses remain visible until a successful refresh. Status SQL uses
bound parameters so apostrophes and other diagnostic text are safe. Normal
in-memory database persistence rules remain unchanged.

## Verification

The patch was statically checked, not compiled or run. The Qt test target in
`tests/feedhealth/feedhealth.pro` covers thresholds, recovery, cancellation,
manual failures, legacy states, tooltip escaping and SQLite text round-tripping.
It is provided for Qt5 and Qt6 builds and was not executed during patch creation.

Manual checks:

1. Refresh a feed returning HTTP 502 manually: an immediate warning and a tooltip
   containing the HTTP status. Repeat with malformed XML and valid HTML instead
   of a feed; parser/root details should be shown, including apostrophes safely.
2. With a new failing feed state, allow two scheduled cycles: first failure gives
   a tooltip, second adds the triangle. Increase internal retry count and confirm
   that retries within a single cycle do not prematurely cross the threshold.
3. Restore a valid response (including an empty valid feed) and confirm the error
   clears. Check HTTP 304/unchanged HEAD responses as well. A zero-byte HTTP 200
   body should fail.
4. While a failed feed is refreshing, check that its previous tooltip/warning
   stays visible. Cancel a queued update and confirm the failure count is unchanged.
5. Check the four-second rotation, clipping, selection and readability at high DPI,
   with long titles and RTL layout. Collapse the affected folder or hide the app:
   animation repainting should stop. Restore it: animation resumes.
6. Disable a failing feed, then a folder containing failing feeds in nested
   subfolders: their warnings and error tooltips should disappear immediately.
   Re-enable them and confirm retained errors are visible again.
7. Restart after saving the database: failure count/history should survive.
   Existing articles, selection and publication-age retention should be unchanged.

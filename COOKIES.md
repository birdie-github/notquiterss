# Website cookies

Cookies are disabled by default. For a feed that needs browser verification:

1. Open the website in your browser and complete verification.
2. Export the relevant cookies in Netscape `cookies.txt` format. Cookie exports
   can grant access to your accounts; do not share them or upload them to a bug report.
3. Open **Tools → Website cookies → Import cookies…**.
4. Select the websites needed by your feeds and confirm. A leading dot includes
   subdomains. Import merges cookies; it does not replace unrelated websites.
5. Update the feed. Some websites may also require the browser's User-Agent,
   configured for the domain in `overrides.ini`. Verification can expire or be
   rejected independently of the cookie expiry date; importing is not guaranteed
   to work.

Only enabled websites can send or receive cookies. Cookie domain, path, expiry,
HTTP-only and Secure flags are preserved. Article images do not use these cookies.
An enabled website may set additional cookies within the enabled scope; it cannot
use this to enable a parent domain or another website. The setting applies to all
feeds using the scope. Removing a website removes its cookies and disables it.
A parent-domain scope subsumes any separately enabled hosts beneath it.

Persistent cookies are stored in `cookies.txt` in the application's data directory.
Session cookies are kept only in memory, including imported session cookies.
The application saves changed persistent cookies every six hours and on orderly
exit, after network workers stop. Closing to the tray does not save cookies.
Imports and removals are saved immediately. Failed imports/removals leave the
previous in-memory state intact. Saves replace the file atomically; failures are
logged and pending automatic saves are retried at the next interval or exit.
A crash can lose server-issued changes since the last save.

The file uses Netscape cookie records and comment lines recording enabled sites.
These sites remain enabled when their cookies expire. A malformed or unreadable
store is left untouched and cookie changes are blocked until it is repaired or
moved aside and the application restarted. Import files are limited to 8 MiB and
10,000 records. Expired records are skipped and counted in the import dialog.
Do not edit the application's store while it is running; use the import dialog.

Netscape format does not preserve modern browser attributes such as SameSite or
partition keys. This is a limited feed-compatibility feature, not a browser-profile
import. The old `cookies.dat` file is ignored and left untouched; reimport the
original browser export. The old `Settings/saveCookies` setting and the external
conversion script are no longer used.

## Developer checks

The standalone Qt test target is `tests/cookies/cookies.pro` (Qt 5.15 or Qt6).
It covers domain/path/Secure matching, session lifetime, merging and removal,
malformed input, failed-write rollback, deferred writes, identical-cookie writes,
and concurrent network adapters. Build it outside the source directory with
qmake and make, then run `cookies-test`. No application profile or network access
is needed.

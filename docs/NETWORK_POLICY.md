# Network URLs and downloads

Network fetches support HTTP and HTTPS. Explicit local-file feeds remain usable;
network redirects to local files are rejected. FTP and its variants are unsupported:
there is no bundled FTP implementation, authentication cache or external FTP handoff.
Article links, feed homepage links and dialog links use the existing HTTP/HTTPS/mailto
allowlist. Fixed application-sharing actions keep their explicit application schemes.

Downloads accept HTTP(S) only. Redirects use QUrl::resolved against the current
response URL, follow only 301/302/303/307/308, and stop after ten hops. Unsupported
schemes, invalid targets and HTTPS-to-HTTP downgrades fail the download. A Location
header on a successful response is not treated as a redirect. Each new download
request starts with fresh headers; HTTP authentication remains managed by Qt.
Redirect response bodies are never written to the destination file. Cancellation,
network errors and file-write failures cannot be followed by a success notification
or automatic opening of the partial file.

Feed and favicon redirects also validate the resolved destination before requesting
it. Update checks use Qt's verified-redirect policy with the same scheme/downgrade
checks and a ten-hop limit. Article image fetching already applies an HTTP(S)
allowlist and its own redirect limit.

## Manual regression checks after building

- Download a normal HTTP and HTTPS file, an empty file, and a chunked response.
  Compare downloaded bytes with the source, including when the save dialog remains
  open until the reply finishes.
- Exercise relative, root-relative and scheme-relative redirects, multiple hops,
  a loop, a missing Location, and a 200 response carrying a Location header.
- Attempt direct ftp:, ftps:, sftp:, file: and data: downloads and redirects to those
  schemes; none may fetch data or launch an external handler. An existing output
  file must remain intact when the request is rejected before any final response.
- Reject an HTTPS-to-HTTP redirect. Check a 404, interrupted transfer, unwritable
  destination and full filesystem: each must show failure rather than completion.
- Cancel during transfer and during a redirect chain. Declining deletion keeps the
  partial file; accepting removes it. No delayed signal may change failure to success.
- Check HTTP authentication, feed refresh (including local-file feeds), favicon
  discovery, article images and update checks. Existing unsupported feed URLs must
  produce an error without attempting that protocol or repeatedly retrying it.
- Click unsupported links in articles, feed properties/homepage actions and dialogs;
  no external protocol handler should launch. Ordinary web and mail links still work.

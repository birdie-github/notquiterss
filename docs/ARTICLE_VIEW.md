# Feed-only article viewer

The article pane uses QTextBrowser and QTextDocument. NotQuiteRSS no longer needs
QtWebKit or QtWebEngine. The viewer has no JavaScript engine, website navigation,
plugins, browser storage or browser tabs. Feed/category tabs remain reader views.
Existing database content and legacy database columns are retained.

Build dependencies: Qt 5.15.x, QtSingleApplication for Qt 5, SQLite, and libxml2 development headers (discovered
through pkg-config). Rerun qmake after applying the renderer migration because
source paths and linked Qt modules have changed. See [INSTALL](../INSTALL) for platform details.

## Article content and presentation

Article HTML is parsed by libxml2 with network access disabled, then serialized
through an element/attribute allowlist. Scripts, stylesheets, frames, objects,
interactive form controls, SVG and MathML are excluded. Embedded media source
URLs become explicit external links. A small set of inline text styles remains.
Original stored HTML is never overwritten by normalization.

Qt rich text displays paragraphs, headings, emphasis, lists, tables, images and
links. It does not implement full browser HTML/CSS layout. The application's
article stylesheet is still configurable, within Qt's supported rich-text subset.
Existing article fonts, colors and zoom preferences remain; obsolete browser font
preferences have been removed. Known incompatible selectors in the old default
stylesheet are filtered when loading it, without rewriting the user's file.

Newspaper controls are application-generated links handled by the reader.
Updates rebuild the document from the current news model. Named article anchors
and character offsets preserve the viewport and text selection where possible;
positions inside a removed article necessarily fall back to a surviving position.
Formatting changes and late image arrivals also preserve the reading position.
Images scale to the available pane width; Ctrl+wheel and the zoom actions scale
text and images. Wide publisher tables may still require horizontal scrolling.

Article search highlights matches and Enter reveals the first match. Its mode is
selected using the small button at the left of the search box: Find in Article.
Selection/copy, image copying, printing, print preview and HTML/text export remain.
HTML export omits newspaper action links and resource-only application icons.

## Image loading

Global/per-feed image preferences retain their existing behavior. Publisher
images use a separate network manager: no feed credentials, cookies, Referer,
disk cache or SSL-error exceptions. HTTPS requests explicitly use the system CA
certificates and verify the peer. Redirects are limited to five, restricted to
HTTP(S), and may not downgrade HTTPS. Remote responses are buffered up to 16 MiB,
checked as raster images (PNG/JPEG/GIF/WebP/BMP, up to 64 megapixels), then decoded
by Qt and supplied as document resources. Requests time out after 30 seconds.
The document loader never falls back to arbitrary local files or website loads.

Full image support requires Qt image-format plugins, including WebP (Fedora:
qt5-qtimageformats), a runtime dependency. Requests advertise only permitted
formats with installed decoders. This allows servers to return JPEG instead of
WebP when WebP support is absent; WebP-only images still require the plugin.
QMovie displays animation when supported by the installed decoder. Images and
animation data are held in memory for the current document and released when
those images leave the document.

Inline raster data URLs are limited to 8 MiB. Article input is limited to 4 MiB;
HTML traversal is limited to 128 levels and 50,000 nodes. Very large or unusual
content should be opened externally. The resource policy is not an OS sandbox:
permitted image loading contacts publisher servers and uses native image decoders.
Feed fetching/authentication and explicit downloads retain their existing behavior.

## Links and diagnostics

Web links open through the OS default browser. Mail links use the default mail
handler. Feed-provided custom protocols and local-file links are not dispatched.
Internal article anchors scroll within the pane. Only application-generated
newspaper controls may use the app action scheme.

Image diagnostics remain opt-in and work independently of the renderer. Start a
fresh application process with --debug for console output, or set
IMAGE_DEBUG=1 for image diagnostics through the configured file logger.
With neither enabled, these diagnostics are silent. They
report TLS configuration, resource permission, HTTP results and decoding failures.

## Manual checks after building

No application build was run while preparing this patch. Source checks and an
isolated Qt rich-text fixture do not substitute for testing the actual application.

- Check single-article and newspaper layouts, including title/date/author colors,
  large images, relative links, local anchors, lists, tables and right-to-left text.
- Exercise read/star/label/delete, filtered categories, sorting and newly arrived
  articles while scrolled midway through a newspaper; check position and selection.
- Toggle images in both layouts, including inherited/per-feed overrides; change
  articles while downloads are pending and check that old results do not appear.
- Check JPEG, PNG, WebP and animated GIF with the installed Qt image handlers.
- Exercise article search, selection/copy, image copy, zoom, print/preview,
  HTML/text export and feed/category tabs.
- Confirm ordinary, middle-clicked and context-menu links open externally, while
  in-article anchors stay local. Test an enclosure's explicit Save Link action.
- Try HTML with script, iframe, meta refresh, object, file URLs, CSS URLs and forged
  app action links: it must neither navigate nor execute application actions.
- Confirm the database's original content remains intact after viewing articles.

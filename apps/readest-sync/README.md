# Readest Sync

Native PocketBook app for downloading DRM-free EPUBs from Readest Cloud and
synchronizing reading progress with Readest on iPhone, browser and KOReader.

**Status: Qt6 UI starts on InkPad; full acceptance of the latest build is pending.**
The previous InkView implementation's login, Wi-Fi recovery, downloads,
bidirectional resume and cover browsing were user-confirmed. Qt Quick now owns
screens, input, timers and cloud cover decoding, using firmware PocketBook
controls. InkView remains only in the device adapter for startup, Wi-Fi, native
reader handoff and local EPUB cover extraction. Existing data formats and native
progress safeguards are retained. The supported native handoff target remains
InkPad 4 (PB743G), firmware `U743g.6.11.1683`; Verse Pro is deferred.
Host tests cover authentication, HTTPS, integrity, download recovery, native
position safeguards, reconciliation, and Qt UI behavior. The device acceptance
checklist is at the end of this README. Diagnostic fixture generation and the
backup-and-verify installer are described below.

## Application structure

QML screens use named commands and explicit controller properties. Library tiles
come from `LibraryModel`, whose roles include account/hash identity, availability,
cover path and both reading percentages. Search, availability filtering and paging
operate on this in-memory model; the view supplies its page capacity.

`AppController` owns navigation, the selected book identity, conflict choices and
user-facing messages. It never reads SQLite, performs transfers or checks EPUBs.
`ApplicationService` owns the cloud client and app database and implements
initialize/sign-in, refresh, discovery, download-or-reuse, sync and open preparation.
It is part of the Qt-free `readest-core` library and returns structured outcomes
and library snapshots. A book request is validated against its signed-in account.

`OperationRunner` runs one operation at a time, joins it before publishing its
result, and owns cancellation, network timeout and keepalive timing. Device
connection callbacks retain their own lifetime token: a late callback cannot
access a destroyed runner, and a pending firmware connection blocks another
connection attempt. Cloud and app-state access stays inside service operations;
the UI only sees completed snapshots. Shutdown cancels and joins outstanding work.

The device adapter supplies paths, Wi-Fi, reader handoff and local cover access.
Native handoff runs on the UI thread after a successful operation. Local cover
extraction is deferred device work for visible books, separate from rendering;
model getters perform no filesystem or database I/O. Cloud cover decoding remains
in the bounded image provider. Screens and shared QML controls are separate files.

Tests link production sources normally. The integrity suite includes a headless
application test with no Qt or network dependency. Qt tests exercise public
commands, independent controller instances, stable book identities, filtering,
worker shutdown and late callbacks; simulator scenarios cover native sync and
reader handoff. This refactor does not change persisted formats or native-position
validation rules. Hardware acceptance of the refactored build is still required.

## Build and package

Run from the repository root:

```sh
docker compose run --rm qt6 make APP=readest-sync check
python3 apps/readest-sync/tools/package_app.py
```

The second command creates a new directory under `build/readest-sync` with:

- `applications/readest-sync.app`
- `system/readest-sync/ca-certificates.crt`
- `manifest.json` containing their SHA-256 checksums

Packaging does not install anything. The package contains no credentials,
library database or reader settings. Back up the existing app before replacing
it during device validation; never replace firmware databases with workspace
snapshots. Runtime dependency and UI checks on InkPad remain necessary.

## App flow

1. Open Readest Sync. Online actions request a Wi-Fi connection when needed. Select **Email**, enter it and confirm.
   Select **Password**, enter it and confirm, then select **Sign in**.
2. Select **Refresh library**, optionally search by title/author, and select a book.
3. Choose **Download EPUB**. The original EPUB must have been uploaded to Readest;
   metadata alone is insufficient. The app validates archive contents and book
   identity before installation under `Books/Readest`.
4. Choose **Open** to sync and continue reading, or **Read offline** to use the
   native reader's existing position without a cloud request.
5. Close the native reader with Back. Choose **Sync now** to upload your position.
   Incoming progress is applied only by **Open**. When a Readest position is
   waiting, this button is labelled **Open at Readest position**.

If a downloaded book has no native settings yet, open and close it once, then
sync. When positions conflict, choose **Use PocketBook position** or **Use
Readest position**. A stale choice is rejected if either position has changed.
When Open encounters conflicting positions or an earlier position, it offers
**Open at PocketBook position** and **Open at Readest position**. The PocketBook
option opens locally without updating the cloud. The Readest option rechecks the
saved choice and applies it immediately, including an earlier passage, using the
normal native backup and conditional update. Earlier PocketBook positions are
not automatically pushed to Readest, whose other clients prefer forward progress. KOReader element/text XPointers can be resolved against
the original XHTML EPUB spine, including collapsed whitespace and UTF-16 text
offsets. Unsupported paths and mixed-media spines are rejected; no page
percentage is used to guess a position.

Back exits from the library, returns from book details, or cancels a network
operation. Downloads run off the UI thread. Failed downloads do not replace
existing books. Cloud deletions and sign-out retain local EPUB files.

## Local data

`system/readest-sync` holds the app's SQLite state, session, certificate bundle
and native position-change backups. Passwords are not persisted. Refresh tokens
are saved for login continuity; PocketBook's USB-readable FAT storage cannot
make them confidential through Unix file permissions. Sign-out removes the
saved session without revoking sessions on the user's other devices.

New EPUB downloads use `Title - Author.epub` (or `Title.epub` when no author is
available), preserving accents and non-Latin text. Unsafe filename characters
are replaced with spaces and long names are shortened. Separate managed
directories prevent books with the same name from overwriting each other.
Existing downloads keep their filenames and reading positions.

Existing EPUBs on internal storage and SD card are matched to the Readest library
by content hash during refresh and before a download. Startup displays the cached
library without a device-wide scan. Matching books
are registered at their original paths; they are not copied, renamed, or modified.
Use **Scan device** to find newly copied books without Wi-Fi. Scans run in the
background and can be cancelled; large collections take longer to inspect.
System/hidden folders, symlinks, and the app's managed download directory are
excluded (managed downloads use their account-specific recovery metadata).
Only matching Readest library entries are linked; unrelated books are left alone.

Each managed download has a metadata file recording its account, Readest book
hash, original-byte SHA-256, size and filename. Recovery validates completed
unregistered downloads after restart. Unknown/partial files are left untouched;
invalid metadata or duplicate local copies are reported.

## Large libraries

Discovery persists candidate fingerprints keyed by path, size, device/inode and
modification/change timestamps. Unchanged candidates need no EPUB content reads.
New candidates use Readest's bounded partial fingerprint (at most twelve 1 KiB
samples). Only matching candidates undergo full SHA-256 and archive validation
before registration. Opening and syncing still verify the original EPUB bytes.
Matches and candidate-index updates are each committed as a batch.

The library snapshot loads saved sync metadata in one query and native percentages
in one grouped query. Cover checks read headers rather than whole image files.
The scale regression uses 500 valid EPUBs totaling roughly 500 MiB and checks
content-read/validation counts, cache persistence, path invalidation and new cloud
matches. It prints host timings as diagnostics; these are not device timings.

## Local checks

The [interactive PocketBook simulator](../../tools/pocketbook-simulator/README.md) runs this app in a
browser with local cloud fixtures, Wi-Fi faults, downloads and a simulated reader.
Start it from the repository root with `docker compose up -d --build simulator`.
Choose **Mock cloud** for local fixtures or **Real Readest** for your actual account.
No device firmware is required.

```sh
python3 apps/readest-sync/tests/test_probe.py
python3 apps/readest-sync/tests/test_cloud.py
python3 apps/readest-sync/tests/test_http.py
python3 apps/readest-sync/tests/test_integrity.py
python3 apps/readest-sync/tests/test_install.py
docker compose run --rm qt6 sh tests/monorepo.sh
docker compose run --rm qt6 sh apps/readest-sync/tests/test_qt.sh
```

Host checks need C/C++ compilers, SQLite, json-c, libcurl, libxml2 and OpenSSL 3
development libraries. On macOS they use Xcode and Homebrew. HTTPS tests bind
a temporary localhost port with a generated certificate and dummy credentials.
The integrity suite also checks download recovery and progress orchestration.
The separate Qt suite exercises the real controller/QML using a desktop-only
PocketBook controls shim, including all library pages and PNG/JPEG covers. These checks do not
prove real-device rendering, live API behavior or arbitrary-passage equivalence.

`tools/install_app.py` accepts a reviewed package, the PB743G mount and the
observed SHA-256 of the currently installed app. It verifies a two-file
allowlist, backs up existing targets, stages and checks both copies, then
replaces them. Its audit compares the native databases and probe EPUB before
and after. It refuses an unexpected installed app. Device writes require the
normal filesystem approval; the installer test uses disposable directories.

`python3 apps/readest-sync/tools/package_probe.py` creates only the reproducible
test EPUB fixture. It does not package the current cloud executable as a probe.

### Network connection handling

Login, library refresh, downloads, and progress sync first request a connection
through PocketBook's network manager. The app waits up to 60 seconds and refreshes
the Wi-Fi power-off timer every 30 seconds while an online action is active.
Completion and cancellation stop the keepalive; normal OS power saving remains
in effect. Startup, sign-out, and **Read offline** do not request Wi-Fi.

A failed connection does not start the operation. Choose **Read offline** to open
an existing download without syncing. Transfer errors include the curl error code
and resolver version for troubleshooting. Sync writes are not automatically
replayed after a network failure.

The SDK does not provide cancellation for an outstanding connection callback.
After a connection timeout, offline work remains available; another online attempt
waits for that callback to finish. If it never finishes, close and reopen the app.
On InkPad, test both a fresh connection and an online action after Wi-Fi has
been idle. A visible Wi-Fi icon alone does not establish that DNS is working.

### Recovery

If a managed EPUB was deleted or moved, its details offer **Download EPUB** again.
A replacement must match the original bytes; an existing copy is never silently
replaced. Registering a new path clears the old native sync baseline so the new
copy reconciles independently. Completed replacement downloads are also recovered
on the next startup. Files moved outside the managed directory remain untouched.

If saved sign-in data cannot be loaded, the app returns to the sign-in screen.
Successful sign-in atomically replaces the session file. Download records and
reading state remain intact. Explicit conflict choices can restore a missing
position from the valid selected side; selecting an empty position is rejected.

### Tiled library

Library refresh uses limited requests and retains the API’s trailing timestamp
ties. Repeated fractional-millisecond boundaries use bounded lookahead, up to a
requested limit of 1,000 rows. Responses remain capped at 4 MiB; an unresolved
or oversized boundary reports an error without advancing the cursor.

Each tile shows separate **PocketBook** and **Readest** reading percentages.
PocketBook uses saved native page counts, read from a database snapshot at
startup, after operations, and on return from the reader. Readest uses the
latest cached library/config page counts; **Refresh library** updates its
library values. `—` means no usable saved percentage is available. The readers
paginate differently, so their percentages can differ at the same location.
These labels are display-only and never determine a sync position.

The library shows six book tiles in portrait and four in landscape, with title,
author and file availability. Tap a tile for its actions. Use **Previous/Next**
or the hardware page buttons to change pages; search still filters title/author.
Choose **All books**, **Available to download**, **On device**, or **Progress only**
above the tiles. Availability filters combine with search and reset pagination.

- **On device**: a downloaded or matched existing EPUB is present locally.
- **Available to download**: no local copy is linked and Readest storage lists one supported EPUB.
- **Progress only**: no local copy is linked and the library entry has no uploaded EPUB; only library/sync
  data is available to this app. Upload the EPUB in Readest and check again.
- **Not checked**: refresh to check storage; a failed check never means “no EPUB”.
- **Multiple EPUBs**, **EPUB unavailable**, or **Removed from cloud**: the entry
  cannot currently be downloaded by this EPUB-only app.

Choose **Refresh library** once after installing this update. It checks the
account's paginated storage listing and saves availability for offline browsing.
Cloud covers are downloaded for visible tiles in the background after refresh,
not for the entire library before it becomes usable. Book actions take priority
over this background work. When the storage listing
has no cover entry, the app also asks Readest for its canonical cover key, as
Readest itself does. Confirmed missing covers (HTTP 404) are remembered for six hours, or until the
listed cover version changes, to avoid repeated requests. PNG and JPEG are
recognized from their contents, including JPEG images stored as `cover.png`. Local EPUBs can also
use PocketBook's native cover loader. Missing/unsupported covers show a title
placeholder; cover failures do not prevent library use or book downloads.
The first cover refresh can take longer; Back cancels it. Cover files are limited
to 2 MiB and four million pixels before decoding.

The storage contract follows Readest's
[storage listing endpoint](https://github.com/readest/readest/blob/main/apps/readest-app/src/pages/api/storage/list.ts).
Cloud covers use Qt's bounded `QImageReader` in an asynchronous image provider.
PNG/JPEG are detected from bytes; source dimensions, decoded allocation and
thumbnail sizes are capped. This removes `LoadPNGStretch` and the former static
PNG decoder. Local EPUB covers can still be extracted by the device adapter and
cached as images; no native cover drawing occurs. The temporary rendering
checkpoint is no longer written.

Readest hashes and integrity checks now use dynamically linked OpenSSL 3,
compiled with matching modern headers. The seven imported digest symbols were
verified against the inspected InkPad `libcrypto.so.3`. HTTPS still uses firmware
libcurl. Sampled MD5 and whole-file SHA-256 behavior are unchanged; verify an
EPUB download and HTTPS on the device when accepting a new build.

For Qt previews and UI tests:

```sh
docker compose run --rm qt6 sh apps/readest-sync/tests/test_qt.sh
```

With the previously extracted firmware libraries available, compare actual exports:

```sh
docker compose run --rm qt6 python3 tools/check_qt6_firmware.py \
  build/readest-sync/readest-sync.app build/readest-sync/firmware-6.11.1683
```

Before accepting the migration on InkPad, test launch, native keyboard sign-in,
refresh, all library pages (especially 8/9), Wi-Fi recovery, an EPUB download,
Read offline, native-reader return, and both progress conflict choices.

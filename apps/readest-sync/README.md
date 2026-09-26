# Readest Sync

Native PocketBook app for browsing local and Readest libraries, transferring
DRM-free EPUBs, and synchronizing reading progress, highlights and attached notes.

**Device-validated on InkPad 4 (PB743G), firmware `U743g.6.11.1683`.**
The Qt Quick interface uses PocketBook controls. Library browsing, downloads,
reader handoff, progress synchronization, bidirectional annotation changes, and
startup/Home behavior have been checked on this device. Other models and firmware,
including Verse Pro, require separate validation. Automatic orientation changes
remain an [open device limitation](https://github.com/42lizard/pocketbook/issues/25);
the simulator can exercise both layouts.

- [Illustrated user guide](docs/USER-GUIDE.md): library, menus, downloads, reading,
  position conflicts, highlights and notes.
- [Installation instructions](release/README.md).
- [Mock simulator scenarios](../../tools/pocketbook-simulator/apps/readest-sync/README.md).
- [Annotation protocol and native storage](docs/research/readest-annotation-protocol.md).

The user guide contains screenshots from the **Mock cloud** simulator, with
synthetic books only. Simulator rendering approximates the firmware controls;
e-ink refresh, the native keyboard and native reader remain device features.

## Application structure

QML screens use named commands and explicit controller properties. Library rows
come from `LibraryModel`, whose roles include account/hash identity, availability,
cover path and both reading percentages. Search, availability filtering and paging
operate on this in-memory model; the view supplies its page capacity.

`AppController` owns navigation, the selected book identity, conflict choices and
user-facing messages. It never reads SQLite, performs transfers or checks EPUBs.
`ApplicationService` owns the cloud client and app database and implements
initialize/sign-in, refresh, discovery, download-or-reuse, sync and open preparation.
It is part of the Qt-free `readest-core` library and returns structured outcomes
and library snapshots. A book request is validated against its signed-in account.

The progress module owns reconciliation and the staged native-resume transition.
Sync stages incoming progress; explicit Open reconciles fresh observations before
applying it. The PocketBook reading-position writer retains its guarded transaction
and small audit, while
the progress module finalizes the baseline and returns the committed app-state
revision. Cancellation after a native commit still allows that finalization.
A failed final audit marker is reported separately from a failed position write.
If native progress changed but app-state recording failed, the app reports that
partial outcome and does not open the reader; the next Sync reconciles fresh state.
An uncertain native commit is also reported explicitly, without an automatic retry.

`OperationRunner` runs one operation at a time, joins it before publishing its
result, and owns cancellation, network timeout and keepalive timing. Online and
offline operations prevent standby through result delivery. A one-second firmware
grace period allows queued UI updates before normal power saving resumes. Device
connection callbacks retain their own lifetime token: a late callback cannot
access a destroyed runner. A foreground operation can take over the pending
connection of a cancelled cover request, retaining its original timeout instead
of starting a competing connection. Cloud and app-state access stays inside service operations;
the UI only sees completed snapshots. Shutdown cancels and joins outstanding work.

Before requesting a firmware connection, the device adapter checks the connection
flag and an active IPv4 default route. Existing connections can serve consecutive
sync and cover requests without another handshake. The runner also checks for a
route while awaiting the firmware callback, so a missing callback does not block
a connection that became available. This is a link/routing check; DNS and HTTPS
failures still surface through the transfer's own timeouts and error handling.

`VisibleCoverLoader` owns visible-page cover scheduling, cancellation, retry
bookkeeping and stale-result rejection. The controller supplies page demand and
suspends it for foreground operations or book details. Local extraction remains
on the UI thread; remote batches of at most six books use the same sequential
runner. Navigation preserves failed-attempt bookkeeping, successful Refresh
allows retries, and session resets invalidate old results even for the same account.

The device adapter supplies paths, Wi-Fi, reader handoff and local cover access.
Native handoff runs on the UI thread after a successful operation. Local cover
extraction is deferred device work for visible books, separate from rendering;
model getters perform no filesystem or database I/O. Cloud cover decoding remains
in the bounded image provider. Screens and shared QML controls are separate files.

Tests link production sources normally. The headless application test needs no
Qt or network connection. The `resume` check uses real SQLite fixtures and
controlled database/filesystem faults to verify native resume and partial failures.
The `cover-loader` CTest check uses controlled deferred
work and completions to exercise cancellation, retries and account/page changes. Qt tests exercise public
commands, independent controller instances, stable book identities, filtering,
worker shutdown and late callbacks; simulator scenarios cover native sync and
reader handoff. This refactor does not change persisted formats or native-position
validation rules. Device acceptance complements these automated checks; repeat it for new firmware or behavior changes.

## Build and package

### GitHub Actions releases

The `Readest Sync` workflow builds and tests pull requests and `main` changes
affecting this app or its shared dependencies. Each successful run provides a
`readest-sync-pocketbook` artifact containing an installable ZIP and SHA-256
checksum. **Run workflow** also builds a development artifact without publishing.

To prepare an independent app release, tag the intended committed revision:

```sh
git tag -a readest-sync/v0.1.0 -m "Readest Sync v0.1.0"
git push origin readest-sync/v0.1.0
```

Tags must use `readest-sync/vMAJOR.MINOR.PATCH`, optionally followed by a
prerelease suffix such as `-rc.1`. Tag pushes run regardless of path filters.
After tests and the ARM build pass, the workflow creates a **draft** GitHub
Release. Download the ZIP from the draft and validate it on the device before
publishing it. Tags for other monorepo apps do not create Readest Sync releases.
An existing release is not overwritten on reruns; use a new version for changed
code. No account secrets or device access are required for CI; the draft job uses
GitHub's built-in token with `contents: write`.

The ZIP includes installation instructions, version/commit identity, licenses,
and only two files intended for the device. Follow the included instructions to
copy those files individually, preserving existing device directories and data.
Compatibility is currently validated on InkPad 4 (PB743G), firmware 6.11.1683;
other devices and firmware need separate testing. Automated checks alone do not
establish device compatibility.

### Local packaging

Run from the repository root:

```sh
docker compose run --rm qt6 make APP=readest-sync check
python3 apps/readest-sync/tools/package_app.py
```

To create the same ZIP used by CI after a successful build:

```sh
python3 apps/readest-sync/tools/package_release.py \
  --version v0.1.0 --commit "$(git rev-parse HEAD)"
```

The ZIP and its checksum are written under `build/readest-sync/release`.
Use a clean checkout when identifying a release with its commit SHA. Existing
ZIPs are not overwritten; choose a different output directory with `--output`
when repeating a local build of the same version.

`package_app.py` creates a new directory under `build/readest-sync` with:

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

Outgoing position updates also carry PocketBook's current/total page counts for
Readest's percentage display. Readest may recalculate that ratio when opening the
book with its own pagination. The CFI remains the precise resume location; page
counts never determine it. Missing native counts retain the remote display value.
Already synchronized positions do not trigger another upload just to change the
percentage; after upgrading, the next PocketBook page turn and sync updates both.

If a downloaded book has no native settings yet, open and close it once, then
sync. When positions conflict, choose **Use PocketBook position** or **Use
Readest position**. A stale choice is rejected if either position has changed.
When Open encounters conflicting positions or an earlier position, it offers
**Open at PocketBook position** and **Open at Readest position**. The PocketBook
option opens locally without updating the cloud. The Readest option rechecks the
saved choice and applies it immediately, including an earlier passage, using the
guarded PocketBook reading-position transaction and conditional update. Earlier
PocketBook positions are not automatically pushed to Readest, whose other clients
prefer forward progress. KOReader element/text XPointers can be resolved against
the original XHTML EPUB spine, including collapsed whitespace and UTF-16 text
offsets. Unsupported paths and mixed-media spines are rejected; no page
percentage is used to guess a position.

Back exits from the library, returns from book details, or cancels a network
operation. Downloads run off the UI thread. Failed downloads do not replace
existing books. Cloud deletions and sign-out retain local EPUB files.

## Local data

`system/readest-sync` holds the app's SQLite state, session, certificate bundle
and PocketBook reading-position audits. Passwords are not persisted. Refresh tokens
are saved for login continuity; PocketBook's USB-readable FAT storage cannot
make them confidential through Unix file permissions. Sign-out removes the
saved session without revoking sessions on the user's other devices.

`operations.log` records refresh phases, process IDs, timestamps and peak memory
in KiB to help diagnose device exits. It includes monotonic milliseconds for
elapsed-time comparisons; the wall clock alone can change or span suspend. It contains no credentials or book data and
is truncated at 64 KiB. It is a best-effort phase log, not a crash backtrace.

New EPUB downloads use `Title - Author.epub` (or `Title.epub` when no author is
available), preserving accents and non-Latin text. Unsafe filename characters
are replaced with spaces and long names are shortened. Separate managed
directories prevent books with the same name from overwriting each other.
Existing downloads keep their filenames and reading positions.

Startup loads cached cloud metadata and scans the **PocketBook library index**
without requesting Wi-Fi. It does not recursively walk every storage directory.
Indexed EPUBs appear even when signed out. **Menu → Scan device** updates this
inventory; **Refresh library** also refreshes cloud metadata and availability.
Unindexed files must first be discovered by the PocketBook library.

Matching uses EPUB content fingerprints rather than titles. Existing files stay
at their original paths; they are not copied or renamed. File stamps retain
fingerprints and metadata for unchanged EPUBs. The first inventory inserts its
rows; later scans insert new rows, update changed rows and remove missing rows.
Unchanged rows are not rewritten. Removing an inventory row does not delete an
EPUB or remove a book from Readest.

Each managed download has a metadata file recording its account, Readest book
hash, original-byte SHA-256, size and filename. Recovery validates completed
unregistered downloads after restart. Unknown/partial files are left untouched;
invalid metadata or duplicate local copies are reported.

## Large libraries

The indexed library scan caches fingerprints and metadata by path, size,
device/inode and modification/change timestamps. Unchanged files need no EPUB
content reads. Changed files use Readest's bounded partial fingerprint (at most
twelve 1 KiB samples) and reload their metadata. Opening, syncing and transferring
still validate the original EPUB bytes. A first launch after USB or a changed
file stamp can take longer than a subsequent launch.

The library snapshot loads saved sync metadata in one query and native percentages
in one grouped query. Cover checks read headers rather than whole image files.
Sync, Open and Upload read the PocketBook reading position directly in a short,
read-only transaction that keeps identity and settings consistent, including WAL
changes. They no longer copy the native database to a temporary capture file.
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
# Core-only host checks (no Qt or PocketBook SDK required):
cmake -S apps/readest-sync -B build/readest-sync/host -DREADEST_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/readest-sync/host -j2
ctest --test-dir build/readest-sync/host --output-on-failure

# A targeted build and test, reusing the same production objects:
cmake --build build/readest-sync/host --target application-test -j2
ctest --test-dir build/readest-sync/host -R '^application$' --output-on-failure

# Qt-only checks and monorepo isolation in the SDK container:
docker compose run --rm qt6 sh apps/readest-sync/tests/test_qt.sh
docker compose run --rm qt6 sh tests/monorepo.sh
```

Host checks need CMake 3.21+, a C++20 compiler, Python 3, pkg-config, SQLite,
json-c, libcurl, libxml2 and OpenSSL development libraries. On macOS CMake finds
Xcode libraries and Homebrew dependencies. The OpenSSL command-line tool is also
needed for temporary TLS certificates. HTTPS tests bind a temporary localhost
port and use dummy credentials.

CMake owns compilation and passes each native executable to its Python harness
through `READEST_TEST_BINARY`. Python only prepares fixtures and checks results.
Run the harnesses through CTest; for direct debugging, set that variable to the
absolute path of the already-built executable. Probe and HTTP harnesses also
accept the usual Python unittest selectors. `ctest -N` lists tests; `-L core` and
`-L packaging` select groups. Integrity, download, progress, application and scale
checks are independently selectable. Assertions stay enabled for host tests,
including Release builds. The application target uses the counted real validator.

Build directories are specific to both platform and profile: do not reuse a macOS
build directory inside Docker, or an ARM directory for host checks. In the SDK
container, unset `CMAKE_TOOLCHAIN_FILE` before configuring any host profile.
Use `CC`/`CXX` at the first CMake configuration to select native compilers.

The simulator configuration includes the complete native, packaging, Qt and mock/
real-transport test suite used by CI:

```sh
docker compose run --rm qt6 sh -ec '
  unset CMAKE_TOOLCHAIN_FILE
  cmake -S apps/readest-sync -B build/readest-sync/simulator -DPOCKETBOOK_SIMULATOR=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/readest-sync/simulator -j2
  ctest --test-dir build/readest-sync/simulator --output-on-failure
'
```

The default configuration remains the PocketBook ARM app; use
`docker compose run --rm qt6 make APP=readest-sync check` to build it and check its
ABI. Its objects remain in `build/readest-sync/qt6` and its installable binary is
`build/readest-sync/readest-sync.app`.

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
through PocketBook's network manager. When a connection is needed, the app first
powers on the Wi-Fi radio using the firmware API, off the UI thread. A successful
connection callback alone does not start the request: the network must also have
a default route. The app waits up to 60 seconds and refreshes
the Wi-Fi power-off timer every 30 seconds while an online action is active.
Keepalive calls run off the UI thread, with at most one in flight, so a stalled
firmware network manager cannot block Cancel or the connection timeout.
All service operations, including offline startup and scans, prevent CPU standby
through the UI completion callback. A one-second firmware grace period then
allows queued rendering to complete. Failure, cancellation and exit also release
the protection; the app does not permanently disable power saving. Network
keepalive remains limited to online operations. Startup, sign-out, and
**Read offline** do not request Wi-Fi.

Temporary diagnostics remain enabled in `system/readest-sync/network.log`.
The log is bounded to approximately 64 KiB and records connection stages, numeric
status, and timing; it does not record credentials, network names, or book data.

A failed connection does not start the operation. Choose **Read offline** to open
an existing download without syncing. Transfer errors include the curl error code
and resolver version for troubleshooting. Sync writes are not automatically
replayed after a network failure.

The SDK does not provide cancellation for an outstanding connection callback.
After a connection timeout, offline work remains available; another online attempt
waits for that callback to finish. If it never finishes, close and reopen the app.
On InkPad, test both a fresh connection and an online action after Wi-Fi has
been idle. A visible Wi-Fi icon alone does not establish that DNS is working.

### Cloud removal and missing EPUBs

Refresh reflects removals from Readest. A removed book with a local copy stays
visible as **Removed from Readest** and remains readable offline. **Re-upload to
Readest** explicitly restores it; refresh and normal sync never restore it
automatically or delete the PocketBook copy. Removed entries without local
files disappear from the library view.

If Readest has book/progress metadata but no EPUB, a matching local copy offers
**Upload EPUB to Readest**. Upload checks cloud files independently of metadata,
retains existing cloud metadata, and uses normal progress conflict handling.

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

### Library list and book details

Library refresh uses limited requests and retains the API’s trailing timestamp
ties. Repeated fractional-millisecond boundaries use bounded lookahead, up to a
requested limit of 1,000 rows. Responses remain capped at 4 MiB; an unresolved
or oversized boundary reports an error without advancing the cursor.

Book details show separate **PocketBook** and **Readest** reading percentages.
PocketBook uses saved native page counts, read directly from the firmware
database in a short read-only transaction at startup, after operations, and on
return from the reader. Readest uses the
latest cached library/config page counts; **Refresh library** updates its
library values. `—` means no usable saved percentage is available. The readers
paginate differently, so their percentages can differ at the same location.
These labels are display-only and never determine a sync position.

The library uses full-width rows with a small cover, title, author and status.
Tap a row for book details. The footer arrows and hardware page buttons change
pages. **Menu** opens search, filters, refresh, device scan and account actions.
The hardware menu key uses the same menu and is disabled during blocking actions.
Long-pressing a row offers its available contextual actions.

Search matches title or author. Filters include **All books**, **Available to
download**, **On device**, **Progress only**, and **PocketBook only**. Search and
filters combine and reset pagination. Page capacity depends on the layout and
screen size.

- **On device**: a downloaded or matched existing EPUB is present locally.
- **Available to download**: no local copy is linked and Readest storage lists one supported EPUB.
- **Progress only**: no local copy is linked and the library entry has no uploaded EPUB; only library/sync
  data is available to this app. Upload the EPUB in Readest and check again.
- **Not checked**: refresh to check storage; a failed check never means “no EPUB”.
- **Multiple EPUBs**, **EPUB unavailable**, or **Removed from cloud**: the entry
  cannot currently be downloaded by this EPUB-only app.

Choose **Refresh library** once after installing this update. It checks the
account's paginated storage listing and saves availability for offline browsing.
Cloud covers are downloaded for visible rows in the background after refresh,
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

List covers remain asynchronous. Their rendered PNG thumbnails are kept in the
separate `system/readest-sync/cover-cache.db`, bounded to 8 MiB of image data and
evicted by least-recent use. The key includes the account, book, source-cover
version, requested size and rendering format. This database contains no sync or
recovery state and can be deleted at any time; cache locks, corruption and write
failures fall back to the original cover without blocking the library. Book
details continue to render from the original cover.

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

For future device acceptance, test launch and Home exit, native keyboard sign-in,
refresh and paging, Wi-Fi recovery, an EPUB download, Read offline, native-reader
return, both progress conflict choices, and highlight/note creation, editing and
deletion in both directions. Repeat Sync to check that it creates no duplicates.

## PocketBook library and uploads

For a linked book with an EPUB on the device and in Readest, **Upload cover to
Readest** retries just the embedded cover. It does not resend the EPUB or change
reading progress. Upload failures include the reservation/transfer HTTP status
when available; missing or unsupported embedded covers are reported separately.

EPUBs indexed by PocketBook appear alongside your Readest books, including when
signed out. **On device** includes both linked and device-only books;
**PocketBook only** shows books absent from the connected Readest library.
**Scan device** updates this inventory without connecting to Wi-Fi. Unindexed
files are not included. Unchanged EPUB fingerprints and metadata are cached;
full archive validation happens when opening or transferring a book.

Select a device-only book and choose **Upload to Readest** to upload its EPUB
and current PocketBook reading position. Sign in first if necessary. Uploading
leaves the original file in place. Embedded title, author, and cover are used
when available; missing metadata falls back to the filename, and a missing cover
never blocks uploading. Unread books have no position to transfer; unsupported
saved positions produce a warning while allowing the book upload.

Matching uses Readest's EPUB fingerprint, not title or author. Different editions
remain separate. When local copies have differing positions, choose the copy by
path and percentage; the app remembers it for opening and synchronization.
If a matching Readest book already exists, its position goes through the normal
conflict resolution instead of being overwritten automatically.

Interrupted uploads retain their completed stages across restarts and require
**Retry upload**. If only progress failed, the EPUB is not sent again. Retries
check the remote state and use the latest local position. Pending work belongs
to the account that started it. Removing a local file changes its device
availability, but does not delete its Readest entry. There are no automatic or
bulk EPUB uploads and no synchronized deletion of EPUB files. Annotation
deletions are synchronized separately, as described below.

## Highlights and attached notes

For a linked, verified EPUB, **Sync now** and online **Open** reconcile highlights
and their attached text notes in both directions. Create, edit or delete them in
the native PocketBook reader or Readest, close the native reader, then sync the
book. **Refresh library** does not synchronize every book's annotations, and
**Read offline** makes no cloud request. The app has no annotation editor or list;
inspect the annotations in the readers.

Annotation writes are enabled only for the validated PB743G firmware above.
Passage ranges must resolve against the same EPUB text; unsupported ranges,
styles or colors are skipped with a warning. Conflicting edits are retained and
reported rather than silently choosing one side. A warning can leave annotation
sync incomplete even when opening the book succeeds. Do not interpret the
reading-position conflict buttons as annotation conflict resolution controls.

An app-owned journal records native/cloud identities, baselines and pending
uploads. Retries reuse their identities to avoid duplicates. Native reads use a
short read-only SQLite transaction; native writes use a guarded transaction with
an unchanged-state check. Neither operation copies the complete firmware DB.
Cloud acknowledgements and a fresh preflight read are checked, but Readest's API
has no conditional-write operation, so a concurrent cloud edit still has a race
window. Avoid editing the same note on two devices while a sync is in progress.

Device validation covered a Readest edit arriving on PocketBook, a PocketBook
replacement arriving in the sync journal with matching cloud state, deletions,
and repeated synchronization. Automated checks additionally cover interrupted
uploads, retries, stale responses, unsupported replacements and conflicts.

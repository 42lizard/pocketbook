# Readest Sync for PocketBook

This package targets the PocketBook InkPad 4 (PB743G) with Qt6 firmware.
Device testing has used firmware 6.11.1683. Other models and firmware versions
are not yet validated. Qt6 and its supporting libraries must already be present
on the device; this package does not install or update firmware.

## Install or update

1. Close Readest Sync and connect the PocketBook by USB in storage mode.
2. Extract the ZIP into a temporary folder on your computer.
3. Back up `applications/readest-sync.app` and
   `system/readest-sync/ca-certificates.crt` from the device if they exist.
4. Copy `applications/readest-sync.app` into the device's `applications` folder.
5. Create `system/readest-sync` if needed, then copy `ca-certificates.crt` from
   the package's matching folder into it.
6. Wait for copying to finish, safely eject the device, disconnect USB, and
   launch Readest Sync from Applications.

Copy the two files individually. Do not replace the device's entire
`applications` or `system` folders. Existing books, login, app state, and reader
databases should be left in place. Documentation and licenses can stay on your
computer. On Linux, if necessary, mark the copied `.app` executable with `chmod +x`.

To undo an update, close the app and restore the two backed-up files by USB.

## Use

Sign in, choose **Menu → Refresh library**, and select a book. Download its EPUB
if needed. **Open** synchronizes before opening the native reader; **Read offline**
uses the current PocketBook position without contacting Readest. **Sync now**
reconciles reading progress, highlights and attached notes for that book.

Highlights and attached notes are created/edited in PocketBook or Readest, not
inside this app. Their edits and deletions synchronize in both directions on the
validated firmware. Unsupported annotations or conflicting edits produce a
warning; reading-position choices do not resolve annotation conflicts.

Existing PocketBook books appear in the library without signing in. Use **Scan
device** to refresh the app's inventory from the native library index and **Upload to Readest** to transfer
a device-only EPUB. The original file stays in place.

The [online user guide](https://github.com/42lizard/pocketbook/blob/main/apps/readest-sync/docs/USER-GUIDE.md)
contains mock-simulator screenshots and explains the actions and limitations.

Restoring an older executable does not undo reading positions or annotations
already synchronized to PocketBook or Readest.

## Checksums and build identity

The accompanying `.zip.sha256` file verifies the ZIP download. For example,
run `sha256sum -c <downloaded-file>.zip.sha256` on Linux, or
`shasum -a 256 -c <downloaded-file>.zip.sha256` on macOS, in the download folder.
`manifest.json` contains SHA-256 hashes of the two device files. `release.json`
identifies the app version and exact source commit.

CI checks compilation, the ARM ABI, and automated tests. A draft release still
needs device validation before publication, including startup, login, refresh,
download, progress sync, and annotation edits/deletions in both directions. No live account credentials are used in CI.

## Licenses

Original project code is MIT-licensed; see `LICENSE`. Dependency notices are in
`licenses/README.md`, with miniz's original license under
`apps/readest-sync/src/vendor/miniz/LICENSE`.

# Bidirectional annotation sync: upstream protocol and native prerequisite

Research date: 2026-09-26. Readest source pinned to
[`8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2`](https://github.com/readest/readest/tree/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2).
This is source research, not a live cloud or device round-trip test.

## Findings

The KOReader plugin uses the normal Readest `/api/sync` endpoint. It pushes
`{books: [], notes: [...], configs: []}` and pulls with `type=notes`, `since`
(epoch milliseconds), `book` and `meta_hash`. There is no separate annotation
endpoint to reproduce. [Endpoint specification](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest-sync-api.json#L1-L19)

Push fields are camelCase: `bookHash`, `metaHash`, `id`, `type`, `xpointer0`,
optional `xpointer1`, selected `text`, optional user `note`, `style`, `color`,
`page`, `createdAt`, `updatedAt`, and optional `deletedAt`. Highlights and their
notes share one `annotation` record; bookmarks use `bookmark`. The plugin maps
underline and strikeout styles and several colors. PDF-style table positions
are not exported by this descriptor, and PDF pull is explicitly unsupported.
[Plugin implementation](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncannotations.lua)

Readest additionally accepts `cfi` and `global`; the server stores supplied
location fields, rather than converting them in the payload transformer. Pull
returns snake_case database fields and ISO timestamps. Readest's client-side
XCFI converter supports ranges and needs the EPUB document; the KOReader plugin
itself consumes XPointer endpoints. A native PocketBook implementation must
preserve both endpoints, not approximate a highlight with a page or reading
position. [Payload conversion](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest-app/src/utils/transform.ts),
[range conversion](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest-app/src/utils/xcfi.ts#L62-L118)

## Identity, edits and deletions

- The plugin preserves a pulled note's ID. For native KOReader items it derives
  seven hexadecimal characters from MD5 of `ko:book_hash:type:pos0:pos1`.
  Pull deduplicates by ID and then by range endpoints (bookmark: one endpoint).
  This is useful precedent for persistent identity, not a reason to copy its
  short hash or its KOReader-specific location encoding. [Identity and pull](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncannotations.lua)
- **Remote edits are a limitation of this plugin:** pull skips any live record
  whose ID or position already exists. It does not replace the existing note,
  selected text, color, style or range. Thus “both ways” in this implementation
  covers additions and deletions, but does not establish correct propagation of
  edits to existing records. PocketBook should not inherit this behavior.
  [Pull implementation](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncannotations.lua)
- Locally deleted records become persisted tombstones in the book sidecar.
  They are sent with a deletion timestamp and retained on failed push. Only
  the exact tombstones acknowledged by that request are cleared. Pull remembers
  pending deletions from both request start and completion, preventing a stale
  response from resurrecting a deleted highlight. Remote tombstones remove
  matching local records before additions are considered. [Implementation](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncannotations.lua),
  [regression specifications](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/spec/syncannotations_spec.lua)
- Server writes use `(user_id, book_hash, id)` identity. Existing note rows are
  replaced when the incoming update timestamp **or** deletion timestamp is
  newer; this is row replacement, not field-level conflict reconciliation.
  Inserts get a server update timestamp. POST returns authoritative records,
  including the server winner, but the plugin push callback ignores that body.
  [Server push](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest-app/src/pages/api/sync.ts#L659-L879)

## Cursors, pagination and retries

Notes pull filters `updated_at > since OR deleted_at > since`. When both book
and metadata hashes are present it matches either hash. The server internally
accumulates pages of 1,000 rows, then deduplicates by ID, choosing the largest
update/deletion timestamp and preferring a tombstone on ties. Notes do not use
the newer server-assigned `synced_at` cursor used for books, or the explicit
limited books pagination contract. [Server pull](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest-app/src/pages/api/sync.ts#L217-L440)

The plugin uses `settings.last_notes_sync_at` for push selection and pull.
Successful push advances it to request-start seconds minus one millisecond,
preserving edits in that second. Nonempty successful pull advances it to the
client's current wall clock. Full sync uses zero. **Inference:** copying this
cursor scheme would risk missing late-arriving or clock-skewed records; it is
not proof of a lossless incremental protocol. Native sync needs durable
per-linked-book reconciliation state and retry-safe application before any
watermark advances. [Plugin cursors](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncannotations.lua)

The client retries certain read-only transport timeouts once with longer
timeouts. It does not automatically retry ambiguous writes or HTTP errors.
Pending tombstones survive failed pushes for subsequent attempts.
[Transport](https://github.com/readest/readest/blob/8d76d3fc1f0e26f8c5dbe1fc343f9d69256eefc2/apps/readest.koplugin/readest_syncclient.lua#L164-L208)

## Native PocketBook prerequisite

The examined public SDK header exposes `OpenBookmarks` and `SwitchBookmark`
using caller-provided page/position arrays. It does not establish a complete
native EPUB highlight-and-note write contract. This is a finding about this
header, not an exhaustive claim about all firmware internals.
[PocketBook SDK, pinned commit](https://github.com/pocketbook/SDK_6.3.0/blob/dd8de463733a2bc0d53d1214b64734586fac427e/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include/inkview.h#L2214-L2217)

Local firmware strings for U743g.6.11.1683 contain `getBookmarks`,
`saveBookmark`, `dbReadAllBookmarks`, `newBookmarkWithExternalUuid` and
`delBookmark` names under `PocketBook::ReaderLib::DocumentObj`. These are leads,
not a supported ABI or sufficient evidence for direct database writes.
Evidence: `build/readest-sync/firmware-6.11.1683/reader-strings.txt`.
The existing native reading-position probe is not an annotation decoder;
its point conversion must not be reused in a way that drops a range endpoint.
[Local probe](../../src/probe.cpp), [position conversion](../../src/position.cpp),
[probe tests](../../tests/probe_test.cpp).

No native annotation fixtures were inspected in this investigation; the device
was unmounted. The next concrete experiment is a controlled EPUB containing
a native highlight plus note, followed by edit and deletion snapshots. Inspect
the associated database and sidecar changes read-only, establish stable IDs,
range encoding, timestamps and deletion behavior, and verify a round trip on
that same book before enabling native writes. Do not assume an older community
database tag/type layout matches this firmware.

## Implementation implications

Reuse Readest's existing notes transport, preserve remote IDs, persist local
identity mappings and an outbox, and acknowledge only applied/accepted changes.
Test repeated sync, edits on both readers, deletion while offline, stale-pull
resurrection, interrupted writes and exact highlighted-text/range round trips.
These are proposed requirements derived from the observed protocol; annotation
sync has not been implemented or validated on the device by this research.

## First native device snapshot

On 2026-09-26, a read-only capture from the connected PB743G found two
highlights with attached notes in Beren und Lúthien (book item 48). Both are
`obj.book_mark` items with `bm.type=note`, UUIDs, `State=0`, and `TimeAlt`.
`bm.note` contains JSON text; `bm.quotation` contains JSON `begin`, `end`,
and selected `text`. Both endpoints are `pbr:/page?...#epubcfi(...)`
locations, including text offsets. `bm.book_mark` contains an anchor and
creation timestamp; color is the literal `cian`. Tag-level edit timestamps
are also present. UUID stability across edits and deletion behavior remain
unverified. This establishes the observed representation, not write support.

Evidence is a local private snapshot (not committed):
`/private/tmp/pocketbook-annotation-baseline-s0bpzmow/annotations.json`,
with the source database/WAL/SHM hashes recorded in `manifest.json`.
The source device files remained byte-for-byte unchanged after inspection.
Next comparison: edit the note “codex note” to “codex note edited” and delete
the complete highlight carrying “test note”, then capture again.

## Edit and deletion snapshot

The second capture is `/private/tmp/pocketbook-annotation-edited-j82_h0jt/`.
Both original items (49 and 50) now have `State=2` and retain their UUIDs
and range data. Item 50 changed from `note` to `highlight` with an empty note
object before its final timestamp. This is consistent with clearing the note
then deleting the highlight; the UI action sequence is not yet confirmed.

The edited note is a new item (51), `State=0`, with a new UUID and creation
timestamp. It contains “codex note edited”. Its quotation JSON matches the
original item 49 exactly, while item 49 retains the old note and has `State=2`.
Thus this observed edit workflow replaces identity rather than preserving it.
The user confirmed using the native Edit action, not deleting and recreating
the annotation. For this observed note edit, native Edit therefore replaces
the item and UUID while marking the previous item deleted. State 2 is consistent with a deletion marker
in this controlled comparison; broader state semantics remain unverified.
Sync must account for replacement plus deletion, not assume immutable native
UUIDs across note edits. All source DB/WAL/SHM hashes remained unchanged.

## First Readest-to-native import trial

Fetched the single live “readest import test” note through Readest's notes GET
endpoint using the existing device session (no cloud mutation). The Readest
book hash matched the local validated EPUB, whose SHA-256 was
`bcf0d4757fda435a1253efaccd323b13eb8518948c8889129837fc9b27bf7549`.
Its CFI range was `epubcfi(/6/20!/4/18,/1:0,/1:355)`. Both endpoints resolve
to the expected plain-text paragraph; the first 355 UTF-16 units equal the
selected text exactly. The remaining seven units are trailing whitespace.
The existing XPointer converter independently resolved the remote end
XPointer to the same CFI text offset 355.

A fixed-fixture diagnostic inserted one new item (52) and five annotation
tags, first into a local snapshot and then transactionally into the device
DB. Native IDs were resolved through type/tag names. Exact pre/post logical
database fingerprints, foreign keys, integrity, duplicate rejection and
rollback on missing-tag failure were checked. The prepared database and
backup are in `/private/tmp/pocketbook-annotation-import-45e25a3q/`; the
diagnostic script is `/private/tmp/pocketbook-annotation-import-trial.py`.
It uses `pbr:/webkit?##` point-CFI anchors, retains the note and yellow color,
and uses a deterministic diagnostic UUID. There were no changes to existing
annotation rows. The diagnostic is not part of the application or an enabled
sync feature. Native reader display and persistence remain to be tested;
successful SQLite insertion alone does not prove native import compatibility.

## Native display verification

Inspected device screenshots `screens/scr0011.bmp` and `screens/scr0012.bmp`
(copies in `/private/tmp/pocketbook-annotation-screens/`). The first shows
the exact intended paragraph highlighted, ending at its closing punctuation
and excluding the following paragraph. The second shows the native Comments
dialog with “readest import test” and Edit/Delete controls. The user also
confirmed correct display. This validates the single import fixture; it is
not an end-to-end bidirectional sync test.

Post-open database capture: `/private/tmp/pocketbook-annotation-after-open-ozyy9jta`. The imported UUID
is preserved and State remains 0. Changed tag names after opening: bm.book_mark, bm.quotation.
The reader normalized `pbr:/webkit?##` anchors to `pbr:/page?...#`
anchors with page/offset metadata, and removed the explicit zero text offset
from the start CFI. The end CFI offset 355, selected text, note, color and UUID
were preserved. Reconciliation must compare semantic ranges rather than raw
location strings, so this normalization does not create a false user edit.

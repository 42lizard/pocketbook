# Kobalt on PocketBook: source feasibility

Date: 2026-10-13. Reviewed source:
[`cbc0f4e9118c0e791056e36bc0b66d90e2af027c`](https://github.com/42lizard/kobalt/tree/cbc0f4e9118c0e791056e36bc0b66d90e2af027c).
This is source research, not evidence of a working PocketBook port. Build and
device results must be recorded separately. The initial target is the current
Qt 6 InkPad 4 environment; older PocketBook compatibility is not implied.

## Assessment

A standalone PocketBook port looks feasible without replacing the reader UI or
CREngine. The strongest evidence is the existing desktop host: it already uses
the shared Qt Widgets reader and Qt-only Android controller without Nickel or
the Android runtime. Shared controls support both Kobo Qt generations. This is
an engineering inference from the existing
[source boundaries](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/docs/development.md#shared-reader-controls)
and [desktop target](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/desktop/CMakeLists.txt).
Linux alone does not establish binary, firmware or display compatibility.

The Kobo installation package is the wrong starting point. Its services depend
on Nickel keyboard, light, battery, lifecycle and presentation adapters. Its
refresh implementation uses Kobo Qt attributes, native symbol lookup and
backing-store flush hooks. Those mechanisms need a PocketBook counterpart.
[Kobo host](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/nickel/common/reader_host_services.cpp),
[Kobo display](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/nickel/qt5/refresh.cpp).

## Reuse and adaptation boundaries

- **Reader and engine:** preserve publication loading, rendering, shared Widgets
  and reading state. The standalone controller already opens plain EPUB archives
  and constructs the shared reader window.
  [Controller](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/android/reader.cpp).
- **Host services:** implement the existing callbacks for clock, battery, page
  buttons, fonts, suspend state, frontlight, dictionary, keyboard, presentation
  and browser. Unsupported services still need valid callbacks/factories;
  `complete()` checks their presence. Desktop provides examples.
  [Interface](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/api/reader_host_services.h),
  [desktop services](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/desktop/services.cpp).
- **Display:** supply `textRefresh`, `pageRefresh`, and
  `setReaderRefreshWindow`. Desktop's implementations are no-ops; host rendering
  therefore cannot establish actual e-ink refresh quality. Device testing must
  check page turns, menu cleanup and repaint completion.
  [Refresh API](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/api/kobo_refresh.h),
  [desktop adapter](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/desktop/display.cpp).
- **Input:** the existing canvas converts touch to mouse events for embedded
  Widgets; host services map page keys. Verify PocketBook's delivered events
  before choosing a direct full-screen QWidget or reusing the canvas, avoiding
  duplicate translation. The canvas uses Qt 6 touch APIs and is not a drop-in
  Qt 5 host.
  [Canvas](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/android/canvas.h).
- **Rotation:** the standalone controller passes a null sensor and host rotation
  callback. Automatic rotation is optional at runtime, but the shared
  `ReaderOrientation` still includes/calls Qt Sensors and inherits
  `QOrientationFilter`. Simply omitting the Sensors link is insufficient. If
  the SDK lacks it, make sensor support optional in this narrow module.
  [Header](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/api/reader_orientation.h),
  [implementation](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/api/reader_orientation.cpp).
- **Local progress:** keep independent Kobalt state initially. `ReadingBook`
  supports a null native bridge, saves local checkpoints, and skips identical
  checkpoint writes. Hosts choose directories. Use a dedicated document cache:
  CREngine deletes unrelated files found in that directory.
  [Book state](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/reading/reading_book.cpp),
  [storage paths](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/storage/reader_storage.h).
- **Native progress and Readest:** `ReadingBridge` models a platform database
  and activity recorder, while `PositionCodec` maps engine/platform positions.
  These are extension points, not proof of compatible position formats. A
  local-only port will not automatically participate in Readest Sync.
  [Bridge](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/reading/reading_bridge.h),
  [codec](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/api/position_codec.h).
- **Launch and assets:** adapt desktop bundle paths to PocketBook directories.
  Desktop initializes bundled fonts/assets and accepts a book path as its first
  argument. Reuse these responsibilities; a new library UI is unnecessary for
  the first experiment.
  [Desktop entry point](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/src/platform/desktop/main.cpp).

## Proposed minimal stages

1. Pin the user's fork as a submodule. Keep Kobalt platform changes in that fork
   and PocketBook build/package integration in this repository. Preserve shared
   renderer/UI sources, dependency pins, patches and notices.
2. Cross-build a standalone target around the existing controller and a small
   PocketBook host. Start with one explicit DRM-free EPUB path, independent
   state/cache directories, no native database bridge and no page animations.
3. Validate shared reader behavior on the host, then on InkPad 4: visible page,
   touch/page keys, menus, checkpoint/reopen, Home/exit, suspend/wake and USB
   lifecycle. Hardware must establish refresh quality, memory use and power
   behavior.
4. Add device services incrementally. Library/default-reader registration,
   native positions and Readest sync are later capabilities with separate tests.

These are proposed stages, not completed implementation. Upstream also
explicitly distinguishes portable tests from firmware integration checks in its
[test documentation](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/docs/development.md#tests).

## Initial local evidence

The fork is pinned, unmodified, under `external/kobalt` on the separate
`experiment/kobalt-pocketbook` branch. The reproducible
[compatibility probe](../../tools/kobalt-probe/README.md) links Kobalt's publication
component, Qt Widgets and InkView with the existing PocketBook toolchain.

On 2026-10-13 the ARM build and existing ABI check passed: ARM softfp,
`/lib/ld-linux.so.3`, no runtime search path, and the existing firmware
symbol-version ceilings. This establishes compilation/linkage of that component,
not runtime compatibility of the full reader. No binary was installed on a device.

The host CTest also passed: an EPUB from Kobalt's fixture generator was inspected,
compressed chapter text read, invalid ZIP input rejected and a Qt Widgets window
painted. CREngine, Kobalt's reader controls and hardware behavior are outside
this probe's coverage.

The current SDK contains Qt Widgets and Network 6.8.2, but no Qt Sensors module.
The retained `build/readest-sync/firmware-6.11.1683/` file inventories list
`libQt6Widgets.so.6.10.3` and `libQt6Network.so.6.10.3`. These local observations
support using the native Qt runtime for this target, not all PocketBook models.
The existing SDK deliberately declares imported Qt targets instead of using
Debian's normal Qt package configuration, which requires OpenGL. Kobalt's
standalone CMake wiring therefore also needs an adaptation; changing the target
name or the compiler alone is insufficient.

## Distribution inventory

Upstream labels its own source MIT, CREngine patches GPL v2 or later, and Qt
patches under Qt's licenses. It describes the combined package as AGPL v3 and
supplies corresponding source/dependency sources with releases. Preserve this
inventory instead of treating the whole executable as MIT-only. This records
upstream's stated distribution model, not a separate legal assessment.
[License statement](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/README.md#license),
[credits](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/ACKNOWLEDGEMENTS.md),
[source-bundle procedure](https://github.com/42lizard/kobalt/blob/cbc0f4e9118c0e791056e36bc0b66d90e2af027c/docs/development.md#ci-and-tagged-releases).

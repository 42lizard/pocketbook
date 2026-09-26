# Readest Sync simulator adapter

The [shared simulator](../../README.md) owns the browser launcher, device controls
and QML shims. This directory contains only Readest-specific mock cloud fixtures,
reader/progress simulation, HTTPS routing and integration tests.

In **Mock cloud**, sign in to the demo account and refresh the library. It has
51 distinct generated EPUB identities: seven portrait pages or nine landscape
pages at the default sizes. Some entries have position data only, multiple EPUBs, or no cover. Covers
include PNG and JPEG stored as `cover.png`. Book 01 always has one EPUB.

## Reading and conflict scenario

1. Select **01 · Position playground**, download it, choose **Read offline**, then
   **Back to Readest Sync**. This creates simulated native reading settings.
2. Choose **Sync now** to establish the initial chapter-1 baseline.
3. In Simulator, change Readest to **Chapter 2**. Hide the panel, choose **Sync now**,
   then **Open at Readest position**. The app applies the position through its
   normal guarded SQLite transaction and conditional update path.
4. Turn the simulated reader to chapter 3 and return. Change Readest to chapter 1.
   **Sync now** now offers the app's conflict choices.
5. Choose **Use Readest position**, then **Open at Readest position** to test an
   explicit move backward. Or choose **Use PocketBook position** to upload the
   farther-ahead local position.

Position controls affect the last opened book, initially book 01. The mock reader
models handoff and saved positions; it does not render the EPUB.

## Fault scenarios

- Shared Wi-Fi controls can fail, delay five seconds, or hold the callback.
  Readest uses its actual 60-second timeout. Release a held callback before
  retrying; selecting Connected does not release an outstanding callback.
- Mock cloud controls offer HTTP 503, slow requests, truncated downloads and
  corrupted bytes. The app's normal integrity and installation code handles them.
- Cancel via Back while a slow operation runs. Readest cancels and exits; restart
  the simulator to check recovery. Cached covers may not trigger new transfers.
- The automated suite verifies recovery of a completed download from metadata
  when its app-state registration is missing.

## Real cloud

Choose **Real Readest** in the panel, reconnect after the app restarts, then sign
in with your real account. Real mode uses `src/http.cpp` with the system CA bundle;
mock HTTP faults and synthetic position edits are unavailable. Wi-Fi connection
callbacks remain simulated. Real-mode reader handoff shows the downloaded path
without writing invented chapter positions to your real library.

The real-mode test sends fake credentials (including symbols and Unicode) through
the production HTTPS client to an isolated local TLS server. It checks routing,
CA validation, downloads and session isolation. It does not establish that a
particular live account can authenticate, or test browser keyboard translation.

## Transport tests

Each application selects one `HttpTransport` for requests, downloads and covers.
The real adapter uses the production HTTPS client; `MockCloud` owns its fixture
state and fault settings. Cancellation is passed per operation. The overlay
shares its mock instance with the application, and cloud-mode switching still
restarts the simulator with a separate storage profile.

The `mock-cloud` CTest check runs authentication, library refresh, covers,
downloads and progress synchronization without a GUI application or display.
It also checks isolated instances, persisted remote progress and cancellation.
The mock uses Qt Core. Cover artwork is generated at build time using Qt Gui,
so the headless test can load the same PNG/JPEG bytes as the simulator.

Run all simulator checks with:

```sh
docker compose run --rm simulator sh -ec '
  cmake -S apps/readest-sync -B build/readest-sync/simulator -DPOCKETBOOK_SIMULATOR=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/readest-sync/simulator -j2
  ctest --test-dir build/readest-sync/simulator --output-on-failure
'
```

## Annotation coverage

Mock mode supplies a native annotation database and the notes sync endpoint.
The automated annotation suite covers highlights and attached notes in both
directions, including edits, deletions, retries and conflicts. The simulator
reader panel is a position/handoff model, not an EPUB renderer or annotation
editor; use the native reader and Readest for visual annotation acceptance.

## Documentation screenshots

The [user guide](../../../../apps/readest-sync/docs/USER-GUIDE.md) uses the real
app QML with mock fixtures. Recreate its screenshots without accessing a real
account or changing the interactive simulator's storage:

```sh
docker compose run --rm -e READEST_DOC_SCREENSHOTS=/workspace/build/readest-sync/doc-screenshots simulator sh -ec '
  cmake --build build/readest-sync/simulator --target simulator-test -j2
  ctest --test-dir build/readest-sync/simulator -R "^simulator$" --output-on-failure
'
```

Build/configure the simulator first using the command above if the build directory
does not yet exist. This test uses a temporary mock profile. It captures the library,
library menu, downloaded book details, mock reader and reading-position conflict.
Review the resulting PNGs, then copy them into
`apps/readest-sync/docs/images/`. The images retain the **Simulator · Mock** label;
they are not device screenshots. `READEST_UI_PREVIEW` remains available for the
broader test preview set, including orientation layouts.

# Readest Sync simulator adapter

The [shared simulator](../../README.md) owns the browser launcher, device controls
and QML shims. This directory contains only Readest-specific mock cloud fixtures,
reader/progress simulation, HTTPS routing and integration tests.

In **Mock cloud**, sign in to the demo account and refresh the library. It has
51 distinct generated EPUB identities: nine portrait pages or thirteen landscape
pages. Some entries have position data only, multiple EPUBs, or no cover. Covers
include PNG and JPEG stored as `cover.png`. Book 01 always has one EPUB.

## Reading and conflict scenario

1. Select **01 · Position playground**, download it, choose **Read offline**, then
   **Back to Readest Sync**. This creates simulated native reading settings.
2. Choose **Sync now** to establish the initial chapter-1 baseline.
3. In Simulator, change Readest to **Chapter 2**. Hide the panel, choose **Sync now**,
   then **Open at Readest position**. The app applies the position through its
   normal database backup and conditional update path.
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

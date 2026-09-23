# PocketBook UI patterns for Readest Sync

This note resolves the Wayfinder question in [Document PocketBook UI patterns for Readest Sync](https://github.com/42lizard/pocketbook/issues/7). It records constraints for later UI decisions; it is not an implementation specification.

## Decision

Readest Sync should use the native PocketBook Library as its primary interaction model and PocketBook system surfaces for menus, dialogs, progress, and errors. The resulting UI should be cover-first, sparse, page-oriented, and driven by the device's established tap, hold, menu-key, and page-key behavior. Readest identity should appear through the app name and restrained accents rather than a separate visual system.

The current app already imports PocketBook controls and uses the firmware `AppHeader` and `FramedTextInput`, but its main library presents two permanent top-level action buttons, a full-width search field, five equally weighted filter buttons, bordered tiles, explicit previous/next buttons, status text, and a permanent account action. That density and equal visual weight explain much of the foreign-body effect ([Main.qml](../../apps/readest-sync/qml/Main.qml), [LibraryPage.qml](../../apps/readest-sync/qml/LibraryPage.qml)).

## Native patterns to adopt

### Library hierarchy

PocketBook defines the Library as a book file manager. Filter, grouping, sorting, search, and view changes live in the Library menu rather than in every book row. It offers cover, detailed, and list densities; the detailed and list presentations expose title, author, reading percentage, and status. Secondary actions such as book information, mark as read, collections, favorites, delete, selection, and open-with live in a long-press context menu. These patterns are documented in the official InkPad 4 manual, pp. 75–78.

Readest Sync should therefore:

- make books the dominant content and default to a detailed cover or list presentation;
- show title, author, reading progress, and one compact availability/sync mark in the first layer;
- move filter, sort, view, refresh, device scan, and account management into a Library menu or another progressively disclosed system surface;
- keep destructive and secondary book actions in book details or a hold/context action surface;
- remove permanent previous/next controls when physical page keys and page gestures can provide the native path, while retaining an accessible touch path where hardware validation shows it is needed.

### Availability and synchronization

The native Library draws a dotted frame around a cloud-only cover and distinguishes device-and-cloud, cloud-only, and device-only books. Synchronization is a transient system state: an animated status-bar indicator appears while work is active, tapping it opens service detail, errors change the indicator, and successful completion removes it. A force-sync action lives in the notification panel. See the official manual, pp. 74–75 and 81–82.

Readest Sync should reuse that vocabulary: a compact cloud-only treatment on the cover, one transient global sync indicator with detail on demand, no persistent success decoration, and a compact retained error state. Exact icons must use assets or controls available on the target firmware rather than imitations drawn from screenshots.

### Input and navigation

On InkPad 4, tap opens an item, touch-and-hold opens its context menu, slide changes a book or list page, and flick scrolls lists. The Menu key opens the available menu. Backward and Forward act as page keys, while mappings and direction change with user configuration and orientation. The Home key returns to the Main Menu. These behaviors are described in the official manual, pp. 11 and 18, and the reading-menu behavior on pp. 52–54.

Every Readest Sync screen should expose its applicable menu through the Menu key. Page keys should move through the current paged collection or selection context, respecting configured mapping and orientation. Touch should follow tap-to-open and hold-for-context semantics. Visible focus must support hardware navigation, but its exact native treatment needs measurement on the target device.

### Native shell and dialogs

The official InkView header exposes application caption and theme primitives (`DrawApplicationCaption`, `GetCaptionHeight`, `LoadApplicationCaptionProperties`, `GetAppGlobalStyle`, `GetAppStyle`, theme font accessors), menus and dialogs (`OpenMenu`, `OpenContextMenu`, `OpenList`, `Message`, `Dialog`, `Dialog3`), progress surfaces, keyboards, book metadata/cover APIs, and book sync states. It also exposes key mapping, orientation adjustment, focus events, page events, and pointer hold/drag events.

These APIs establish the native vocabulary even where Qt Quick owns rendering. The next decisions should prefer firmware PocketBook controls and runtime theme metrics already available through `com.pocketbook.controls`; custom QML should fill demonstrated gaps rather than recreate the whole shell.

### E Ink behavior

InkView separates full/high-quality, soft, partial, black-and-white, DU4, and dynamic updates. Its header warns that overlapping asynchronous dynamic updates before completion can create artifacts and provides `WaitForUpdateComplete`. The InkPad 4 firmware 6.8.3711 notes also record fixes for slider ghosting, hardware-button list scrolling, orientation-aware previous/next behavior, double page turns, and skipped pages.

The prototype should avoid continuous animation, update the smallest coherent region, coalesce intersecting changes, serialize overlapping updates, and use full/high-quality refreshes at page or state boundaries to clear accumulated artifacts. Exact waveform choice and refresh cadence remain hardware findings, not design assumptions.

Firmware 6.10 added adjustable UI text size, bold text, grey-text contrast, and a dyslexic font. Layouts should allow text growth and avoid tight fixed rows or low-contrast grey as the only carrier of state.

## Prototype and hardware checks

The simulator prototype should exercise:

- detailed cover/list presentation with several title and author lengths;
- cloud-only, device-only, combined, syncing, error, and completed states;
- Library menu access by touch and Menu key;
- tap, hold/context, page keys, keyboard focus, scrolling, and orientation changes;
- empty, loading, offline, and failure states without persistent technical text dominating the library.

Hardware acceptance on InkPad 4 firmware `6.11.1683` must measure native caption, font, row, icon, dialog, focus, and touch geometry; test rapid state changes and long-list navigation; verify configured key mappings in both orientations; and confirm ghost cleanup after paging and progress changes.

## Evidence gaps

- PocketBook publishes no public HIG or numeric minimum touch target. Measure native Library and Settings controls and finger-test the prototype on the reference device.
- No first-party release notes or public SDK source for target firmware `6.11.1683` were found. The newest official release notes found were `6.10.2767`; validate all visual and input assumptions on the actual device.
- The public SDK provides a 6.8 release and 6.5 source branch. Treat its API as vocabulary and capability evidence, not proof of 6.11 ABI or pixel-level behavior.
- Focus appearance, dialog geometry, caption height, fonts, and refresh waveforms depend on runtime theme and hardware.

## Primary sources

- [PocketBook InkPad 4 user manual, firmware 6.8.2015](https://support.pocketbook-int.com/fw/743G/u/6.8.2015/manual/User_Manual_InkPad_4_EN.pdf)
- [Official PocketBook SDK repository](https://github.com/pocketbook/SDK_6.3.0)
- [Official SDK 6.5 InkView header](https://github.com/pocketbook/SDK_6.3.0/blob/6.5/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include/inkview.h)
- [Official PocketBook SDK 6.8 release](https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8)
- [Official InkView documentation repository](https://github.com/pocketbook-free/InkViewDoc)
- [InkPad 4 firmware 6.8.3711 release notes](https://support.pocketbook-int.com/fw/743G/u/6.8.3711/rn/ww/RN_PB_InkPad_4_EN.pdf)
- [InkPad 4 firmware 6.10.2767 release notes](https://support.pocketbook-int.com/fw/743G/u/6.10.2767/rn/ww/RN_PB_InkPad_4_EN.pdf)

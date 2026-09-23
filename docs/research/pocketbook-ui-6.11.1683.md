# PocketBook InkPad 4 UI measurements on firmware 6.11.1683

This note resolves [issue #12](https://github.com/42lizard/pocketbook/issues/12). It measures the native PocketBook UI on an InkPad 4 running `U743g.6.11.1683` with the `Line` theme and English interface. The capture was made on 2026-09-23 at the device's native `1404 × 1872`, 300 dpi, 4-bit grayscale framebuffer resolution.

The measurements below describe observed firmware behavior. They are reference inputs for the Readest Sync component language, not requirements to reproduce PocketBook controls pixel for pixel.

## Reference set

| Surface | Evidence | Capture method |
| --- | --- | --- |
| Detailed Library list | [library-detailed-list.png](assets/pocketbook-ui-6.11.1683/library-detailed-list.png) | Device screenshot |
| Library category menu | [library-category-menu.png](assets/pocketbook-ui-6.11.1683/library-category-menu.png) | Device screenshot |
| Library sort menu | [library-sort-menu.png](assets/pocketbook-ui-6.11.1683/library-sort-menu.png) | Device screenshot |
| Book context menu | [book-context-menu.png](assets/pocketbook-ui-6.11.1683/book-context-menu.png) | Device screenshot |
| Destructive confirmation | [delete-dialog.png](assets/pocketbook-ui-6.11.1683/delete-dialog.png) | Device screenshot |
| Search without keyboard | [search-input.png](assets/pocketbook-ui-6.11.1683/search-input.png) | Device screenshot |
| Book information | [book-info-photo.jpg](assets/pocketbook-ui-6.11.1683/book-info-photo.jpg) | Device photograph |
| Search with keyboard | [keyboard-photo.jpg](assets/pocketbook-ui-6.11.1683/keyboard-photo.jpg) | Device photograph |

Long-pressing Home before choosing **Take a screenshot** first dispatches Home and removes transient surfaces. It returned the Book info page to Library and dismissed the on-screen keyboard. The two photographs record those surfaces without changing their state. Do not use the photographs for exact pixel geometry because of perspective.

## Measured geometry

Direct framebuffer captures provide the following high-confidence measurements.

| Component | Pixel measurement | Approximate physical size at 300 dpi | Observation |
| --- | ---: | ---: | --- |
| Status bar | `136 px` high | `11.5 mm` | Persistent time, task-manager affordance, Wi-Fi, light, and battery |
| Library toolbar | `138 px` high | `11.7 mm` | Full-width row below the status bar |
| Detailed Library row | `199 px` high | `16.8 mm` | Separators occur at y = 273, 472, 671, 870, and then every 199 px |
| Date gutter | `63 px` wide | `5.3 mm` | Vertical date text is secondary metadata outside the main row content |
| Detailed-list cover | `105 × 155 px` | `8.9 × 13.1 mm` | Compact cover at x = 79…183 in the first row |
| Category menu | `579 × 1163 px` | `49.0 × 98.5 mm` | x = 191…769, y = 272…1434; 10 px outer stroke |
| Sort menu | `432 × 546 px` | `36.6 × 46.2 mm` | x = 633…1064, y = 274…819; anchored below the active toolbar item |
| Book context menu | `662 × 1163 px` | `56.0 × 98.5 mm` | x = 684…1345, y = 274…1436; 10 px outer stroke |
| Context-menu item rhythm | about `123 px` | `10.4 mm` | One icon and one text label per row, divided by hairlines |
| Confirmation dialog | `1200 × 574 px` | `101.6 × 48.6 mm` | x = 102…1301, y = 1152…1725; positioned low over dimmed content |
| Dialog action band | `134 px` high | `11.3 mm` | Two equal actions separated by a single vertical rule |
| Search field | `1095 × 139 px` | `92.7 × 11.8 mm` | x = 155…1249, y = 135…273; back action remains outside the field |
| Search-results heading | `137 px` high | `11.6 mm` | Separate band between the field and results |

The Library toolbar partitions actions into large, direct targets: Home, current collection, current sort, search, and view. The active collection or sort item becomes a black field with white content. Popup menus align to the activating region instead of centering on the screen.

## Native hierarchy and component behavior

### Detailed Library

- The book list owns almost all vertical space. There is no persistent bottom navigation or action bar.
- Rows use a compact cover, bold title, regular author, and italic progress or completion state. Series metadata follows the state after a thin separator.
- A narrow date gutter sits outside the content column. A narrow scroll indicator appears at the far right.
- Collection, sort, search, and view controls live in the toolbar. Their menus overlay and visually mute the list rather than navigating to a separate settings screen.
- Page buttons move through the Library. Home returns to the main screen, Menu exposes the applicable menu, and Back returns to the preceding surface.

### Menus and selection

- Popup menus use a heavy black outer stroke, white fill, single-color line icons, large labels, and thin row separators.
- The selected item is a black rounded rectangle with white icon and label.
- The category menu is left-aligned below its toolbar trigger. The sort menu is narrower and aligned below the sort trigger.
- The book context menu is right-aligned with a small pointer aimed at the selected row. It exposes Book info, reading-state, collection, favorite, author, delete, cloud, selection, and open-with actions.
- Touch targets are based on the complete row. Icons identify actions, but labels remain the primary carrier of meaning.

### Book information

- The page retains the status bar and adds a dedicated header with Back and a centered uppercase `BOOK INFO / TITLE` caption.
- The main region uses a large cover column and a metadata column. Labels are bold; values are regular; hairlines separate groups.
- Favorite is a standalone outline-heart action near the start of the metadata column.
- Added date, size, and format form a three-column strip below the main metadata.
- A full-width outlined `CHOOSE ACTION` drawer is fixed to the bottom edge. Secondary actions remain collapsed until requested.

### Search and keyboard

- Search replaces the normal Library toolbar with Back, a large outlined search field, clear action, and view action.
- Result count is a separate centered heading. The empty state uses one large centered message without an illustration.
- The keyboard occupies roughly the lower two fifths of the screen and preserves the search context above it.
- A suggestion strip sits above three alphabetic key rows. The bottom row contains numeric mode, language, space/language label, punctuation, and the primary Search action.
- Keys use thin gray outlines and spacious rectangular targets. The primary Search key is the only filled control.

### Dialogs

- The confirmation dialog appears low on the screen and leaves the underlying source context visible in gray.
- One large question-mark symbol sits beside a short, direct prompt.
- Actions use an undifferentiated two-column band. Neither destructive nor cancel action receives a filled emphasis in the captured state.

## Focus, orientation, and E Ink observations

- A visible focus highlight appears during activation and disappears immediately after the click. It cannot be held long enough for the built-in screenshot path. Readest Sync should treat focus as transient feedback and avoid relying on persistent focus decoration as the only state cue.
- The native Library stayed in portrait when the device was physically rotated, even with the G-sensor enabled. No Library landscape reflow occurred on `6.11.1683`. Readest Sync should therefore make portrait the hardware reference and treat simulator landscape support as defensive layout behavior rather than a native Library parity target.
- No residual ghosting was observed during rapid Library paging and repeated menu opening. A full refresh occurred only after several interactions.
- Book covers briefly flashed during list updates. Static text and separators remained stable. Readest Sync should avoid repainting covers when only text or sync state changes, and it should allow periodic full refreshes to clear accumulated artifacts.

## Differences from the public 6.8/6.10 evidence

The target device confirms the interaction vocabulary found in the public 6.8 manual and 6.10 accessibility notes: detailed/list hierarchy, tap and hold, large row actions, sparse line icons, transient sync behavior, and strong black/white selected states remain valid.

The target capture adds facts that were not safe to infer from those sources:

- exact status, toolbar, row, menu, dialog, and input geometry at `1404 × 1872`;
- a `199 px` detailed-row rhythm with a separate date gutter;
- transient focus that disappears after activation;
- no automatic Library landscape layout despite an enabled G-sensor;
- cover-only flashing during partial list updates and no observed residual ghosting;
- the target firmware's Book info structure and full on-screen keyboard layout.

## Implications for Readest Sync

Use these measurements as the starting proportions for the component-language ticket:

- reserve one status/header band, then give the remaining space to content;
- use approximately `138 px` primary control bands and `199 px` detailed book rows at the reference resolution;
- keep book status to one short italic line in the list;
- use anchored overlay menus with approximately `123 px` action rows;
- use low, source-preserving dialogs with an approximately `134 px` action band;
- keep search and keyboard as a dedicated mode while preserving the user's Library context;
- implement portrait first and verify any landscape layout independently in Readest Sync;
- isolate cover updates from text and status updates to reduce visible flashing.

## Provenance

The original device screenshots are 4-bit BMP files. Repository assets are PNG conversions with no retouching. The two device photographs are downscaled JPEG derivatives of the original HEIC files; they are evidence of hierarchy and behavior rather than sources for exact pixel measurements.

The capture wizard is available at [`tools/capture_pocketbook_ui.sh`](../../tools/capture_pocketbook_ui.sh). Its Bash syntax and ShellCheck validation passed before use.

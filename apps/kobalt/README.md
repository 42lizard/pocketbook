# Kobalt on PocketBook

Experimental standalone EPUB reader for **InkPad Color 3 (PB743K3), firmware
U743k3.6.10.2854**. The device supplies Qt 6.8.2 and its `pocketbook2` platform
plugin. Other models and firmware versions are not validated.

The port reuses [Nico Verbruggen's Kobalt](https://github.com/nicoverbruggen/kobalt)
through [42lizard's fork](https://github.com/42lizard/kobalt/tree/pocketbook-port)
in `external/kobalt`. CREngine, the reading interface, settings, bookmarks and
local state remain upstream components. A small PocketBook host replaces Kobo
integration. This is a development build; physical-device acceptance is pending.

## Build and verify

From the parent repository root, initialize the submodule and build the SDK image:

```sh
git submodule update --init external/kobalt
docker compose build qt6
```

Download the [official target firmware](https://download.pocketbook-int.com/fw/Yitoa/743k3/user/20260105/sw_20260105_U743k3_6.10.2854_user.zip)
and extract its `SWUPDATE.BIN` to `build/kobalt/firmware-color3/`. This is a local
build input; do not copy the firmware onto the device. Install Python 3.11+
and 7-Zip (`7zz`) on the host, then extract the libraries:

```sh
python3 apps/kobalt/tools/prepare_firmware.py \
  build/kobalt/firmware-color3/SWUPDATE.BIN build/kobalt/firmware-prepared
```

The helper rejects a different firmware SHA-256. Extraction requires roughly
1 GiB of disk space. Provenance and runtime evidence are recorded in
[the firmware report](../../docs/research/kobalt-color3-runtime.md).

```sh
docker compose run --rm qt6 python3 external/kobalt/pocketbook/build.py \
  --firmware-libs /workspace/build/kobalt/firmware-prepared/runtime/lib --jobs 4

docker compose run --rm qt6 python3 apps/kobalt/tools/check_firmware.py \
  external/kobalt/build/pocketbook/arm/app/kobalt \
  --library-dir build/kobalt/firmware-prepared/runtime/lib \
  --library-dir build/kobalt/firmware-prepared/rootfs/lib

docker compose run --rm qt6 python3 external/kobalt/pocketbook/build.py --host --jobs 4
python3 apps/kobalt/tests/test_package.py
```

The host test opens a real EPUB, turns a page, closes the reader and compares the
rendered page after reopening with the same profile. The firmware check verifies
ARM softfp, loader, absence of build RPATHs, direct library availability and exact
strong imported symbol names/versions. It does not simulate the device or verify
all transitive firmware dependencies.

Run the complete upstream regression suite with `--host --regression`. Use
`--skip-dependencies` after dependencies and assets have been built. Dependency
versions, checksums and CREngine patches use Kobalt's existing manifests.

## Package

```sh
python3 apps/kobalt/tools/package.py \
  --binary external/kobalt/build/pocketbook/arm/app/kobalt \
  --assets external/kobalt/build/pocketbook/arm/assets/install/.adds/kobalt/assets \
  --notice external/kobalt/LICENSE \
  --notice external/kobalt/ACKNOWLEDGEMENTS.md \
  --notices-dir external/kobalt/resources/notices \
  --output build/kobalt/Kobalt-PB743K3.zip
```

The packager refuses to overwrite an existing ZIP. Font and third-party licenses
are included under `notices/`; acknowledgments and full notices are also
compiled into the reader's About screen. The ZIP contains only:

```text
applications/kobalt.app        launcher
system/kobalt/bin/kobalt       ARM executable
system/kobalt/assets/          fonts, hyphenation and manifest
system/kobalt/notices/         license, acknowledgments and third-party notices
```

No Qt libraries, firmware, Kobo installation files, books or user state are
packaged. The combined reader includes GPL/AGPL dependencies: use Kobalt's
[corresponding-source procedure](../../external/kobalt/docs/development.md#ci-and-tagged-releases)
when distributing binaries. A checkout alone is not a complete source bundle.

## Install and try

Connect the PB743K3 by USB. Extract the ZIP on the computer, then copy only
`applications/kobalt.app` and `system/kobalt/` to the matching device paths.
Merge directories; keep existing `config/`, fonts, diagnostics and other apps.
For an update, keep a copy of the old launcher, binary and assets first.
Safely eject and disconnect USB, then open **Kobalt** in Applications.

Choose **Open EPUB**, select an unprotected local EPUB, turn pages and exit.
The next launch offers the last book and restores its saved position.
Hardware page keys and touchscreen page turns use the existing reader actions.
Home exits after pending reading work drains. The reader's close action returns
to its start screen. An EPUB path may also be passed to the launcher.

Kobalt stores its own progress, bookmarks, preferences and layout cache under
`/mnt/ext1/system/kobalt/config`. Logs are in `app.log` with one bounded
startup rotation; diagnostic files are under `diagnostics/`. It does not copy or
modify PocketBook's reading database and does not register as the default reader.

Native-library progress/annotation integration and Readest sync are not part of
this port. Automatic G-sensor rotation, in-app frontlight control, dictionary
installation and external browser launch are unavailable. Manual rotation uses
InkView. Native keyboard, color refresh, suspend/wake, Home, USB handling and
rotation still need validation on the actual PB743K3.

To remove the application, delete `applications/kobalt.app` and
`system/kobalt/bin`, `assets`, and `notices`. Retain `config` if reading progress
should survive reinstallation; deleting all of `system/kobalt` removes its data.

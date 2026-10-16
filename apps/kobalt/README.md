# Kobalt on PocketBook

The experimental PocketBook InkPad Color 3 port is maintained in
[42lizard/kobalt](https://github.com/42lizard/kobalt/tree/pocketbook-port), included
here as the `external/kobalt` submodule. Its buildchain is now self-contained:
the fork includes the SDK recipe, firmware preparation, ABI/runtime checks,
packaging, registration helper and tests. A standalone clone does not require
this monorepo.

## Build and install

Follow [PocketBook build and installation](../../external/kobalt/pocketbook/README.md)
for the complete workflow. From this repository:

If this checkout has old monorepo build output, move
`external/kobalt/build/pocketbook` aside once; the new container uses a different
source mount, which invalidates the old CMake caches.

```sh
git submodule update --init external/kobalt
cd external/kobalt
./build-pocketbook.sh --firmware /absolute/path/SWUPDATE.BIN --jobs 4 --test
```

The firmware input must be the official PB743K3 U743k3.6.10.2854
`SWUPDATE.BIN`; its checksum is verified. The outputs are
`external/kobalt/build/pocketbook/Kobalt-PB743K3.zip` and its `pocketbook.json`
provenance record. Firmware libraries and user data are never packaged.
Installation and registration are documented in the fork; the build does not
write to a connected device.

## Fork maintenance

Keep the fork's `main` aligned with upstream. Rebase `pocketbook-port` onto new
upstream revisions instead of merging them, preserving a linear patch series
for a future upstream contribution. Before rewriting published history, archive
device-confirmed and tested source revisions, use an explicit force-with-lease,
and update this repository's submodule pin.

The historical runtime evidence and device acceptance status are recorded in
[the firmware report](../../docs/research/kobalt-color3-runtime.md). Startup and
EPUB opening were confirmed with `6de0711`; the rebased upstream synchronization
`1a17e2f` has been installed but still awaits physical acceptance. Remaining
native integration limitations are listed in the fork's documentation.

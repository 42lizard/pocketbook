# Kobalt PocketBook compatibility probe

This is an isolated port experiment, **not a working Kobalt reader**. It compiles
the fork's unchanged publication/archive component with Qt Widgets and the
existing PocketBook startup adapter. It does not include CREngine, Kobalt's
reader UI, native reading-state integration or a device installer.

The source is pinned by the `external/kobalt` submodule to the user's
[fork](https://github.com/42lizard/kobalt). Original project:
[Nico Verbruggen's Kobalt](https://github.com/nicoverbruggen/kobalt).
Do not run Kobalt's Kobo installer on PocketBook.

## Reproduce the checks

From the repository root:

```sh
git submodule update --init external/kobalt

# Cross-compile for the current InkPad 4 target and check the ARM ABI.
docker compose run --rm qt6 sh -ec '
  cmake -S tools/kobalt-probe -B build/kobalt/probe-arm -DCMAKE_BUILD_TYPE=Release
  cmake --build build/kobalt/probe-arm -j2
  python3 tools/check_qt6_app.py build/kobalt/probe-arm/kobalt-probe.app
'

# Run the same publication component and a Widgets paint check on the host.
docker compose run --rm qt6 sh -ec '
  unset CMAKE_TOOLCHAIN_FILE
  cmake -S tools/kobalt-probe -B build/kobalt/probe-host -DKOBALT_PROBE_HOST=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build/kobalt/probe-host -j2
  ctest --test-dir build/kobalt/probe-host --output-on-failure
'
```

The host test generates an EPUB with Kobalt's own fixture generator, inspects
its archive, reads compressed chapter text, rejects invalid ZIP input and grabs
a painted Widgets window. It does not test EPUB layout or actual e-ink output.

The ARM binary is `build/kobalt/probe-arm/kobalt-probe.app`. If launched manually,
it shows a clearly labelled probe screen and Close button. An optional first
argument supplies a read-only EPUB archive to inspect. No native databases or
book files are written. The probe is excluded from normal app builds/releases.

## Next port milestone

Reuse the shared reader/CREngine and standalone controller through a small
PocketBook host. Build the pinned engine dependencies with the PocketBook
toolchain, make Qt Sensors optional, adapt startup/assets/state paths and connect
the existing display/host-service interfaces. Start with one explicit EPUB path
and local Kobalt progress, then validate page turns, touch, exit and suspend on
the device. Native library registration and Readest integration are separate work.

See the [source research and evidence](../../docs/research/kobalt-pocketbook-port.md).
All upstream notices remain in the submodule; a complete reader package has
additional dependency/source-distribution requirements described there.

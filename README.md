# PocketBook Qt6 app monorepo

Build Qt Quick apps for PocketBook using
[fstanis/pocketbook-sdk-qt6](https://github.com/fstanis/pocketbook-sdk-qt6).
The current target is **InkPad 4 (PB743G), firmware U743g.6.11.1683**. That
firmware contains Qt 6.10.3, the `pocketbook2` platform plugin and
`com.pocketbook.controls`. We compile against the upstream Qt 6.8.2 toolchain.
Other devices, including Verse Pro, need separate validation.

- **Hello** is a small Qt Quick example using PocketBook controls.
- **[Readest Sync](apps/readest-sync/README.md)** downloads EPUBs and synchronizes
  native-reader progress with Readest Cloud. Its Qt6 UI has been launched on InkPad;
  full device acceptance of the latest build is pending. The previous InkView UI's login,
  networking, downloads, progress sync and cover browsing were user-confirmed.

## Build

With Docker and Compose running, from the repository root:

```sh
docker compose build qt6
docker compose run --rm qt6 make                    # All apps
docker compose run --rm qt6 make APP=readest-sync    # One app
docker compose run --rm qt6 make check              # Build and check all apps
docker compose run --rm qt6 make APP=hello check
```

Each app produces `build/<name>/<name>.app`. CMake objects and dependencies live
in `build/<name>/qt6`, so apps cannot overwrite one another's objects. QML is
compiled into the ELF resource; the device supplies Qt and PocketBook controls.
No Qt runtime or firmware files are installed with an app.

The `qt6` image supports the host architecture, including Apple Silicon. It
pins the upstream builder image digest, CMake helper commit and SDK commit in
[Dockerfile.qt6](Dockerfile.qt6). Only the necessary SDK headers/link libraries
are fetched. The older `sdk` service is retained for firmware/probe inspection;
it is **not** the build environment for the migrated apps.

`make check` verifies ARM softfp, the loader, Qt/InkView linkage, no rpath or zstd
resource dependency, and symbol-version ceilings for the inspected firmware.
These checks do not replace launching the app on the reader.

## Run

For Hello, copy `build/hello/hello.app` to the reader's `applications` directory,
safely eject, disconnect USB and launch Hello from Applications.

For Readest Sync, build a two-file package:

```sh
python3 apps/readest-sync/tools/package_app.py
```

Use its documented backup-and-verify installer with the observed hash of the
installed app. The installer preserves sessions, app state, EPUBs and native
reading databases. See [Readest Sync's instructions](apps/readest-sync/README.md).

## Add an app

```sh
cp -R apps/hello apps/my-app
docker compose run --rm qt6 make APP=my-app check
```

Hello's CMake file derives the target name from its directory. Edit
`src/main.cpp` and `qml/Main.qml`; list additional sources/resources in
`CMakeLists.txt` and the QRC file. The root Makefile discovers `apps/*/Makefile`.
Names may contain letters, digits, hyphens and underscores, without spaces.

```text
apps/<name>/
  Makefile                 # Includes mk/qt6-app.mk
  CMakeLists.txt           # Uses pocketbook_add_app
  src/                     # C++ controller/device integration
  qml/                     # Qt Quick screens and compiled QRC
mk/pocketbook-qt6.cmake     # Loads the pinned upstream SDK
mk/qt6-app.mk               # Shared per-app build/check commands
```

Keep InkView calls in a small device adapter; its global macros collide with
Qt headers. Initialize InkView with `TASK_MAKEACTIVE` before QGuiApplication,
select Qt's software renderer, add `/ebrmain/qml`, and use explicit screen
geometry. Hello demonstrates this startup sequence.

```sh
docker compose run --rm qt6 make list
docker compose run --rm qt6 make APP=hello clean     # Only this app's outputs
docker compose run --rm qt6 sh tests/monorepo.sh     # Isolated build checks
```

## Qt UI checks and previews

For the shared PocketBook simulator in your browser:

```sh
docker compose up -d --build simulator
```

Open <http://localhost:6080/vnc.html?autoconnect=true&resize=scale> after the
initial build. Choose **Sign in to demo account**, then **Refresh library**.
Select **Mock cloud** for local fixtures or **Real Readest** to use your account.
Run another integrated app with `SIMULATOR_APP=hello docker compose up -d simulator`.
The shared tooling runs desktop app builds without firmware. See the [simulator guide](tools/pocketbook-simulator/README.md) for
controls, conflict scenarios, persistent state and automated checks.

```sh
docker compose run --rm qt6 sh apps/readest-sync/tests/test_qt.sh
```

This builds a native desktop test executable, runs the real controller/QML
against temporary local fixtures, and renders portrait/landscape previews under
`build/readest-sync/qt6-preview-*.png`. A small **test-only** PocketBook controls
shim replaces the ARM firmware plugin on the desktop. Its appearance and input
method are approximations; device keyboard, e-ink updates, reader handoff and
live networking must be checked on InkPad.

The core tests and optional firmware-export comparison are documented in the
[app README](apps/readest-sync/README.md). Readest uses dynamically linked OpenSSL 3 for book hashes and the firmware
libcurl for HTTPS; no crypto or Qt runtime is installed on the reader.

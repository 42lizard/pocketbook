# Shared PocketBook simulator

Monorepo development tooling for running native desktop builds of PocketBook
Qt Quick apps without device firmware. The shared runtime provides device-sized
windows, portrait/landscape rotation, hardware button events, simulated Wi-Fi
callbacks, screenshots and desktop replacements for the PocketBook QML controls
used by this repository. It runs C++/QML app code, not ARM `.app` binaries.

The runtime has no dependency on Readest Sync. App-specific cloud and reader
fixtures live under `apps/<app>` inside this tooling directory. Both Hello and
Readest Sync are integrated.

## Launch an app

From the repository root, with Docker running:

```sh
docker compose up -d --build simulator                         # Readest Sync
SIMULATOR_APP=hello docker compose up -d --build simulator      # Hello
SIMULATOR_APP=readest-sync docker compose up -d simulator       # Switch back
```

The first run installs dependencies and compiles the selected app. Once
`docker compose logs -f simulator` reports the URL, open:

<http://localhost:6080/vnc.html?autoconnect=true&resize=scale>

The service publishes only port 6080 on localhost. Qt runs inside Docker;
noVNC sends its display and input through the browser. The 1800 × 1800 virtual
screen accommodates both app orientations; unused space is black. Browser
scaling is adjustable in the noVNC settings.

The scrollable **Simulator** panel contains shared device controls plus any
app-specific controls. **Back** follows the app's behavior, including exiting
from its root screen. Restart an exited app with:

```sh
docker compose restart simulator
docker compose stop simulator
```

If Vimium intercepts letters or navigation keys, press **i** before typing to
enter its insert mode. Escape leaves that mode. The simulator's inputs are
pixels in the remote display, not browser HTML inputs.

## Readest Sync cloud modes

Open **Simulator → Cloud service** and choose:

- **Mock cloud**: local demo account, 51 generated books, configurable HTTP and
  download failures, and simulated reading-position conflicts. Requests never
  fall back to the network. Use **Sign in to demo account**, then **Refresh library**.
- **Real Readest**: the app's existing HTTPS client connects to Readest. Hide the
  simulator panel, enter your real credentials in the app, and refresh your
  library. Covers and EPUB downloads use the real service with certificate
  verification and normal session persistence.

Switching restarts the app; reconnect/reload the browser if noVNC disconnects.
The selected mode persists across restarts. Switching is disabled during an
operation or while the simulated reader is open. Each mode has separate
sessions, app databases, downloads, covers and native reading databases.
Passwords are not persisted by the app.

The fixture chapter editor and remote-position buttons exist only in mock mode.
Real-mode reader handoff shows the downloaded EPUB's path; it does not render
that EPUB or manufacture native reading positions. Login, library, covers and
downloads can be tested against the live account. Actual reading and passage
interoperability still need a device or an eventual desktop reader integration.

See [Readest scenarios](apps/readest-sync/README.md) for mock sync and recovery tests.

## Storage and screenshots

The Compose volume `simulator-data` stores data by app under
`/simulator-data/<app>`. Readest's real profile is in its `real-cloud` subdirectory;
its mock profile is at the app's root. The first simulator's existing Readest
profile at `/simulator-data` remains supported automatically, without copying or
mixing its sessions. Other apps always have their own directory.

Screenshots are saved at the app's storage root. The control panel reports the
filename. To copy the volume contents into the ignored build directory:

```sh
docker compose cp simulator:/simulator-data ./build/simulator-export
```

For a fresh independent session, stop the current service and use another
Compose project name, which creates a separate volume:

```sh
docker compose stop simulator
docker compose -p pocketbook-sim-fresh up -d --build simulator
```

Direct desktop launches default to `/tmp/pocketbook-simulator-<pid>`. Set
`POCKETBOOK_SIM_ROOT` to an absolute, dedicated scratch directory to reuse data.
Nonempty directories without a simulator marker are refused. For Readest,
`POCKETBOOK_SIM_CLOUD=mock|real` chooses the startup mode for direct launches;
`READEST_SIM_CA` can specify a trust bundle when the host uses a different path.

## Add another Qt Quick app

Follow `apps/hello`, the smallest working integration. In the app's CMake file,
branch before including the ARM SDK:

```cmake
option(POCKETBOOK_SIMULATOR "Build with the shared simulator" OFF)
if(POCKETBOOK_SIMULATOR)
  include(../../mk/pocketbook-simulator.cmake)
  pocketbook_add_simulator_app(my-app SOURCES src/main.cpp QRC qml/app.qrc)
  return()
endif()
# Normal device build follows.
```

In desktop startup, skip InkView and the `pocketbook2` platform plugin, add
`POCKETBOOK_SIM_CONTROLS` to the QML import path, and set the same screen context
properties as the device app. Create a `pocketbook::Device`, call
`prepareStorage()`, load the app's root `Window`, then call
`device.attachWindow(engine)`. The default overlay is completely app-independent.
Keep the device object alive until the QML engine is destroyed.

Use `device.connectNetwork(callback)` from the app's desktop platform adapter
when Wi-Fi simulation is needed. Hardware buttons send Qt Page Up, Page Down and
Back events by default; `buttonHandler` can connect an app's controller directly.
Custom simulation panels can extend `DeviceOverlay.qml` using its `appControls`
and `appScreen` components; Readest's adapter demonstrates this without putting
cloud logic into the shared device runtime.

Then launch with `SIMULATOR_APP=my-app docker compose up -d simulator`. The
launcher validates the app name, configures `-DPOCKETBOOK_SIMULATOR=ON`, and builds
under `build/<app>/simulator`. Apps must opt into the desktop target; merely
copying an ARM binary into the repository is insufficient. The control shim
covers the types currently used by Hello and Readest; add other controls as
future apps need them.

## Checks

With the simulator service running:

```sh
docker compose exec simulator sh -c 'cmake --build build/readest-sync/simulator -j2 && READEST_UI_PREVIEW=/workspace/build/readest-sync/preview ctest --test-dir build/readest-sync/simulator --output-on-failure'
docker compose exec simulator sh -c 'cmake -S apps/hello -B build/hello/simulator -DPOCKETBOOK_SIMULATOR=ON && cmake --build build/hello/simulator -j2 && ctest --test-dir build/hello/simulator --output-on-failure'
```

The Readest checks cover the mock end-to-end flows, real HTTPS routing against a
local TLS server, certificate rejection, credential forwarding, download routing,
mode/profile isolation, and QML rendering. They never use your Readest account.
Hello's smoke test exercises the shared window, rotation, rendering and Back event.

Firmware compatibility, the native keyboard, e-ink refresh, suspend/resume,
actual reader pagination and device performance remain on-device tests.

# Kobalt target runtime: InkPad Color 3

Inspected on 2026-10-13. Target: PocketBook InkPad Color 3, **PB743K3**,
user-reported firmware **U743k3.6.10.2854**. These findings supersede the
InkPad 4 runtime assumption in the initial feasibility study. No application
was run on the device during this investigation, and no device files were written.

## Result

The exact firmware ships **Qt 6.8.2**, including Core, Gui, Widgets and Network,
and the native **pocketbook2** Qt platform plugin. The existing Qt 6.8.2
PocketBook SDK is therefore a suitable starting point. There is no observed
Qt-major-version blocker requiring a Qt 5 rewrite or bundling a foreign Qt
runtime. This is static compatibility evidence; successful application launch,
touch, refresh, suspend and shutdown still require device testing.

## Primary source and artifact identity

The [official support page](https://pocketbook.ch/en-ch/support) loads the
[product API for model 2256](https://pocketbook.ch/front-api/api/en-ch/product?product_id=2256).
Its `6.10.2854` entry links to the
[official firmware archive](https://download.pocketbook-int.com/fw/Yitoa/743k3/user/20260105/sw_20260105_U743k3_6.10.2854_user.zip).
The archive contains `fwinfo.xml` with version `U743k3.6.10.2854` and
`SWUPDATE.BIN`; the binary header also identifies `PocketBook743k3`.

SHA-256 digests of inspected artifacts:

| Artifact | SHA-256 |
| --- | --- |
| Downloaded ZIP | `769fb0ed98446fb134f6baf177504e1ab2e9552b2944f2a4ec55fab059d305cf` |
| `SWUPDATE.BIN` | `0ca8343a41e2497e0d038ed8f105be2f4d7f60ce9747e8ada24b164c5d5fd444` |
| `libQt6Core.so.6.8.2` | `1f8d2a814dbf64857ad7fbd4232b254267fd33e7c0434d95e71a8bb730b87f69` |
| Main-root `libc.so.6` | `a2e901366e98b866ec3521e5ebff30cc89387212be2d1deeb68fc093048adde3` |
| `libstdc++.so.6.0.32` | `a7b28dc0bb197ae4ee79ec6c0ec6af596dc88e60ad97553ef5351b0fcff819ef` |
| `libinkview.so` | `dbb87acdfeb18e246adb2abc265bb04786e2bb3eacd30c43f617a39987f69d7c` |
| `libqpocketbook2.so` | `e4e69c99932f7a62e7cfc11c502dc2b5177181eb71978cacfe6747306bf0a795` |

## Shipped runtime observations

Evidence comes from the downloaded firmware, inspected with 7-Zip 26.04,
GNU ARM `readelf`/`objdump` in the existing build container, and `strings`.
These are shell-tool observations, not REA Evidence IDs. REA's firmware tool
reported `capability_unavailable`: its Binwalk integration requires Linux and
an explicitly configured Binwalk executable, unavailable in the connected
macOS REA service. No REA setup changes were necessary for this bounded inspection.

| Question | Observation |
| --- | --- |
| Qt generation/version | Real Core binary contains `Qt 6.8.2 (arm-little_endian-ilp32-eabi shared (dynamic) release build; by Clang 15.0.3)`. |
| Qt modules needed for standalone Widgets host | Real Core, Gui, Widgets and Network libraries are present, all version 6.8.2. |
| Qt Sensors | No Qt Sensors library appears in the main root, boot cramfs, ebrmain or nested runtime inventories. Make sensor support optional; do not assume the SDK omission alone establishes runtime absence. |
| ARM ABI | Qt Core ELF header: ELF32, little endian, ARM, EABI5, **soft-float ABI** (`0x5000200`). Attributes: ARMv7 application profile, Thumb-2, VFPv3, NEONv1. This is incompatible with an ARM hard-float calling convention. |
| Loader | Main root supplies `/lib/ld-linux.so.3`; Qt Core lists `ld-linux.so.3` as a dependency. |
| C runtime | Main-root `libc.so.6` identifies GNU C Library / Buildroot 2.39; highest numeric exported version definition is `GLIBC_2.39`. |
| C++ runtime | Firmware `libstdc++.so.6.0.32` exports through `GLIBCXX_3.4.32` and `CXXABI_1.3.14`; also includes ARM-specific ABI tags. |
| Native Qt platform | `plugins/platforms/libqpocketbook2.so`, plugin key `pocketbook2`; links Qt6 Gui/Core, InkView, `libhwconfig.so` and `libcommon_utilities.so`. |
| Refresh connection | The platform plugin imports InkView `FullUpdate`, `FullUpdateHQ`, `PartialUpdate`, `PartialUpdateHQ`, `PartialUpdateBW`, `PartialUpdateBlack` and `PartialUpdateDU4`. This establishes an existing firmware refresh integration, not its behavior for every application repaint. |

Version ceilings are an initial filter, not a substitute for comparing the final
executable's undefined symbols, exact symbol versions and direct dependencies
against the extracted firmware exports. Qt private API compatibility is not
established merely by the public Qt version.

The native platform plugin should own the hardware presentation path initially.
For an eventual direct framebuffer implementation, the
[KOReader PB743K3 adapter](https://github.com/koreader/koreader/blob/master/frontend/device/pocketbook/device.lua)
is useful independent implementation evidence: it sets RGB24 despite a reported
8-bit framebuffer, forced landscape-counterclockwise orientation, and viewport
`x=3, y=2, w=1398, h=1864`. These values were not independently measured on this
device and should not be copied into a native Qt host without a demonstrated need.

## InkView exports

All requested names exist as global defined function symbols in this firmware's
`libinkview.so`:

| Symbol | ELF virtual address | Static observation |
| --- | --- | --- |
| `GetBatteryPower` | `0x75afc` | Tail branch to `hw_power@plt`. |
| `IsCharging` | `0x75ad0` | Tail branch to `hw_ischarging@plt`. |
| `GetFrontlightState` | `0x79abc` | Defined, 96 bytes. |
| `SetFrontlightState` | `0x79b78` | Defined, 88 bytes. |
| `OpenKeyboard` | `0x80b50` | Defined, 4536 bytes; `OpenKeyboardEx` also exists. |
| `FullUpdate` | `0x553d4` | Defined, 60 bytes; `FullUpdateHQ` also exists. |
| `BanSleep` | `0x75d18` | Tail branch to `hw_ban_suspend@plt`, not an empty stub. |
| `iv_sleepmode` | `0x783b8` | Defined, 20 bytes. |

These are linked virtual addresses, not process runtime addresses. Symbol presence
does not verify SDK header declarations, function signatures, argument semantics
or power-management behavior. Use the corresponding API declaration and test the
chosen lifecycle on the actual device.

## Retained local evidence and extraction

Artifacts are ignored build outputs under `build/kobalt/firmware-color3/`:

| Path relative to that directory | Contents |
| --- | --- |
| `fwinfo.xml`, `SWUPDATE.BIN` | Original ZIP members. |
| `rootfs.raw` | Gzip member at byte **15336448** in SWUPDATE, decompressed to a 32 MiB ext2 image. |
| `ebrmain.raw` | Gzip member at byte **20579328**, decompressed to a 517820416-byte ext2 image. |
| `cramfs.img` | Boot cramfs at byte **8193024**, length 6156288; separate from the main root. |
| `ebrmain/cramfs.img` | Member extracted from ebrmain; despite its filename this is **SquashFS 4, LZO**, not cramfs. Contains the real Qt/InkView libraries. |
| `rootfs/lib/libc.so.6`, `rootfs/lib/ld-linux.so.3` | Main-root C runtime and loader for final-binary checks. |
| `runtime/lib/libQt6Core.so.6.8.2` | Real Qt Core library. |
| `runtime/lib/libQt6Gui.so.6.8.2` | Real Qt Gui library. |
| `runtime/lib/libQt6Widgets.so.6.8.2` | Real Qt Widgets library. |
| `runtime/lib/libQt6Network.so.6.8.2` | Real Qt Network library. |
| `runtime/lib/libstdc++.so.6.0.32`, `runtime/lib/libinkview.so` | Real C++ runtime and InkView library. |
| `ebrmain/plugins/platforms/libqpocketbook2.so` | Native QPA plugin. |
| `files.txt`, `rootfs-files.txt`, `cramfs-files.txt`, `runtime-files.txt` | `7zz l` inventories of each respective filesystem. |

The ebrmain `lib/libQt6*.so.6.8.2` entries are symlinks to
`../cramfs/lib/`. Merely extracting those links does not extract the actual
libraries. The runtime paths above refer to real files extracted from the nested
SquashFS image. 7-Zip declined unsafe/out-of-tree links during the preliminary
link inventory extraction; the required real files were subsequently extracted
directly without following those links.

Recheck a selected library using the existing container, for example:

```sh
docker compose run --rm qt6 arm-linux-gnueabi-readelf -h -A -d \
  /workspace/build/kobalt/firmware-color3/runtime/lib/libQt6Core.so.6.8.2
docker compose run --rm qt6 arm-linux-gnueabi-readelf --version-info \
  /workspace/build/kobalt/firmware-color3/rootfs/lib/libc.so.6
docker compose run --rm qt6 arm-linux-gnueabi-readelf --wide --dyn-syms \
  /workspace/build/kobalt/firmware-color3/runtime/lib/libinkview.so
```

## Remaining validation

- Compare the completed port's imports and packaged dependency set to these
  exact libraries; a successful initial publication probe is not a full-reader check.
- Device launch, EPUB opening, touch and page keys, local position persistence,
  menus, color refresh, Home/exit, USB handling and suspend/wake remain runtime
  acceptance work.
- Native library registration and Readest synchronization are separate from
  the initial local-only reader port.

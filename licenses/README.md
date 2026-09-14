# Licensing and third-party notices

The [root MIT license](../LICENSE) covers original code and documentation in
`apps/hello`, `apps/readest-sync`, and the shared build, test, and simulator
tooling. Third-party files retain their original licenses. Experimental files
outside the tracked project are not included in this licensing declaration.

## Code included in the application

| Component | Use | License and notice |
| --- | --- | --- |
| miniz 3.1.2 | Vendored EPUB ZIP reader, compiled into readest-sync | [Original MIT notice](../apps/readest-sync/src/vendor/miniz/LICENSE); additional notices remain in the source files |
| json-c | Statically linked into the device readest-sync executable | [MIT and package notices](libjson-c-dev-copyright.txt) |
| SQLite | Statically linked into the device readest-sync executable | [Public-domain dedication and package notices](libsqlite3-dev-copyright.txt) |
| Mozilla CA bundle, converted by curl | Shipped as `system/readest-sync/ca-certificates.crt` | [MPL 2.0](MPL-2.0.txt); provenance and source in [the asset README](../apps/readest-sync/assets/README.md) |

The SQLite package notice also covers packaging under separate terms. Those
packaging terms do not change the public-domain status of SQLite itself.

## Libraries and SDK used externally

| Component | Use | Notice |
| --- | --- | --- |
| Qt Core, Gui, Qml, Quick | Dynamically linked device UI; desktop tests and simulator | [Qt Base notices](qt6-base-dev-copyright.txt), [Qt Declarative notices](qt6-declarative-dev-copyright.txt), [LGPLv3](LGPL-3.txt) and its incorporated [GPLv3 terms](GPL-3.txt) |
| libcurl | Firmware HTTPS library; desktop HTTPS client | [curl notices](libcurl4-openssl-dev-copyright.txt) |
| libxml2 | Dynamically linked EPUB XML parser | [libxml2 notices](libxml2-dev-copyright.txt) |
| OpenSSL 3 libcrypto | Dynamically linked hashing support | [OpenSSL notices](libssl-dev-copyright.txt), [Apache License 2.0](Apache-2.0.txt) |
| fstanis/pocketbook-sdk-qt6 | External CMake helpers and build SDK integration | [MIT license](pocketbook-sdk-qt6-MIT.txt) |
| PocketBook InkView, hardware libraries and QML controls | SDK interfaces and existing firmware runtime | Governed by PocketBook's respective terms; not relicensed by this repository |

Device installers deploy the application, not copies of the Qt, PocketBook,
libcurl, libxml2, or OpenSSL shared libraries. The simulator controls under
`tools/pocketbook-simulator/qt-controls` are this project's test approximations,
not copies of the firmware controls.

## Notice provenance and scope

The `*-copyright.txt` files are unmodified Debian package notices retrieved from
the project's existing Qt build image on 2026-09-14. Standard license texts are
copied from that image's `/usr/share/common-licenses`; references to that directory
in the notices correspond to the same named `.txt` files here. These full package
notices include examples, tests, tools, and packaging that are not necessarily
linked into our applications. Their GPL and other license alternatives do not
declare our original code to be GPL-licensed.

The SDK helper MIT notice was copied from its local upstream checkout. The build
pins that project at `754e436c7c24b4e2695154e2efffbf7447b4130e`; its license does
not cover all components downloaded by the SDK.

`MPL-2.0.txt` is the unmodified license text from Mozilla, retrieved on 2026-09-14
from <https://www.mozilla.org/media/MPL/2.0/index.f75d2927d3c1.txt>.

These notices describe the direct dependencies above. They are not an inventory
of every package in the Docker images or the PocketBook firmware. Firmware and
desktop library versions may differ from the build image; redistributed runtime
libraries or container images need notices and any required source corresponding
to the actual versions distributed.

When distributing applications, include the root license, applicable dependency
notices, and the miniz notice. LGPL obligations for Qt remain separate from the
MIT license for our code, including applicable notices and the ability to use
modified libraries. License texts alone do not replace source, relinking, or
installation-information obligations where those apply. PocketBook SDK/runtime
redistribution permissions have not been established by this notice collection.

Upstream references:

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [PocketBook Qt6 SDK helpers](https://github.com/fstanis/pocketbook-sdk-qt6)
- [PocketBook SDK](https://github.com/pocketbook/SDK_6.3.0)
- [miniz 3.1.2](https://github.com/richgel999/miniz/releases/tag/3.1.2)

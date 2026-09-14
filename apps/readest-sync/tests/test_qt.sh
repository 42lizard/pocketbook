#!/bin/sh
# Run inside: docker compose run --rm qt6 sh apps/readest-sync/tests/test_qt.sh
set -eu
unset CMAKE_TOOLCHAIN_FILE
cmake -S apps/readest-sync -B build/readest-sync/desktop -DREADEST_DESKTOP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/readest-sync/desktop -j2
QT_QPA_PLATFORM=offscreen READEST_UI_PREVIEW="$PWD/build/readest-sync/qt6-preview" \
    ctest --test-dir build/readest-sync/desktop --output-on-failure

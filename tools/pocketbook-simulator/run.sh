#!/bin/sh
set -eu
unset CMAKE_TOOLCHAIN_FILE
app=${SIMULATOR_APP:-readest-sync}
case "$app" in ''|*[!a-zA-Z0-9_-]*) echo "Invalid SIMULATOR_APP" >&2; exit 1;; esac
if [ ! -f "apps/$app/CMakeLists.txt" ]; then echo "Unknown app: $app" >&2; exit 1; fi
cc=$(command -v "${CC:-cc}")
cxx=$(command -v "${CXX:-c++}")
cache="build/$app/simulator/CMakeCache.txt"
# CMake's automatic compiler change clears other options, including simulator
# mode. Start a fresh configuration only for a compiler change, preserving the
# normal incremental build and the separately stored simulator data.
set --
if [ -f "$cache" ]; then
    cached_cc=$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$cache")
    cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$cache")
    if [ "$cached_cc" != "$cc" ] || [ "$cached_cxx" != "$cxx" ]; then set -- --fresh; fi
fi
cmake "$@" -S "apps/$app" -B "build/$app/simulator" -DPOCKETBOOK_SIMULATOR=ON -DCMAKE_BUILD_TYPE=Debug \
    "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cxx"
cmake --build "build/$app/simulator" -j2
export POCKETBOOK_SIM_ROOT="/simulator-data/$app"
# Preserve the first simulator's existing Readest data without copying sessions.
if [ "$app" = readest-sync ] && [ -f /simulator-data/.readest-simulator ]; then
    export POCKETBOOK_SIM_ROOT=/simulator-data
fi
if [ -f "$POCKETBOOK_SIM_ROOT/cloud-mode" ]; then
    export POCKETBOOK_SIM_CLOUD="$(cat "$POCKETBOOK_SIM_ROOT/cloud-mode")"
else
    export POCKETBOOK_SIM_CLOUD=mock
fi
export LANG=C.UTF-8
export QT_QUICK_BACKEND=software
export QT_QPA_PLATFORM=vnc:size=1800x1800:port=5900
export QT_VNC_NO_PASSWORD=1
websockify --web /usr/share/novnc 6080 localhost:5900 &
bridge=$!
trap 'kill "$bridge" 2>/dev/null || true' EXIT INT TERM
echo 'Simulator: http://localhost:6080/vnc.html?autoconnect=true&resize=scale'
"build/$app/simulator/$app"

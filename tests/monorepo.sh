#!/bin/sh
# Run inside: docker compose run --rm qt6 sh tests/monorepo.sh
set -eu
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$repo/Makefile" "$work/"
cp -R "$repo/mk" "$repo/tools" "$work/"
mkdir "$work/apps"
cp -R "$repo/apps/hello" "$work/apps/hello"
cp -R "$repo/apps/hello" "$work/apps/second"
cd "$work"
make clean
make APP=hello clean
make APP=second
make clean
test ! -f build/second/second.app
make -j2
make check
test -f build/hello/qt6/CMakeFiles/hello.dir/src/main.cpp.o
test -f build/second/qt6/CMakeFiles/second.dir/src/main.cpp.o
sha256sum build/second/second.app > second.sha256
make APP=hello clean
test ! -f build/hello/hello.app
sha256sum -c second.sha256
make APP=hello
sha256sum -c second.sha256
printf '#include "test.h"\n' >> apps/hello/src/main.cpp
printf '#define TEST_VALUE 1\n' > apps/hello/src/test.h
make APP=hello
sleep 1
touch apps/hello/src/test.h
make APP=hello
test build/hello/qt6/CMakeFiles/hello.dir/src/main.cpp.o -nt apps/hello/src/test.h
sha256sum -c second.sha256
if make APP=missing clean; then exit 1; fi
make clean
test ! -f build/hello/hello.app
test ! -f build/second/second.app
echo 'Qt6 monorepo isolation, header dependency and selective-clean checks passed.'

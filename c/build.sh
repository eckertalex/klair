#!/bin/sh
# usage: ./build.sh [debug|release]
# CC picks the compiler, clang by default
set -eu

cd "$(dirname "$0")"

flags="-std=c23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wundef -Wno-unused-function"
case "${1:-debug}" in
    debug) flags="$flags -O0 -g -DBUILD_DEBUG=1" ;;
    release) flags="$flags -O2 -g -DBUILD_DEBUG=0" ;;
    *)
        echo "usage: $0 [debug|release]" >&2
        exit 2
        ;;
esac

# Outside a git checkout the version string has no hash
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    flags="$flags -DBUILD_GIT_HASH=\"$(git describe --always --dirty --exclude '*')\""
fi

# Absolute paths, so errors resolve from the repo root too
mkdir -p build
${CC:-clang} $flags -I"$PWD/src" "$PWD/src/klair/klair_main.c" -o build/klair

#!/bin/sh
# usage: ./build.sh [debug|release|test]
set -eu

cd "$(dirname "$0")"

flags="-vet"
case "${1:-debug}" in
    debug) flags="$flags -debug -o:none" ;;
    release) flags="$flags -o:speed" ;;
    test) exec odin test src -vet ;;
    *)
        echo "usage: $0 [debug|release|test]" >&2
        exit 2
        ;;
esac

# Outside a git checkout the version string has no hash
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    flags="$flags -define:BUILD_GIT_HASH=$(git describe --always --dirty --exclude '*')"
fi

mkdir -p build
odin build src -out:build/klair $flags

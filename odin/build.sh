#!/bin/sh
set -eu

cd "$(dirname "$0")"

flags="-strict-style -vet"
case "${1:-debug}" in
    debug) flags="$flags -debug -o:none" ;;
    release) flags="$flags -o:speed" ;;
    *)
        echo "usage: $0 [debug|release]" >&2
        exit 2
        ;;
esac

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    flags="$flags -define:BUILD_GIT_HASH=$(git describe --always --dirty --exclude '*')"
fi

mkdir -p build
odin build src -out:build/klair $flags

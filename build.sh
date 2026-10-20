#!/bin/sh
set -eu

cd "$(dirname "$0")"

CC="${CC:-cc}"
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo v0.0.0)"
FLAGS="$(cat compile_flags.txt)"

$CC $FLAGS -O2 -g -DKLAIR_VERSION="\"$VERSION\"" -o klair klair.c

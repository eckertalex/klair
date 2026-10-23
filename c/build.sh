#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Unpack Arguments --------------------------------------------------------
mode="debug"
compiler="clang"
action="build"
separator=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        debug|release)
            mode="$1"
            ;;
        clang|gcc|cc)
            compiler="$1"
            ;;
        build|run)
            action="$1"
            ;;
        --)
            separator=1
            shift
            break
            ;;
        *)
            echo "usage: $0 [debug|release] [clang|gcc|cc] [build|run] [-- args...]" >&2
            exit 1
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 1
fi

# --- Compile/Link Definitions -----------------------------------------------
FLAGS="$(cat compile_flags.txt)"

# --- Choose Mode -------------------------------------------------------------
echo "[$compiler compile]"
case "$mode" in
    debug)
        echo "[debug mode]"
        MODE_FLAGS="-O0 -g -DBUILD_DEBUG=1"
        ;;
    release)
        echo "[release mode]"
        MODE_FLAGS="-O2 -g -DBUILD_DEBUG=0"
        ;;
esac

# --- Build Metadata ----------------------------------------------------------
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo v0.0.0)"

VERSION_FLAG="-DKLAIR_VERSION=\"$VERSION\""

# --- Build -------------------------------------------------------------------
mkdir -p build
$compiler $FLAGS $MODE_FLAGS $VERSION_FLAG klair.c -o build/klair

# --- Run ---------------------------------------------------------------------
if [ "$action" = "run" ]; then
    exec ./build/klair "$@"
fi

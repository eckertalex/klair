#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Unpack Arguments --------------------------------------------------------
mode="debug"
action="build"

while [ "$#" -gt 0 ]; do
    case "$1" in
        debug|release)
            mode="$1"
            ;;
        build|run|test)
            action="$1"
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "usage: $0 [debug|release] [build|run|test] [-- args...]" >&2
            exit 1
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 1
fi

# --- Choose Mode -------------------------------------------------------------
case "$mode" in
    debug)
        echo "[debug mode]"
        MODE_FLAGS="-debug -o:none"
        ;;
    release)
        echo "[release mode]"
        MODE_FLAGS="-o:speed"
        ;;
esac

# --- Compiler Flags ----------------------------------------------------------
ODIN_FLAGS="-vet"

# --- Build Metadata ----------------------------------------------------------
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo v0.0.0)"

VERSION_FLAG="-define:KLAIR_VERSION=$VERSION"

# --- Execute -----------------------------------------------------------------
case "$action" in
    test)
        exec odin test src -vet
        ;;
    run)
        exec odin run src $MODE_FLAGS $ODIN_FLAGS "$VERSION_FLAG" -- "$@"
        ;;
    *)
        mkdir -p build
        exec odin build src -out:build/klair $MODE_FLAGS $ODIN_FLAGS "$VERSION_FLAG"
        ;;
esac

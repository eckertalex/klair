#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Arguments ---------------------------------------------------------------
mode="debug"
action="build"

while [ "$#" -gt 0 ]; do
    case "$1" in
        debug|release) mode="$1" ;;
        build|run|test) action="$1" ;;
        --)
            shift
            break
            ;;
        *)
            echo "usage: $0 [debug|release] [build|run|test] [-- args...]" >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 2
fi

# --- Test --------------------------------------------------------------------
if [ "$action" = "test" ]; then
    exec odin test src -vet
fi

# --- Git Commit ID -----------------------------------------------------------
git_hash=""
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git_hash=$(git describe --always --dirty --exclude '*')
fi

# --- Flags -------------------------------------------------------------------
flags_common="-vet -define:BUILD_GIT_HASH=$git_hash"
flags_debug="-debug -o:none"
flags_release="-o:speed"

# --- Choose Flags ------------------------------------------------------------
if   [ "$mode" = "debug" ];   then echo "[debug mode]";   flags="$flags_common $flags_debug"
elif [ "$mode" = "release" ]; then echo "[release mode]"; flags="$flags_common $flags_release"
fi

# --- Build/Run ---------------------------------------------------------------
case "$action" in
    run)
        exec odin run src $flags -- "$@"
        ;;
    build)
        mkdir -p build
        exec odin build src -out:build/klair $flags
        ;;
esac

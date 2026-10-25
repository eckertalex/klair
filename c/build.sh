#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Arguments ---------------------------------------------------------------
mode="debug"
compiler="clang"
action="build"

while [ "$#" -gt 0 ]; do
    case "$1" in
        debug|release) mode="$1" ;;
        clang|gcc|cc) compiler="$1" ;;
        build|run) action="$1" ;;
        --)
            shift
            break
            ;;
        *)
            echo "usage: $0 [debug|release] [clang|gcc|cc] [build|run] [-- args...]" >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 2
fi

# --- Git Commit ID -----------------------------------------------------------
git_hash=""
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git_hash=$(git describe --always --dirty --exclude '*')
fi

# --- Flags -------------------------------------------------------------------
cc_cflags_gcc=""
cc_cflags_clang=""
cc_common="-std=c23 -Isrc -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wundef -Wno-unused-function -DBUILD_GIT_HASH=\"$git_hash\""
cc_debug="-O0 -g -DBUILD_DEBUG=1 $cc_common"
cc_release="-O2 -g -DBUILD_DEBUG=0 $cc_common"

# --- Choose Flags ------------------------------------------------------------
if   [ "$compiler" = "gcc" ];   then echo "[gcc compile]";   cc_compiler="${CC:-gcc} $cc_cflags_gcc"
elif [ "$compiler" = "clang" ]; then echo "[clang compile]"; cc_compiler="${CC:-clang} $cc_cflags_clang"
elif [ "$compiler" = "cc" ];    then echo "[cc compile]";    cc_compiler="${CC:-cc}"
fi
if   [ "$mode" = "debug" ];   then echo "[debug mode]";   compile="$cc_compiler $cc_debug"
elif [ "$mode" = "release" ]; then echo "[release mode]"; compile="$cc_compiler $cc_release"
fi

# --- Build -------------------------------------------------------------------
mkdir -p build
$compile src/klair/klair_main.c -o build/klair

# --- Run ---------------------------------------------------------------------
if [ "$action" = "run" ]; then
    exec ./build/klair "$@"
fi


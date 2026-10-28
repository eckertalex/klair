#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Arguments ---------------------------------------------------------------
impls=""
mode="debug"

while [ "$#" -gt 0 ]; do
    case "$1" in
        c|odin|go) impls="$impls $1" ;;
        debug|release) mode="$1" ;;
        *) break ;;
    esac
    shift
done

impls="${impls:-c go odin}"

# --- Build -------------------------------------------------------------------
# Only stdout is dropped, so warnings and errors still show
conformance/build.sh build >/dev/null
binaries=""
for impl in $impls; do
    "$impl"/build.sh "$mode" build >/dev/null
    binaries="$binaries $impl/build/klair"
done

# --- Check -------------------------------------------------------------------
# The rest of the arguments go to the checker, e.g. -q or -run TEXT
exec conformance/build/conformance check "$@" $binaries

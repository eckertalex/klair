#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Arguments ---------------------------------------------------------------
impls=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        c|odin|go) impls="$impls $1" ;;
        *) break ;;
    esac
    shift
done

impls="${impls:-c go odin}"

# --- Build -------------------------------------------------------------------
status=0
for impl in $impls; do
    echo "=== $impl ==="
    ./"$impl"/build.sh "$@" || status=1
    echo
done

exit "$status"

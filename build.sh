#!/bin/sh
set -eu

cd "$(dirname "$0")"

# Forwards all arguments unchanged to each implementation's build.sh.
# Every implementation runs even if an earlier one fails.
status=0
for impl in c go odin; do
    echo "=== $impl ==="
    ./"$impl"/build.sh "$@" || status=1
    echo
done

exit "$status"

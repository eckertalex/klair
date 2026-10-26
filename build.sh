#!/bin/sh
set -eu

cd "$(dirname "$0")"

usage() {
    echo "usage: $0 [c] [odin] [go] [args for each build.sh...]" >&2
    echo "       $0 [c] [odin] [go] check [debug|release] [-q] [-run TEXT]" >&2
    exit 2
}

impls=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        c|odin|go) impls="$impls $1" ;;
        *) break ;;
    esac
    shift
done
impls="${impls:-c go odin}"

# Prints a build's output only if it fails, then stops: checking a stale
# binary would be misleading
build_quietly() {
    if ! output="$("$@" 2>&1)"; then
        printf '%s\n' "$output" >&2
        echo "build failed: $*" >&2
        exit 1
    fi
}

check() {
    mode="debug"
    flags=""
    while [ "$#" -gt 0 ]; do
        case "$1" in
            debug|release) mode="$1" ;;
            -q) flags="$flags -q" ;;
            -run)
                [ "$#" -ge 2 ] || usage
                flags="$flags -run $2"
                shift
                ;;
            *) usage ;;
        esac
        shift
    done

    build_quietly conformance/build.sh build
    binaries=""
    for impl in $impls; do
        build_quietly "$impl/build.sh" "$mode" build
        binaries="$binaries $impl/build/klair"
    done

    # Unquoted on purpose: one argument per word
    exec conformance/build/conformance check $flags $binaries
}

if [ "${1:-}" = "check" ]; then
    shift
    check "$@"
fi

status=0
for impl in $impls; do
    echo "=== $impl ==="
    ./"$impl"/build.sh "$@" || status=1
    echo
done

exit "$status"

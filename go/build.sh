#!/bin/sh
set -eu

cd "$(dirname "$0")"

# --- Arguments ---------------------------------------------------------------
mode="debug"
action="build"

while [ "$#" -gt 0 ]; do
    case "$1" in
        debug|release) mode="$1" ;;
        build|run|test|audit|tidy) action="$1" ;;
        --)
            shift
            break
            ;;
        *)
            echo "usage: $0 [debug|release] [build|run|test|audit|tidy] [-- args...]" >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 2
fi

# --- Actions -----------------------------------------------------------------
case "$action" in
    tidy)
        go mod tidy -v
        go fix ./...
        go fmt ./...
        exit 0
        ;;
    audit)
        go mod tidy -diff
        go mod verify
        test -z "$(gofmt -l .)"
        go vet ./...
        exit 0
        ;;
    test)
        exec go test -v -race ./...
        ;;
esac

# --- Git Commit ID -----------------------------------------------------------
git_hash=""
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git_hash=$(git describe --always --dirty --exclude '*')
fi

# --- Flags -------------------------------------------------------------------
build_debug=""
build_release="-trimpath"

linker_common="-X main.BUILD_GIT_HASH=$git_hash"
linker_debug="-X main.BUILD_DEBUG=true"
linker_release="-s -w -X main.BUILD_DEBUG=false"

# --- Choose Flags ------------------------------------------------------------
case "$mode" in
    debug)
        echo "[debug mode]"
        build_flags="$build_debug"
        linker_flags="$linker_common $linker_debug"
        ;;
    release)
        echo "[release mode]"
        build_flags="$build_release"
        linker_flags="$linker_common $linker_release"
        ;;
esac

# --- Build/Run ----------------------------------------------------------------
case "$action" in
    run)
        exec env CGO_ENABLED=0 go run $build_flags "-ldflags=$linker_flags" . "$@"
        ;;
    build)
        mkdir -p build
        exec env CGO_ENABLED=0 go build $build_flags "-ldflags=$linker_flags" -o build/klair .
        ;;
esac

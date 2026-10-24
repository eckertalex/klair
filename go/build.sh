#!/bin/sh
set -eu

cd "$(dirname "$0")"

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
            exit 1
            ;;
    esac
    shift
done

if [ "$action" != "run" ] && [ "$#" -gt 0 ]; then
    echo "error: program arguments require the 'run' action" >&2
    exit 1
fi

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo v0.0.0)"

case "$mode" in
    debug)
        echo "[debug mode]"
        DEBUG=true
        LINKER_FLAGS="-X main.DEBUG=$DEBUG -X main.VERSION=$VERSION"
        BUILD_FLAGS=""
        ;;
    release)
        echo "[release mode]"
        DEBUG=false
        LINKER_FLAGS="-X main.DEBUG=$DEBUG -X main.VERSION=$VERSION"
        BUILD_FLAGS="-s -w -trimpath"
        ;;
esac

case "$action" in
    tidy)
        go mod tidy -v
        go fix ./...
        go fmt ./...
        ;;
    test)
        exec go test -v -race -buildvcs ./...
        ;;
    run)
        exec go run $BUILD_FLAGS -ldflags="$LINKER_FLAGS" . "$@"
        ;;
    audit)
        exec go mod tidy -diff
        exec go mod verify
        test -z "$$(gofmt -l .)"
        exec go vet ./...
        ;;
    build)
        mkdir -p build
        exec env CGO_ENABLED=0 go build $BUILD_FLAGS -ldflags="$LINKER_FLAGS" -o=build/klair .
        ;;
esac

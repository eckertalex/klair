#!/bin/sh
# usage: ./build.sh [debug|release|test]
set -eu

cd "$(dirname "$0")"
# Lets editors resolve Go's relative paths
echo "build: Entering directory '$PWD'" >&2

flags=""
ldflags=""
case "${1:-debug}" in
    debug) ldflags="-X main.BUILD_DEBUG=true" ;;
    release) flags="-trimpath" ldflags="-s -w -X main.BUILD_DEBUG=false" ;;
    test) exec go test -v ./... ;;
    *)
        echo "usage: $0 [debug|release|test]" >&2
        exit 2
        ;;
esac

# Outside a git checkout the version string has no hash
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    ldflags="$ldflags -X main.BUILD_GIT_HASH=$(git describe --always --dirty --exclude '*')"
fi

go vet ./...
mkdir -p build
CGO_ENABLED=0 go build $flags -ldflags="$ldflags" -o build/klair .

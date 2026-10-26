#!/bin/sh
set -eu

cd "$(dirname "$0")"

case "${1:-build}" in
    build)
        mkdir -p build
        exec odin build src -vet -out:build/conformance
        ;;
    test)
        exec odin test src -vet
        ;;
    *)
        echo "usage: $0 [build|test]" >&2
        exit 2
        ;;
esac


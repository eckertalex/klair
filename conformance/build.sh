#!/bin/sh
set -eu

cd "$(dirname "$0")"

exec odin build src -vet -out:build/conformance

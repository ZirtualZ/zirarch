#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_DIR=$(mktemp -d)
trap 'rm -rf -- "$TEST_DIR"' EXIT HUP INT TERM

"${CC:-cc}" ${CFLAGS:--O2 -Wall -Wextra -Wpedantic} \
    -I"$ROOT/src" -std=c11 \
    "$ROOT/tests/applications_test.c" "$ROOT/src/applications.c" \
    -o "$TEST_DIR/applications-test"
"$TEST_DIR/applications-test" "$ROOT/tests/fixtures/terminal.desktop"
grep -q 'netsurf-gtk' "$ROOT/src/applications.c" ||
	{ echo "NetSurf browser recognition is missing." >&2; exit 1; }
grep -q 'about:blank' "$ROOT/src/applications.c" ||
	{ echo "NetSurf startup URL fallback is missing." >&2; exit 1; }
printf 'Application metadata checks passed.\n'

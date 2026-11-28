#!/bin/sh
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

if grep -Eq 'ManagedWindow[[:space:]]+clients\[[^]]+\]|PendingClose[[:space:]]+pending_closes\[[^]]+\]' \
	src/wm.c; then
	echo "WM fixed managed-client or pending-close storage still exists" >&2
	exit 1
fi

grep -q 'realloc(' src/wm.c ||
	{ echo "WM storage must grow with realloc" >&2; exit 1; }
grep -q 'ensure_client_capacity' src/wm.c ||
	{ echo "WM client growth helper is missing" >&2; exit 1; }
grep -q 'ensure_pending_close_capacity' src/wm.c ||
	{ echo "WM pending-close growth helper is missing" >&2; exit 1; }
grep -q 'clamp_managed_geometry' src/wm.c ||
	{ echo "WM geometry clamping helper is missing" >&2; exit 1; }
grep -q 'publish_workarea' src/wm.c ||
	{ echo "WM workarea publisher is missing" >&2; exit 1; }

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)" simple-wm
echo "Window manager storage checks passed."

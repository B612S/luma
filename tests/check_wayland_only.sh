#!/bin/sh
# Wayland-only policy check: the binary must exit with status 1 and explain
# that a Wayland session is required when no Wayland session is available.
#
# Run with the environment variables WAYLAND_DISPLAY and WAYLAND_SOCKET empty
# (see tests/CMakeLists.txt).
set -u

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/luma" >&2
    exit 2
fi

output=$("$1" 2>&1)
status=$?

printf '%s\n' "$output"

if [ "$status" -ne 1 ]; then
    echo "expected exit status 1, got $status" >&2
    exit 1
fi

printf '%s' "$output" | grep -q "requires a Wayland session" || {
    echo "missing the Wayland-only error message" >&2
    exit 1
}

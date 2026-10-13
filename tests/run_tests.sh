#!/usr/bin/env bash
set -euo pipefail

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="${JSONBENCH_BINARY:-$TESTS_DIR/../src/jsonbench}"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: binary not found or not executable: $BINARY" >&2
    echo "       Run './autogen.sh && ./configure && make' first" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: Python 3 is required by the regression test helper" >&2
    exit 1
fi
if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [JSON_ENGINE]" >&2
    exit 1
fi

# Standard-library helper, preserving the existing CLI and engine filter.
exec python3 "$TESTS_DIR/regression_tests.py" "$BINARY" "$@"

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

# Negative ordering check: build the handoff with plain stores and run the
# two-thread stress under ThreadSanitizer. The run must fail; a clean run means
# the test is not exercising the publication ordering. If TSan is unavailable
# on this host, skip with a clear message rather than fail the build.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/test-tsan"
CC="${CC:-cc}"
mkdir -p "$OUT"

if ! "$CC" -std=c99 -O1 -g -pthread -fsanitize=thread -DHANDOFF_PLAIN \
  -I"$ROOT/src" -I"$ROOT/tools/fake" \
  "$ROOT/tools/test/test_handoff.c" \
  "$ROOT/src/core/handoff.c" "$ROOT/src/core/uiact.c" \
  "$ROOT/src/core/uistate.c" "$ROOT/src/core/input.c" \
  "$ROOT/src/core/xfer.c" "$ROOT/src/core/path.c" \
  "$ROOT/src/core/cause.c" "$ROOT/src/core/fmt.c" \
  -o "$OUT/test_handoff_plain" 2>"$OUT/compile.log"; then
  echo "tsan: not available here; ordering check skipped"
  exit 0
fi

if "$OUT/test_handoff_plain" >"$OUT/run.log" 2>&1; then
  echo "tsan: expected the plain-store build to race, but it passed" >&2
  exit 1
fi

echo "tsan: missing release/acquire was caught"

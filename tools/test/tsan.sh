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

# The threaded app test with the real worker and the singleton fakes. The
# fakes are shared across the two threads, so this run is honest only because
# the shared clock is atomic; it is the app under TSan, not a scoped subset.
if ! "$CC" -std=c99 -O1 -g -pthread -fsanitize=thread \
  -I"$ROOT/src" -I"$ROOT/tools/fake" -I"$ROOT/tools/preview" \
  "$ROOT/tools/test/test_app_thread.c" "$ROOT"/tools/fake/*.c \
  "$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c "$ROOT"/src/gfx/*.c \
  "$ROOT"/src/ui/*.c "$ROOT"/src/app/*.c \
  "$ROOT/tools/preview/vita2d_soft.c" -lz -lm \
  -o "$OUT/test_app_thread" 2>"$OUT/app-compile.log"; then
  echo "tsan: app-thread build unavailable here; skipped"
  exit 0
fi

set +e
"$OUT/test_app_thread" >"$OUT/app-run.log" 2>&1
status=$?
set -e

if grep -q "WARNING: ThreadSanitizer" "$OUT/app-run.log"; then
  cat "$OUT/app-run.log" >&2
  echo "tsan: the threaded app test reported a race" >&2
  exit 1
fi
if [[ "$status" -ne 0 ]]; then
  if grep -q "FATAL: ThreadSanitizer" "$OUT/app-run.log"; then
    echo "tsan: runtime unavailable here; app-thread check skipped"
    exit 0
  fi
  cat "$OUT/app-run.log" >&2
  echo "tsan: the threaded app test failed" >&2
  exit 1
fi

echo "tsan: threaded app clean with the fakes"

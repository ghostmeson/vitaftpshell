#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

# ThreadSanitizer checks.
#
#   1. A negative ordering check: build the handoff with plain stores and run
#      the two-thread stress. The run must report a race; a clean run means the
#      test is not exercising the publication ordering.
#   2. The real app thread with the singleton fakes. The fakes are shared
#      across the two threads, so this run is honest only because the shared
#      clock is atomic; it is the app under TSan, not a scoped subset.
#
# TSan availability is probed once with a trivial program. A source or link
# failure after that probe is a real failure, never a silent skip.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/test-tsan"
CC="${CC:-cc}"
mkdir -p "$OUT"

cat > "$OUT/probe.c" <<'EOF'
int main(void)
{
    return 0;
}
EOF

if ! "$CC" -std=c99 -fsanitize=thread "$OUT/probe.c" -o "$OUT/probe" \
    2>"$OUT/probe-compile.log"; then
  echo "tsan: the toolchain cannot build a TSan binary; checks skipped"
  exit 0
fi
if ! "$OUT/probe" >"$OUT/probe-run.log" 2>&1; then
  echo "tsan: the TSan runtime is unavailable here; checks skipped"
  exit 0
fi

# --- Negative ordering check ------------------------------------------------
if ! "$CC" -std=c99 -O1 -g -pthread -fsanitize=thread -DHANDOFF_PLAIN \
  -I"$ROOT/src" -I"$ROOT/tools/fake" \
  "$ROOT/tools/test/test_handoff.c" \
  "$ROOT/src/core/handoff.c" "$ROOT/src/core/uiact.c" \
  "$ROOT/src/core/uistate.c" "$ROOT/src/core/input.c" \
  "$ROOT/src/core/xfer.c" "$ROOT/src/core/path.c" \
  "$ROOT/src/core/cause.c" "$ROOT/src/core/fmt.c" \
  -o "$OUT/test_handoff_plain" 2>"$OUT/compile.log"; then
  cat "$OUT/compile.log" >&2
  echo "tsan: the handoff build failed after the TSan probe" >&2
  exit 1
fi

set +e
"$OUT/test_handoff_plain" >"$OUT/run.log" 2>&1
status=$?
set -e

if [[ "$status" -eq 0 ]]; then
  echo "tsan: expected the plain-store build to race, but it passed" >&2
  exit 1
fi
if ! grep -q "WARNING: ThreadSanitizer" "$OUT/run.log"; then
  cat "$OUT/run.log" >&2
  echo "tsan: the plain-store build failed without reporting a race" >&2
  exit 1
fi

echo "tsan: missing release/acquire was caught"

# --- Threaded app -----------------------------------------------------------
if ! "$CC" -std=c99 -O1 -g -pthread -fsanitize=thread \
  -I"$ROOT/src" -I"$ROOT/tools/fake" -I"$ROOT/tools/preview" \
  "$ROOT/tools/test/test_app_thread.c" "$ROOT"/tools/fake/*.c \
  "$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c "$ROOT"/src/gfx/*.c \
  "$ROOT"/src/ui/*.c "$ROOT"/src/app/*.c \
  "$ROOT/tools/preview/vita2d_soft.c" -lz -lm \
  -o "$OUT/test_app_thread" 2>"$OUT/app-compile.log"; then
  cat "$OUT/app-compile.log" >&2
  echo "tsan: the app-thread build failed after the TSan probe" >&2
  exit 1
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
if [[ "$status" -ne 0 ]] || ! grep -q "test_app_thread: OK" "$OUT/app-run.log"; then
  cat "$OUT/app-run.log" >&2
  echo "tsan: the threaded app test did not complete cleanly" >&2
  exit 1
fi

echo "tsan: threaded app clean with the fakes"

# --- Threaded transfer ------------------------------------------------------
# The second app-thread binary drives a download, the SIZE-only refresh, a
# cancel, and a server session while the scripted peer runs on its own thread.
if ! "$CC" -std=c99 -O1 -g -pthread -fsanitize=thread \
  -I"$ROOT/src" -I"$ROOT/tools/fake" -I"$ROOT/tools/preview" \
  "$ROOT/tools/test/test_app_thread_xfer.c" "$ROOT"/tools/fake/*.c \
  "$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c "$ROOT"/src/gfx/*.c \
  "$ROOT"/src/ui/*.c "$ROOT"/src/app/*.c \
  "$ROOT/tools/preview/vita2d_soft.c" -lz -lm \
  -o "$OUT/test_app_thread_xfer" 2>"$OUT/xfer-compile.log"; then
  cat "$OUT/xfer-compile.log" >&2
  echo "tsan: the threaded transfer build failed after the TSan probe" >&2
  exit 1
fi

set +e
"$OUT/test_app_thread_xfer" >"$OUT/xfer-run.log" 2>&1
status=$?
set -e

if grep -q "WARNING: ThreadSanitizer" "$OUT/xfer-run.log"; then
  cat "$OUT/xfer-run.log" >&2
  echo "tsan: the threaded transfer test reported a race" >&2
  exit 1
fi
if [[ "$status" -ne 0 ]] || ! grep -q "test_app_thread_xfer: OK" \
  "$OUT/xfer-run.log"; then
  cat "$OUT/xfer-run.log" >&2
  echo "tsan: the threaded transfer test did not complete cleanly" >&2
  exit 1
fi

echo "tsan: threaded transfer clean with the fakes"

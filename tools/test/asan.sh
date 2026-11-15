#!/usr/bin/env bash
# Build the integration suite and the threaded app test with ASan/UBSan and run
# them. The ordinary host runner stays fast; this is the release evidence.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/test-asan"
CC="${CC:-cc}"
mkdir -p "$OUT"

build_and_run() {
  local src="$1"
  local name
  name="$(basename "$src" .c)"
  "$CC" -std=c99 -O1 -g -Wall -Wextra -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/src" -I"$ROOT/tools/fake" -I"$ROOT/tools/preview" \
    "$src" "$ROOT"/tools/fake/*.c "$ROOT"/src/core/*.c \
    "$ROOT"/src/core/ftp/*.c "$ROOT"/src/gfx/*.c "$ROOT"/src/ui/*.c \
    "$ROOT"/src/app/*.c "$ROOT/tools/preview/vita2d_soft.c" \
    -lz -lm -o "$OUT/$name"
  "$OUT/$name"
}

# Every host suite; the ordinary runner stays fast, this is the release
# evidence. A new suite is covered automatically.
for src in "$ROOT"/tools/test/test_*.c; do
  build_and_run "$src"
done
echo "asan: OK"

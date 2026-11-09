#!/usr/bin/env bash
set -euo pipefail

# Host preview: renders the shipped gfx layer through a software vita2d shim and
# writes a PNG. Not part of the app build.
#
#   tools/preview/run.sh [output-dir]

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/preview}"

mkdir -p "$OUT"

CC="${CC:-cc}"

"$CC" -std=c99 -O1 -g \
  -Wall -Wextra \
  -I"$ROOT/tools/preview" \
  -I"$ROOT/src" \
  "$ROOT/tools/preview/preview.c" \
  "$ROOT/tools/preview/vita2d_soft.c" \
  "$ROOT"/src/gfx/*.c \
  -lz -lm \
  -o "$OUT/preview"

"$OUT/preview" "$OUT"

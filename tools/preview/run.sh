#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

# Host preview: renders the shipped screens through a software vita2d shim and
# writes one PNG per settled state. Not part of the app build.
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
  "$ROOT"/src/gfx/*.c "$ROOT"/src/ui/*.c \
  "$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c \
  -lz -lm \
  -o "$OUT/preview"

"$OUT/preview" "$OUT"

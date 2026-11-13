#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

# The Vita package installer rejects 24-bit truecolor LiveArea art with
# 0x8010113D at the end of the install. Every sce_sys PNG must be an indexed
# 8-bit PNG (color type 3, bit depth 8). This guard runs in the host gate and in
# build.sh, so a replacement icon or background cannot regress the install.
#
#   tools/check_assets.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

check_png() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    echo "asset guard: missing $file" >&2
    return 1
  fi

  local signature
  signature="$(od -An -tx1 -N8 "$file" | tr -d ' \n')"
  if [[ "$signature" != "89504e470d0a1a0a" ]]; then
    echo "asset guard: $file is not a PNG" >&2
    return 1
  fi

  local bit_depth color_type
  bit_depth="$(od -An -tu1 -j24 -N1 "$file" | tr -d ' ')"
  color_type="$(od -An -tu1 -j25 -N1 "$file" | tr -d ' ')"
  if [[ "$bit_depth" != "8" || "$color_type" != "3" ]]; then
    echo "asset guard: $file is bit depth $bit_depth color type $color_type;" >&2
    echo "  the Vita installer requires indexed 8-bit PNG (bit depth 8, color type 3)." >&2
    return 1
  fi
}

check_png "$ROOT/sce_sys/icon0.png"
check_png "$ROOT/sce_sys/livearea/contents/bg.png"
check_png "$ROOT/sce_sys/livearea/contents/startup.png"

echo "asset guard: OK"

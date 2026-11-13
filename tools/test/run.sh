#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

# Host checks. Never part of the VPK.
#
#   tools/test/run.sh
#
# The run starts with the layering guard, then compiles each tools/test/test_*.c
# against the fakes and the real core, screens, and runtime.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/test"
CC="${CC:-cc}"

# --- Layering guard ---------------------------------------------------------
# Only these files may include a psp2/ or vita2d.h header (AGENTS.md). The scan
# covers every directory under src/, so a violation in src/app or src/ui fails
# just as a violation in src/core does. tools/ is host-only and is deliberately
# outside the guard: its preview stub *is* the software vita2d, not the SDK.
ALLOWED=(
  "src/platform/psp2_net.c"
  "src/platform/psp2_fs.c"
  "src/platform/psp2_input.c"
  "src/platform/psp2_ime.c"
  "src/platform/psp2_clock.c"
  "src/platform/psp2_log.c"
  "src/gfx/draw.c"
)

violations=0
while IFS= read -r -d '' file; do
  # psp2common/ and psp2kern/ are platform headers too. Matching only psp2/
  # would let a policy file include a Vita type through a sibling prefix.
  while IFS= read -r hit; do
    rel="${file#"$ROOT"/}"
    line="${hit%%:*}"

    permitted=0
    for allowed in "${ALLOWED[@]}"; do
      if [[ "$rel" == "$allowed" ]]; then
        permitted=1
        break
      fi
    done

    if [[ "$permitted" -eq 0 ]]; then
      echo "LAYERING VIOLATION: $rel:$line includes a platform header" >&2
      echo "  Only the seven files named in AGENTS.md may include a platform header." >&2
      violations=$((violations + 1))
    fi
  done < <(grep -nE \
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](psp2/|psp2common/|psp2kern/|vita2d\.h)' \
    "$file" || true)
done < <(find "$ROOT/src" -type f \( -name '*.c' -o -name '*.h' \) -print0)

if [[ "$violations" -ne 0 ]]; then
  echo "layering guard: FAILED ($violations violation(s))" >&2
  exit 1
fi

echo "layering guard: OK"

# --- Asset guard ------------------------------------------------------------
# The installer rejects 24-bit truecolor LiveArea art with 0x8010113D; every
# sce_sys PNG must be indexed 8-bit (color type 3, bit depth 8).
"$ROOT/tools/check_assets.sh"

# --- Host tests -------------------------------------------------------------
mkdir -p "$OUT"
shopt -s nullglob

tests=("$ROOT"/tools/test/test_*.c)
if [[ "${#tests[@]}" -eq 0 ]]; then
  echo "host tests: none yet"
  exit 0
fi

fake_sources=("$ROOT"/tools/fake/*.c)
core_sources=("$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c)

for test_src in "${tests[@]}"; do
  name="$(basename "$test_src" .c)"

  # The graphics test links the renderer against the software vita2d shim, not
  # the policy core. The render suites link the real ui and core as well.
  if [[ "$name" == "test_integration" ]]; then
    "$CC" -std=c99 -O1 -g -Wall -Wextra -pthread \
      -I"$ROOT/src" -I"$ROOT/tools/fake" -I"$ROOT/tools/preview" \
      "$test_src" "$ROOT"/tools/fake/*.c "$ROOT"/src/core/*.c \
      "$ROOT"/src/core/ftp/*.c "$ROOT"/src/gfx/*.c "$ROOT"/src/ui/*.c \
      "$ROOT"/src/app/*.c "$ROOT/tools/preview/vita2d_soft.c" \
      -lz -lm -o "$OUT/$name"
    "$OUT/$name"
    continue
  fi

  if [[ "$name" == "test_render_workspace" || "$name" == "test_render_screens" ]]; then
    "$CC" -std=c99 -O1 -g -Wall -Wextra \
      -I"$ROOT/src" -I"$ROOT/tools/preview" -I"$ROOT/tools/test" \
      "$test_src" "$ROOT"/src/gfx/*.c "$ROOT"/src/ui/*.c \
      "$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c \
      "$ROOT/tools/preview/vita2d_soft.c" \
      -lz -lm -o "$OUT/$name"
    "$OUT/$name"
    continue
  fi

  if [[ "$name" == "test_gfx" ]]; then
    "$CC" -std=c99 -O1 -g -Wall -Wextra \
      -I"$ROOT/src" -I"$ROOT/tools/preview" \
      "$test_src" "$ROOT"/src/gfx/*.c "$ROOT/tools/preview/vita2d_soft.c" \
      -lz -lm -o "$OUT/$name"
    "$OUT/$name"
    continue
  fi

  # Bash 3.2 treats an empty array as unbound under `set -u`.
  "$CC" -std=c99 -O1 -g -Wall -Wextra -pthread \
    -I"$ROOT/src" -I"$ROOT/tools/fake" \
    "$test_src" ${fake_sources[@]+"${fake_sources[@]}"} \
    ${core_sources[@]+"${core_sources[@]}"} \
    -o "$OUT/$name"
  "$OUT/$name"
done

echo "host tests: OK"

#!/usr/bin/env bash
set -euo pipefail

# Host checks. Never part of the VPK.
#
#   tools/test/run.sh
#
# Phase 1 runs the layering guard. Later units add host tests under
# tools/test/test_*.c; the loop below compiles each against the fakes and the
# policy core.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/test"
CC="${CC:-cc}"

# --- Layering guard ---------------------------------------------------------
# Only these files may include a psp2/ or vita2d.h header (AGENTS.md). The
# allowlist is whole-tree: a violation in src/app or src/ui fails just as a
# violation in src/core does, because that is where a select() or a vita2d
# call is most likely to drift in.
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
done < <(find "$ROOT" \( -path "$ROOT/build" -o -path "$ROOT/.git" \) -prune \
  -o -type f \( -name '*.c' -o -name '*.h' \) -print0)

if [[ "$violations" -ne 0 ]]; then
  echo "layering guard: FAILED ($violations violation(s))" >&2
  exit 1
fi

echo "layering guard: OK"

# --- Host tests -------------------------------------------------------------
mkdir -p "$OUT"
shopt -s nullglob

tests=("$ROOT"/tools/test/test_*.c)
if [[ "${#tests[@]}" -eq 0 ]]; then
  echo "host tests: none yet (Phase 1 is the build shape)"
  exit 0
fi

fake_sources=("$ROOT"/tools/fake/*.c)
core_sources=("$ROOT"/src/core/*.c "$ROOT"/src/core/ftp/*.c)

for test_src in "${tests[@]}"; do
  name="$(basename "$test_src" .c)"
  "$CC" -std=c99 -O1 -g -Wall -Wextra \
    -I"$ROOT/src" -I"$ROOT/tools/fake" \
    "$test_src" "${fake_sources[@]}" "${core_sources[@]}" \
    -o "$OUT/$name"
  "$OUT/$name"
done

echo "host tests: OK"

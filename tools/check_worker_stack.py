#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail the build when a worker-path frame would exhaust the thread stack.

b20 and b40 both died with the worker thread past its stack: b20 on a rename
that reserved run_copy_file's 8 KiB buffer inside fsop_run, b40 on a 64 KiB
`Path` array inlined into handle_action plus a recursive handle_action call.
The host tests cannot measure the target stack, so this check reads the linked
ELF and enforces the budget the design states (D77).

Usage: check_worker_stack.py build/vitaftpshell
"""

import re
import subprocess
import sys

# handle_action is the frame every local action passes through. Anything near
# this ceiling means a buffer belongs in the heap-owned Worker, not the stack.
HANDLE_ACTION_MAX = 16 * 1024

# The non-recursive part of the copy/rename chain. The folder scan recurses
# separately and is checked below as its own frame times its depth constant.
FIXED_CHAIN_MAX = 64 * 1024
# Functions the compiler keeps separate at -O2. State that moved into the heap
# run still costs stack when inlined, so each owner below is measured at the
# frame it actually reserves: worker_step owns local_run_pump/finish_local_run,
# handle_action owns run_plan_begin/execute_action/fact_tree, and fsop_run_step
# owns copy_pump and the copy loop.
FIXED_CHAIN = (
    "worker_step",
    "handle_action",
    "fsop_run_step",
    "logev_emit",
    "local_batch_emit_done",
)

# The folder scan recurses to this depth; the product must stay small. Keep in
# sync with WORKER_REMOTE_DEPTH in src/app/worker.c.
RECURSION_FRAME = "fact_tree"
RECURSION_DEPTH = 32
RECURSION_MAX = 8 * 1024

# The UI thread's dispatch frame. The command buffer lives in App, so this is
# small; the check exists so a future local cannot grow it unnoticed.
UI_FRAME = "feed"
UI_FRAME_MAX = 4 * 1024


def disassemble(elf):
    out = subprocess.run(
        ["arm-vita-eabi-objdump", "-d", elf],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return out.stdout


def frames(dis):
    """Return {function: (frame_bytes, contains_self_call)}."""
    sizes = {}
    current = None
    self_call = {}
    for line in dis.splitlines():
        m = re.match(r"^[0-9a-f]{8} <([^>]+)>:", line)
        if m:
            current = m.group(1)
            sizes.setdefault(current, 0)
            self_call.setdefault(current, False)
            continue
        if current is None:
            continue
        if re.search(r"sub\.w\s+sp, sp, #(\d+)", line):
            sizes[current] += int(re.search(r"#(\d+)", line).group(1))
        elif re.search(r"\bsub\s+sp, #(\d+)", line):
            sizes[current] += int(re.search(r"#(\d+)", line).group(1))
        push = re.search(r"\b(stmdb|push)\b[^\n]*", line)
        if push and ("{" in line):
            # A register push costs four bytes a register plus alignment slack.
            regs = re.findall(r"\b(r\d+|lr|sl|fp|ip)\b", line)
            sizes[current] += 4 * len(set(regs))
        if re.search(
            r"\b(bl|blx|b|b\.n|b\.w)\s+[0-9a-f]+ <"
            + re.escape(current)
            + r">",
            line,
        ):
            self_call[current] = True
    return sizes, self_call


def main():
    if len(sys.argv) != 2:
        print("usage: check_worker_stack.py <elf>", file=sys.stderr)
        return 2

    sizes, self_call = frames(disassemble(sys.argv[1]))
    problems = []

    handle = sizes.get("handle_action")
    if handle is None:
        problems.append("handle_action not found in the ELF")
    elif handle > HANDLE_ACTION_MAX:
        problems.append(
            "handle_action reserves %d bytes (limit %d): a large local "
            "belongs in the Worker" % (handle, HANDLE_ACTION_MAX)
        )
    if self_call.get("handle_action"):
        problems.append("handle_action calls itself; batches must advance from worker_step")

    chain = 0
    missing = []
    for name in FIXED_CHAIN:
        if name not in sizes:
            missing.append(name)
            continue
        chain += sizes[name]
    if missing:
        problems.append("chain functions missing from the ELF: %s" % ", ".join(missing))
    elif chain > FIXED_CHAIN_MAX:
        problems.append(
            "worker copy chain reserves %d bytes (limit %d)"
            % (chain, FIXED_CHAIN_MAX)
        )

    recursion = 0
    scan = sizes.get(RECURSION_FRAME)
    if scan is None:
        problems.append("%s not found in the ELF" % RECURSION_FRAME)
    elif not self_call.get(RECURSION_FRAME):
        problems.append("%s is not a self-call; the depth product is meaningless"
                        % RECURSION_FRAME)
    else:
        recursion = scan * (RECURSION_DEPTH + 1)
        if recursion > RECURSION_MAX:
            problems.append(
                "%s recursion reserves %d bytes (limit %d)"
                % (RECURSION_FRAME, recursion, RECURSION_MAX)
            )

    ui = sizes.get(UI_FRAME)
    if ui is None:
        problems.append("%s not found in the ELF" % UI_FRAME)
    elif ui > UI_FRAME_MAX:
        problems.append(
            "UI dispatch reserves %d bytes (limit %d): move locals into App"
            % (ui, UI_FRAME_MAX)
        )
    if self_call.get(UI_FRAME):
        problems.append("%s calls itself; defer the event instead" % UI_FRAME)

    if problems:
        for p in problems:
            print("worker stack: %s" % p, file=sys.stderr)
        return 1

    print(
        "worker stack: handle_action %d B, fixed chain %d B, scan %d B, "
        "UI %d B, all inside budget"
        % (handle or 0, chain, recursion, ui or 0)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

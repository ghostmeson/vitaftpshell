#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail the host suite when the documentation drifts from its owners.

The documentation truth sweep (docs/plans/2026-11-16-0845-docs-documentation-
truth-sweep-plan.md, DR13) requires the structure to be checkable: links and
cited design sheets resolve, every plan and design file has an index row, every
markdown file has an owner, R/U/KTD/D references resolve, whole-set ranges end
at their owner's highest ID, build tokens stay in the documents that own
volatile build history, the three status blocks are one block, the traceability
table keeps its three rows, and a retired phrase cannot return to a live
document.

The baseline in docs/drift-baseline.txt is a snapshot of the violations that
existed when the gate landed.  Its entries key on file, check, and matched text,
never on a line number, so a moved paragraph stays covered.  Entries may only
be removed: --regenerate refuses to write a baseline that would gain an entry,
and a normal run fails on an entry that no longer matches anything so the
baseline shrinks instead of rotting.

The checks live in docsguard/checks.py, the command line and baseline in
docsguard/cli.py, and the self-test fixtures in docsguard/fixtures.py.

Usage: check_docs.py [--verbose] [--self-test] [--regenerate] [--root DIR]
"""

import os
import sys

# The package sits beside this script.  Running the script directly already
# puts this directory on sys.path, but naming it keeps the import working when
# the script is reached through a symlink or an embedding caller.
TOOLS_DIR = os.path.dirname(os.path.realpath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from docsguard.cli import main


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# SPDX-License-Identifier: GPL-3.0-only
"""Arguments, the baseline, reporting, and exit codes for the docs gate."""

import os
import sys

from .checks import Tree, collect_findings, key_set


# The baseline lives beside the docs it measures, so a fixture rooted elsewhere
# carries its own baseline and the self-test never reads the real one.
BASELINE_REL = "docs/drift-baseline.txt"


# --- baseline ---------------------------------------------------------------


def load_baseline(tree):
    """Return (entries, errors).  A malformed line is an error, not a guess."""
    entries = set()
    errors = []
    lines = tree.lines(BASELINE_REL)
    if lines is None:
        return entries, errors
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3 or not all(part.strip() for part in parts):
            errors.append((number, line))
            continue
        entries.add(tuple(part.strip() for part in parts))
    return entries, errors


def format_baseline(entries):
    header = (
        "# docs guard baseline: file<TAB>check<TAB>matched text\n"
        "# Entries may only be removed; --regenerate prunes them and refuses"
        " additions.\n"
    )
    return header + "".join(
        "\t".join(entry) + "\n" for entry in sorted(entries)
    )


def partition(findings, baseline):
    covered = [f for f in findings if (f.file, f.check, f.text) in baseline]
    uncovered = [f for f in findings if (f.file, f.check, f.text) not in baseline]
    return covered, uncovered


def stale_entries(findings, baseline):
    return sorted(baseline - key_set(findings))


# --- arguments and reporting ------------------------------------------------


def usage():
    print(
        "usage: check_docs.py [--verbose] [--self-test] [--regenerate]"
        " [--root DIR]",
        file=sys.stderr,
    )


def parse_args(argv):
    options = {
        "root": ".",
        "verbose": False,
        "self_test": False,
        "regenerate": False,
    }
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--verbose":
            options["verbose"] = True
            index += 1
        elif arg == "--self-test":
            options["self_test"] = True
            index += 1
        elif arg == "--regenerate":
            options["regenerate"] = True
            index += 1
        elif arg == "--root":
            if index + 1 >= len(argv):
                usage()
                return None
            options["root"] = argv[index + 1]
            index += 2
        else:
            usage()
            return None
    return options


def report(found):
    print(
        "docs guard: %s:%d: %s: %s"
        % (found.file, found.line, found.check, found.message),
        file=sys.stderr,
    )


# --- entry point ------------------------------------------------------------


def main(argv):
    options = parse_args(argv)
    if options is None:
        return 2
    if options["self_test"]:
        # Imported here, not at module scope: fixtures.py drives this same
        # main() to assert the gate's exits and output, and a top-level import
        # would close the cycle.
        from . import fixtures
        return fixtures.self_test()

    tree = Tree(options["root"])
    found = collect_findings(tree)
    baseline, errors = load_baseline(tree)
    if errors:
        for number, line in errors:
            print(
                "docs guard: %s:%d: baseline: malformed entry %r"
                % (BASELINE_REL, number, line),
                file=sys.stderr,
            )
        return 1

    if options["regenerate"]:
        additions = sorted(key_set(found) - baseline)
        if additions:
            for rel, check, text in additions:
                print(
                    "docs guard: baseline: refusing to add entry:"
                    " %s\t%s\t%s" % (rel, check, text),
                    file=sys.stderr,
                )
            return 1
        if not found and not baseline:
            print("docs guard: no findings; baseline not written")
            return 0
        path = os.path.join(tree.root, BASELINE_REL)
        if not found:
            if os.path.exists(path):
                os.remove(path)
            print("docs guard: no findings; baseline removed")
            return 0
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(format_baseline(key_set(found)))
        print(
            "docs guard: baseline updated: %d entries"
            % len(key_set(found)))
        return 0

    covered, uncovered = partition(found, baseline)
    stale = stale_entries(found, baseline)
    for item in uncovered:
        report(item)
    for rel, check, text in stale:
        print(
            "docs guard: %s: baseline entry no longer matches (%s: %s);"
            " run --regenerate to remove it" % (BASELINE_REL, check, text),
            file=sys.stderr,
        )
    if uncovered or stale:
        return 1

    if options["verbose"]:
        for item in covered:
            print(
                "docs guard [baseline]: %s:%d: %s: %s"
                % (item.file, item.line, item.check, item.message),
                file=sys.stderr,
            )
    print("docs guard: OK")
    return 0


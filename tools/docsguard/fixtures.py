# SPDX-License-Identifier: GPL-3.0-only
"""The --self-test fixture trees and the assertions they must satisfy."""

import contextlib
import io
import os
import shutil
import sys
import tempfile

from .checks import STATUS_BEGIN, STATUS_END, Tree, collect_findings
from .cli import BASELINE_REL, format_baseline, main, partition


# --- self-test fixtures -----------------------------------------------------


STATUS_BLOCK_ROOT = (
    STATUS_BEGIN + "\n"
    "`building`. The board is [the board](PROJECTS.md); the evidence is"
    " [the evidence](docs/VERIFICATION.md).\n"
    + STATUS_END + "\n"
)
STATUS_BLOCK_PROJECT = (
    STATUS_BEGIN + "\n"
    "`building`. The board is [the board](../PROJECTS.md); the evidence is"
    " [the evidence](VERIFICATION.md).\n"
    + STATUS_END + "\n"
)
FIXTURE_PLAN = "docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md"


def fixture_files():
    return {
        "README.md": "# Fixture\n\n" + STATUS_BLOCK_ROOT,
        "AGENTS.md": "# Fixture contract\n\n" + STATUS_BLOCK_ROOT,
        "PROJECTS.md": "# Projects\n\nFixture board.\n",
        "STRATEGY.md": "# Strategy\n\n```text\nfixture\n```\n",
        "docs/PROJECT.md": (
            "# Project\n\n"
            + STATUS_BLOCK_PROJECT
            + "\n## Traceability\n\n"
            "| Feature | Requirements | Flows | Matrix rows |\n"
            "|---|---|---|---|\n"
            "| FTP client | R1 | F1 | M1 |\n"
            "| Local file manager | R2 | F2 | M1 |\n"
            "| FTP server | R3 | F3 | M1 |\n"
        ),
        "docs/VERIFICATION.md": (
            "# Verification\n\n"
            "| Row | What | Result |\n"
            "|---|---|---|\n"
            "| M1 | fixture matrix row | pending |\n"
        ),
        "docs/FIXES.md": "# Fixes\n",
        "docs/DECISIONS.md": "# Decisions\n\n## D1. Fixture decision\n",
        "docs/README.md": (
            "# Documentation\n\n"
            "| Path | What it is | Owner |\n"
            "|---|---|---|\n"
            "| `../README.md` | Root readme | Project |\n"
            "| `../STRATEGY.md` | Strategy | Product |\n"
            "| `../PROJECTS.md` | Board | Project |\n"
            "| `../AGENTS.md` | Contract | Project |\n"
            "| `README.md` | Map | Docs |\n"
            "| `PROJECT.md` | Project | Product |\n"
            "| `VERIFICATION.md` | Evidence | Engineering |\n"
            "| `FIXES.md` | Fixes | Engineering |\n"
            "| `DECISIONS.md` | Decisions | All |\n"
            "| `plans/` | Plans | Planning |\n"
            "| `design/` | Design | Design |\n"
        ),
        "docs/plans/README.md": (
            "# Plans\n\n"
            "| Plan | Status |\n"
            "|---|---|\n"
            "| [fixture](2026-11-08-1103-feat-vitaftpshell-v1-plan.md) | active |\n"
        ),
        FIXTURE_PLAN: (
            "# Fixture Plan\n\n"
            "- R1. First.\n"
            "- R2. Second.\n"
            "- R3. Third.\n"
            "- KTD1. A technical decision.\n\n"
            "### U1. A unit\n"
        ),
        "docs/design/README.md": (
            "# Design\n\n"
            "| File | State |\n"
            "|---|---|\n"
            "| [sheet](2026-01-01-sheet.html) | settled |\n"
        ),
        "docs/design/2026-01-01-sheet.html": "<html><body>sheet</body></html>\n",
    }


def build_fixture(root):
    for rel, body in fixture_files().items():
        path = os.path.join(root, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(body)


def append(root, rel, body):
    with open(os.path.join(root, rel.replace("/", os.sep)), "a",
              encoding="utf-8", newline="\n") as fh:
        fh.write(body)


def write_fixture(root, rel, body):
    path = os.path.join(root, rel.replace("/", os.sep))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)


def replace_in(root, rel, old, new):
    path = os.path.join(root, rel.replace("/", os.sep))
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text.replace(old, new, 1))


def self_test():
    """Prove every check fails on its own fixture and passes on a clean one."""
    cases = []

    def case(name, expected, mutate):
        cases.append((name, expected, mutate))

    case("clean fixture", None, None)
    case("broken link", "link",
         lambda root: append(root, "README.md", "\n[broken](missing-page.md)\n"))
    case("missing cited sheet", "code-path",
         lambda root: append(
             root, "docs/DECISIONS.md",
             "\nSee `docs/design/missing-sheet.html`.\n"))
    case("unbalanced fence", "fence",
         lambda root: append(root, "STRATEGY.md", "\n```text\nunclosed\n"))
    case("unindexed plan", "inventory",
         lambda root: write_fixture(
             root, "docs/plans/2026-01-02-0900-unindexed-plan.md",
             "# Unindexed\n"))
    case("unowned markdown", "ownership",
         lambda root: write_fixture(root, "docs/unlisted.md", "# Unlisted\n"))
    case("dead reference", "reference",
         lambda root: append(root, "README.md", "\nSee R99.\n"))
    case("stale whole-set range", "reference",
         lambda root: append(root, "README.md", "\nSee R1-R2.\n"))
    case("build token outside owner", "build-token",
         lambda root: append(root, "README.md", "\nBuild b99.\n"))
    case("status block divergence", "status-block",
         lambda root: replace_in(root, "AGENTS.md", "`building`", "`built`"))
    case("traceability row missing", "traceability",
         lambda root: replace_in(
             root, "docs/PROJECT.md", "| FTP server | R3 | F3 | M1 |\n", ""))
    case("traceability requirement missing", "traceability",
         lambda root: replace_in(
             root, "docs/PROJECT.md",
             "| FTP client | R1 | F1 | M1 |", "| FTP client | F1 | M1 |"))
    case("traceability matrix missing", "traceability",
         lambda root: replace_in(
             root, "docs/PROJECT.md",
             "| FTP client | R1 | F1 | M1 |", "| FTP client | R1 | F1 | - |"))
    case("retired phrase in summary", "retired-phrase",
         lambda root: append(root, "README.md", "\nThis is pre-alpha.\n"))
    case("retired phrase in dated plan", None,
         lambda root: append(root, FIXTURE_PLAN, "\nThis was pre-alpha.\n"))

    failures = []
    for name, expected, mutate in cases:
        root = tempfile.mkdtemp(prefix="check-docs-")
        try:
            build_fixture(root)
            if mutate is not None:
                mutate(root)
            found = collect_findings(Tree(root))
            checks = {f.check for f in found}
            if expected is None and found:
                failures.append(
                    "%s: expected a clean tree, found %s"
                    % (name, sorted(checks)))
            elif expected is not None and expected not in checks:
                failures.append(
                    "%s: expected %s, found %s"
                    % (name, expected, sorted(checks) or "none"))
        finally:
            shutil.rmtree(root, ignore_errors=True)

    # A reference inside range passes where the stale range above failed.
    root = tempfile.mkdtemp(prefix="check-docs-")
    try:
        build_fixture(root)
        append(root, "README.md", "\nSee R3.\n")
        checks = {f.check for f in collect_findings(Tree(root))}
        if "reference" in checks:
            failures.append("in-range reference: expected no reference finding")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    # A clean tree passes the gate with no baseline file present (AE4).
    root = tempfile.mkdtemp(prefix="check-docs-")
    try:
        build_fixture(root)
        with contextlib.redirect_stdout(io.StringIO()) as clean_out:
            with contextlib.redirect_stderr(io.StringIO()):
                clean_run = main(["--root", root])
        if clean_run != 0 or clean_out.getvalue().strip() != "docs guard: OK":
            failures.append(
                "clean gate: expected exit 0 and the exact OK line")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    # A baseline entry suppresses its finding; adding to it is refused and a
    # stale entry is pruned by a regeneration that only removes.
    root = tempfile.mkdtemp(prefix="check-docs-")
    try:
        build_fixture(root)
        append(root, "README.md", "\nThis is pre-alpha.\n")
        found = collect_findings(Tree(root))
        phrase = [f for f in found if f.check == "retired-phrase"]
        if not phrase:
            failures.append("baseline: fixture produced no phrase finding")
        else:
            entry = (phrase[0].file, phrase[0].check, phrase[0].text)
            covered, uncovered = partition(found, {entry})
            if not covered or uncovered:
                failures.append(
                    "baseline: one entry did not suppress exactly its finding")
            stale = ("README.md", "link", "stale-entry")
            baseline_path = os.path.join(
                root, BASELINE_REL.replace("/", os.sep))
            os.makedirs(os.path.dirname(baseline_path), exist_ok=True)
            with open(baseline_path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(format_baseline({entry, stale}))
            append(root, "README.md", "\nBuild b99.\n")
            with contextlib.redirect_stdout(io.StringIO()):
                with contextlib.redirect_stderr(io.StringIO()):
                    refused = main(["--root", root, "--regenerate"])
            if refused != 1:
                failures.append(
                    "baseline: regeneration with a new entry did not exit 1")
            replace_in(root, "README.md", "\nBuild b99.\n", "")
            with contextlib.redirect_stdout(io.StringIO()):
                with contextlib.redirect_stderr(io.StringIO()):
                    stale_run = main(["--root", root])
            if stale_run != 1:
                failures.append(
                    "baseline: a stale entry did not fail the normal run")
            with contextlib.redirect_stdout(io.StringIO()):
                with contextlib.redirect_stderr(io.StringIO()):
                    pruned = main(["--root", root, "--regenerate"])
            if pruned != 0:
                failures.append(
                    "baseline: removal-only regeneration did not exit 0")
            with open(baseline_path, encoding="utf-8") as fh:
                written = fh.read()
            if "stale-entry" in written:
                failures.append(
                    "baseline: regeneration did not prune the stale entry")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    with contextlib.redirect_stderr(io.StringIO()):
        if main(["--root"]) != 2:
            failures.append("missing argument: expected exit 2")

    if failures:
        for failure in failures:
            print("docs guard: self-test: %s" % failure, file=sys.stderr)
        return 1
    print("docs guard: self-test OK")
    return 0

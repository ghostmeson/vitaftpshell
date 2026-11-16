# SPDX-License-Identifier: GPL-3.0-only
"""The drift checks and the read-only tree view they run against.

Each check reports Findings for one drift class.  `collect_findings` runs them
in CHECKS order and sorts the result; nothing here reads the baseline, parses
arguments, or exits.
"""

import glob
import os
import re
from collections import Counter, namedtuple


# Only files that own volatile build history may carry a build token.  Dated
# plan bodies are permitted because they record which build a change landed in.
BUILD_OWNERS = frozenset((
    "PROJECTS.md",
    "docs/VERIFICATION.md",
    "docs/FIXES.md",
    "docs/DECISIONS.md",
))

STATUS_DOCS = ("README.md", "AGENTS.md", "docs/PROJECT.md")
STATUS_BEGIN = "<!-- status:begin -->"
STATUS_END = "<!-- status:end -->"

# Active and archived inventory roots and the index that must mention each file.
INVENTORY = (
    ("docs/plans", "docs/plans/README.md"),
    ("docs/plans/archive", "docs/plans/archive/README.md"),
    ("docs/design", "docs/design/README.md"),
    ("docs/design/archive", "docs/design/archive/README.md"),
)

V1_PLAN = "docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md"
DECISIONS = "docs/DECISIONS.md"
VERIFICATION = "docs/VERIFICATION.md"

# A dated plan body is exempt from the whole-set range rule and from the
# retired-phrase list: it is the historical record of a change, not a summary.
DATED_PLAN_RE = re.compile(r"^\d{4}-\d{2}-\d{2}-")

SKIP_DIRS = frozenset((
    ".git",
    "build",
    "vitasdk",
    "node_modules",
    "__pycache__",
    ".venv",
))

FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})(.*)$")
LINK_RE = re.compile(r'!?\[[^\]]*\]\(\s*(<[^>]*>|[^)\s]+)(?:\s+"[^"]*")?\s*\)')
CODE_RE = re.compile(r"`([^`\n]+)`")
REF_RE = re.compile(r"(?<![#\w])(R|U|KTD|D)(\d+)\b")
RANGE_RE = re.compile(
    r"(?<![#\w])(R|U|KTD)(\d+)\s*[-\u2013\u2014]\s*(?:(R|U|KTD))?(\d+)\b"
)
BUILD_RE = re.compile(r"vitaftpshell-v\d+\.\d+|\bb\d+\b")
RECURSIVE_RE = re.compile(r"\brecursive folders?\b", re.IGNORECASE)
QUALIFIER_RE = re.compile(
    r"\b(local|remote|planned|deferred|R22|R23|D65|D92)\b", re.IGNORECASE
)

RETIRED_PHRASES = (
    ("one active transfer", re.compile(r"\bone active transfer\b", re.IGNORECASE)),
    (
        "multiple server clients",
        re.compile(r"\bmultiple (?:simultaneous )?server clients\b", re.IGNORECASE),
    ),
    ("pre-alpha", re.compile(r"\bpre[-\s]?alpha\b", re.IGNORECASE)),
    ("TO RETRY", re.compile(r"\bTO RETRY\b")),
    ("control+1", re.compile(r"\bcontrol\s*\+\s*1\b", re.IGNORECASE)),
    (
        "control port plus one",
        re.compile(r"\bcontrol (?:port )?plus (?:one|1)\b", re.IGNORECASE),
    ),
    ("1024-65534", re.compile(r"\b1024\s*[-\u2013\u2014]\s*65534\b")),
)

Finding = namedtuple("Finding", "file line check text message")


def text_key(value):
    """Collapse a matched text to one baseline-safe field.

    Baseline entries never carry tabs or newlines, and trailing whitespace is
    noise: a reflowed line must not orphan its entry.
    """
    return " ".join(str(value).split())


class Tree:
    """Read-only view of the repository or a self-test fixture."""

    def __init__(self, root):
        self.root = os.path.abspath(root)
        self._lines = {}

    def path(self, rel):
        return os.path.join(self.root, rel.replace("/", os.sep))

    def exists(self, rel):
        return os.path.exists(self.path(rel))

    def lines(self, rel):
        if rel not in self._lines:
            try:
                with open(self.path(rel), encoding="utf-8", errors="replace") as fh:
                    self._lines[rel] = fh.read().splitlines()
            except OSError:
                self._lines[rel] = None
        return self._lines[rel]

    def is_dir(self, rel):
        return os.path.isdir(self.path(rel))


def finding(rel, line, check, text, message):
    return Finding(rel, line, check, text_key(text), message)


def key_set(findings):
    return {(f.file, f.check, f.text) for f in findings}


# --- file discovery ---------------------------------------------------------


def markdown_files(tree):
    found = []
    for dirpath, dirnames, filenames in os.walk(tree.root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for name in sorted(filenames):
            if name.endswith(".md"):
                rel = os.path.relpath(os.path.join(dirpath, name), tree.root)
                found.append(rel.replace(os.sep, "/"))
    return sorted(found)


def is_dated_plan(rel):
    return rel.startswith("docs/plans/") and DATED_PLAN_RE.match(
        os.path.basename(rel)
    ) is not None


def unfenced(lines):
    """Yield (line number, line) for lines outside fenced code blocks.

    A quoted example is not a live claim, so references, tokens, and phrases
    are checked in prose only.  The fence check reports an unbalanced block on
    its own, which is why skipping to end-of-file here is safe.
    """
    fence = None
    kept = []
    for number, line in enumerate(lines, 1):
        match = FENCE_RE.match(line)
        if fence is None and match:
            fence = (match.group(1)[0], len(match.group(1)))
            continue
        if fence is not None:
            if (
                match
                and match.group(1)[0] == fence[0]
                and len(match.group(1)) >= fence[1]
                and not match.group(2).strip()
            ):
                fence = None
            continue
        kept.append((number, line))
    return kept


def local_links(tree, rel):
    """Resolve every relative markdown link in one file to a tree path."""
    resolved = set()
    for _, line in unfenced(tree.lines(rel) or []):
        for match in LINK_RE.finditer(line):
            target = target_path(match.group(1))
            if target is None:
                continue
            resolved.add(os.path.normpath(
                os.path.join(os.path.dirname(rel), target)
            ).replace(os.sep, "/"))
    return resolved


def target_path(raw):
    target = raw.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1].strip()
    if not target or target.startswith("#"):
        return None
    if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target):
        return None
    if target.startswith("/"):
        return None
    path = target.split("#", 1)[0]
    return path or None


# --- one check per drift class ---------------------------------------------


def check_links(tree):
    """Every relative markdown link and image must resolve on disk.

    External URLs and in-page anchors are not this repository's files, so they
    are skipped.  A link keeps its anchor: only the path part is resolved.
    """
    found = []
    for rel in markdown_files(tree):
        for number, line in unfenced(tree.lines(rel) or []):
            for match in LINK_RE.finditer(line):
                raw = match.group(1)
                target = target_path(raw)
                if target is None:
                    continue
                resolved = os.path.normpath(
                    os.path.join(os.path.dirname(rel), target)
                )
                if not os.path.exists(os.path.join(tree.root, resolved)):
                    found.append(finding(
                        rel,
                        number,
                        "link",
                        raw.strip("<>"),
                        "link target does not resolve: %s" % raw.strip("<>"),
                    ))
    return found


def check_code_paths(tree):
    """A backticked docs/ path is a citation, so a missing file is drift.

    Globs and ellipses are templates, not citations; flagging them would make
    the check cry wolf on prose that is deliberately general.  An archived
    plan's forward-looking paths describe what it proposed, not what exists
    now; its links, fences, and references stay checked.
    """
    found = []
    for rel in markdown_files(tree):
        if "/archive/" in "/" + rel:
            continue
        for number, line in unfenced(tree.lines(rel) or []):
            for match in CODE_RE.finditer(line):
                token = match.group(1).strip()
                if not token.startswith("docs/"):
                    continue
                if any(mark in token for mark in "*{}<>|") or "..." in token:
                    continue
                path = token.split("#", 1)[0]
                if not path:
                    continue
                if not os.path.exists(os.path.join(tree.root, path)):
                    found.append(finding(
                        rel,
                        number,
                        "code-path",
                        token,
                        "backticked path does not resolve: %s" % token,
                    ))
    return found


def check_fences(tree):
    """Every fenced block must close with the fence that opened it."""
    found = []
    for rel in markdown_files(tree):
        opened = None
        for number, line in enumerate(tree.lines(rel) or [], 1):
            match = FENCE_RE.match(line)
            if not match:
                continue
            marker = match.group(1)
            rest = match.group(2)
            if opened is None:
                opened = (marker[0], len(marker), number, line.strip())
            elif (
                marker[0] == opened[0]
                and len(marker) >= opened[1]
                and not rest.strip()
            ):
                opened = None
        if opened is not None:
            found.append(finding(
                rel,
                opened[2],
                "fence",
                opened[3],
                "unbalanced code fence opened here",
            ))
    return found


def check_inventory(tree):
    """Every plan and design file is indexed in its own folder's README.

    The index link is the row: a backticked mention is not an index.  An
    archived file is indexed by the archive README, so a move and its row land
    in the same unit (KTD6).
    """
    found = []
    for area, index in INVENTORY:
        if not tree.is_dir(area):
            continue
        indexed = local_links(tree, index) if tree.exists(index) else set()
        for name in sorted(os.listdir(tree.path(area))):
            if name.startswith("."):
                continue
            rel = "%s/%s" % (area, name)
            if name == "README.md" or not os.path.isfile(tree.path(rel)):
                continue
            if not tree.exists(index):
                found.append(finding(
                    rel, 1, "inventory", rel,
                    "no index file %s" % index,
                ))
            elif rel not in indexed:
                found.append(finding(
                    rel, 1, "inventory", rel,
                    "not indexed by %s" % index,
                ))
    return found


def check_ownership(tree):
    """Every markdown file is a row in the ownership table or under one.

    A directory row such as `plans/` owns the files beneath it; the design
    renders are covered that way rather than one row per PNG (DR1).
    """
    index = "docs/README.md"
    exact = set()
    directories = set()
    for _, line in unfenced(tree.lines(index) or []):
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cell = stripped.strip("|").split("|")[0].strip()
        match = re.fullmatch(r"`([^`]+)`", cell)
        if not match:
            continue
        row = os.path.normpath(
            os.path.join(os.path.dirname(index), match.group(1))
        ).replace(os.sep, "/")
        if tree.is_dir(row):
            directories.add(row)
        else:
            exact.add(row)

    found = []
    for rel in markdown_files(tree):
        if rel in exact:
            continue
        if any(rel.startswith(directory + "/") for directory in directories):
            continue
        found.append(finding(
            rel, 1, "ownership", "ownership-table",
            "markdown file is not listed in %s" % index,
        ))
    return found


def read_ids(lines, pattern):
    ids = set()
    for line in lines or []:
        match = re.match(pattern, line)
        if match:
            ids.add(int(match.group(1)))
    return ids


def check_references(tree):
    """Every R/U/KTD/D citation resolves, and a whole-set range is complete.

    The v1 plan owns R, U, and KTD numbering; DECISIONS owns D numbering.  A
    dated plan body may define its own units, so inside one only the range
    matters.  Outside one, the ID must exist, and a range that starts at 1 is
    the whole set: its end must equal the owner's highest defined ID.
    """
    plan_lines = tree.lines(V1_PLAN)
    decision_lines = tree.lines(DECISIONS)
    if plan_lines is None:
        return [finding(
            V1_PLAN, 1, "reference", "v1-plan",
            "cannot read the requirement owner",
        )]
    if decision_lines is None:
        return [finding(
            DECISIONS, 1, "reference", "decisions",
            "cannot read the decision owner",
        )]

    defined = {
        "R": read_ids(plan_lines, r"^- R(\d+)\."),
        "KTD": read_ids(plan_lines, r"^- KTD(\d+)\."),
        "U": read_ids(plan_lines, r"^### U(\d+)\."),
        "D": read_ids(decision_lines, r"^## D(\d+)\."),
    }
    maxima = {
        "R": max(defined["R"]),
        "KTD": max(defined["KTD"]),
        "U": max(defined["U"]),
        "D": max(defined["D"]),
    }
    for prefix in ("R", "U", "KTD", "D"):
        if not defined[prefix]:
            return [finding(
                V1_PLAN if prefix != "D" else DECISIONS,
                1, "reference", "definitions",
                "the %s owner defines no %s ids" % (prefix, prefix),
            )]

    found = []
    for rel in markdown_files(tree):
        plan_body = is_dated_plan(rel)
        for number, line in unfenced(tree.lines(rel) or []):
            consumed = []
            for match in RANGE_RE.finditer(line):
                consumed.append(match.span())
                prefix, start, second, end = match.groups()
                start = int(start)
                end = int(end)
                raw = text_key(match.group(0))
                if second is not None and second != prefix:
                    found.append(finding(
                        rel, number, "reference", raw,
                        "range mixes prefixes: %s" % raw,
                    ))
                    continue
                if start < 1 or end < start:
                    found.append(finding(
                        rel, number, "reference", raw,
                        "range is not an increasing set: %s" % raw,
                    ))
                    continue
                if plan_body:
                    if end > maxima[prefix]:
                        found.append(finding(
                            rel, number, "reference", raw,
                            "range %s exceeds the owner's highest %s%d"
                            % (raw, prefix, maxima[prefix]),
                        ))
                elif start not in defined[prefix] or end not in defined[prefix]:
                    found.append(finding(
                        rel, number, "reference", raw,
                        "range has an undefined %s id: %s" % (prefix, raw),
                    ))
                elif start == 1 and end != maxima[prefix]:
                    found.append(finding(
                        rel, number, "reference", raw,
                        "whole-set range ends at %s%d; the owner defines"
                        " %s1-%s%d"
                        % (prefix, end, prefix, prefix, maxima[prefix]),
                    ))
            for match in REF_RE.finditer(line):
                if any(begin <= match.start() < finish for begin, finish in consumed):
                    continue
                prefix, number_text = match.groups()
                value = int(number_text)
                raw = text_key(match.group(0))
                if prefix == "D":
                    if value not in defined["D"]:
                        found.append(finding(
                            rel, number, "reference", raw,
                            "dead reference: %s (DECISIONS defines D1-D%d)"
                            % (raw, maxima["D"]),
                        ))
                elif plan_body:
                    if value < 1 or value > maxima[prefix]:
                        found.append(finding(
                            rel, number, "reference", raw,
                            "reference %s is outside the owner's %s1-%s%d"
                            % (raw, prefix, prefix, maxima[prefix]),
                        ))
                elif value not in defined[prefix]:
                    found.append(finding(
                        rel, number, "reference", raw,
                        "dead reference: %s (the v1 plan defines %s1-%s%d)"
                        % (raw, prefix, prefix, maxima[prefix]),
                    ))
    return found


def check_build_tokens(tree):
    """A volatile build token stays in the documents that own build history.

    A summary that pins a build goes stale on the next one; the board, the
    evidence page, the fix index, the decisions, and dated plans are the only
    places allowed to name one.
    """
    found = []
    for rel in markdown_files(tree):
        if rel in BUILD_OWNERS or is_dated_plan(rel):
            continue
        for number, line in unfenced(tree.lines(rel) or []):
            for match in BUILD_RE.finditer(line):
                # A token inside a kebab-case filename is part of a plan's
                # name, not a claim about which build carries the code.
                start, end = match.span()
                if (start > 0 and line[start - 1] == "-") or (
                        end < len(line) and line[end] == "-"):
                    continue
                token = text_key(match.group(0))
                found.append(finding(
                    rel, number, "build-token", token,
                    "build token outside its owner: %s" % token,
                ))
    return found


def status_block(tree, rel):
    """Return (begin_line, prose_lines, link_rows, marker_error).

    link_rows holds (raw target, placeholder, resolved path) for comparison.
    A missing marker is its own error so the message names the marker the
    document lost, not a vague "status" failure.
    """
    lines = tree.lines(rel)
    if lines is None:
        return None, None, None, ("status:begin", 1, "cannot read the file")
    begin = None
    end = None
    for number, line in enumerate(lines, 1):
        marker = line.strip()
        if marker == STATUS_BEGIN and begin is None:
            begin = number
        elif marker == STATUS_END and begin is not None:
            end = number
            break
    if begin is None:
        return None, None, None, (
            "status:begin", 1, "status block begin marker is missing")
    if end is None:
        return None, None, None, (
            "status:end", begin, "status block end marker is missing")

    prose = [line.rstrip() for line in lines[begin:end - 1]]
    while prose and not prose[0].strip():
        prose.pop(0)
    while prose and not prose[-1].strip():
        prose.pop()

    links = []
    for line in prose:
        for match in LINK_RE.finditer(line):
            raw = match.group(1).strip()
            target = target_path(raw)
            if target is None:
                links.append((raw, None, None))
                continue
            resolved = os.path.normpath(
                os.path.join(os.path.dirname(rel), target)
            ).replace(os.sep, "/")
            if resolved == "PROJECTS.md":
                links.append((raw, "{BOARD}", resolved))
            elif resolved == "docs/VERIFICATION.md":
                links.append((raw, "{EVIDENCE}", resolved))
            else:
                links.append((raw, None, resolved))
    return begin, prose, links, None


def check_status_block(tree):
    """README, AGENTS, and docs/PROJECT carry one identical status block.

    Only the board and evidence link targets may differ, because each document
    sits at a different depth.  The labels and every other word must match, or
    one summary has drifted from the other two (DR4, AE8).
    """
    found = []
    compared = {}
    for rel in STATUS_DOCS:
        begin, prose, links, error = status_block(tree, rel)
        if error is not None:
            text, line, message = error
            found.append(finding(rel, line, "status-block", text, message))
            continue

        board = [row for row in links if row[1] == "{BOARD}"]
        evidence = [row for row in links if row[1] == "{EVIDENCE}"]
        if len(links) != 2 or len(board) != 1 or len(evidence) != 1:
            found.append(finding(
                rel, begin, "status-block", "status-links",
                "status block must carry exactly one board link and one"
                " evidence link",
            ))
            continue

        normalized = "\n".join(prose)
        for raw, placeholder, _ in links:
            normalized = normalized.replace(raw, placeholder)
        compared[rel] = (begin, normalized)

    if len(compared) > 1:
        counts = Counter(value for _, value in compared.values())
        best = max(counts.values())
        if len(counts) > 1:
            for rel, (begin, normalized) in compared.items():
                if best == 1 or counts[normalized] < best:
                    found.append(finding(
                        rel, begin, "status-block", "status-block",
                        "status prose differs from the other documents",
                    ))
    return found


MATRIX_ROW = re.compile(r"^\|\s*(M\d+)\s*\|")
TABLE_SEPARATOR = re.compile(r"^\|[\s:|-]+\|$")


def table_cells(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def matrix_ids(tree):
    ids = set()
    for line in tree.lines(VERIFICATION) or []:
        match = MATRIX_ROW.match(line)
        if match:
            ids.add(match.group(1))
    return ids


def feature_kind(name):
    lowered = name.lower()
    if "client" in lowered:
        return "client"
    if "manager" in lowered or "local" in lowered:
        return "manager"
    if "server" in lowered:
        return "server"
    return None


def find_traceability_table(lines):
    """Return (start index, table lines) for the table with the three heads.

    The table is located by its header, not by a required section title, so
    the owner may place it wherever the document reads best.
    """
    index = 0
    while index < len(lines):
        if not lines[index].strip().startswith("|"):
            index += 1
            continue
        start = index
        block = []
        while index < len(lines) and lines[index].strip().startswith("|"):
            block.append(lines[index])
            index += 1
        header = [cell.lower() for cell in table_cells(block[0])]
        if (
            any("feature" in cell for cell in header)
            and any("requirement" in cell for cell in header)
            and any("matrix" in cell or "evidence" in cell for cell in header)
        ):
            return start, block
    return None, None


def check_traceability(tree):
    """docs/PROJECT maps the three primary features to R-IDs and M-rows.

    The table is the one place a reader can go from a feature to its
    requirement and its evidence row, so a missing row, requirement, or matrix
    citation is drift rather than style (DR12, DR13).
    """
    rel = "docs/PROJECT.md"
    lines = tree.lines(rel)
    if lines is None:
        return [finding(
            rel, 1, "traceability", "traceability-table",
            "cannot read docs/PROJECT.md",
        )]

    start, block = find_traceability_table(lines)
    if block is None:
        return [finding(
            rel, 1, "traceability", "traceability-table",
            "no table with Feature, Requirements, and Matrix columns",
        )]
    if len(block) < 2 or not TABLE_SEPARATOR.match(block[1].strip()):
        return [finding(
            rel, start + 1, "traceability", "traceability-table",
            "traceability table has no separator row",
        )]

    rows = [table_cells(line) for line in block[2:]]
    if len(rows) != 3:
        return [finding(
            rel, start + 1, "traceability", "traceability-rows",
            "traceability table has %d rows; expected three primary features"
            % len(rows),
        )]

    found = []
    kinds = []
    matrix = matrix_ids(tree)
    for offset, row in enumerate(rows):
        line_number = start + offset + 3
        kind = feature_kind(row[0] if row else "")
        kinds.append(kind)
        joined = " ".join(row)
        if kind is None:
            found.append(finding(
                rel, line_number, "traceability", "traceability-features",
                "traceability row does not name a primary feature",
            ))
        if not re.search(r"\bR\d+\b", joined):
            found.append(finding(
                rel, line_number, "traceability", "traceability-requirements",
                "traceability row has no requirement ID",
            ))
        cited = re.findall(r"\bM\d+\b", joined)
        if not cited:
            found.append(finding(
                rel, line_number, "traceability", "traceability-matrix",
                "traceability row has no verification matrix row",
            ))
        for row_id in cited:
            if matrix and row_id not in matrix:
                found.append(finding(
                    rel, line_number, "traceability", "traceability-matrix",
                    "traceability row cites unknown matrix row %s" % row_id,
                ))
    if set(kinds) != {"client", "manager", "server"}:
        found.append(finding(
            rel, start + 1, "traceability", "traceability-features",
            "traceability table must cover the client, manager, and server",
        ))
    return found


def phrase_allowed(rel, line):
    """Archived files, dated plans, evidence rows, and the decision record keep
    their wording.

    A decision entry states what was decided at the time; rewriting it to the
    new wording would erase the history DR3 protects, and the superseding entry
    is where the current rule lives.  A historical evidence row is the same
    shape.  Live prose in every other file is still checked, because that is
    where a stale claim reads as current.
    """
    if "/archive/" in "/" + rel:
        return True
    if is_dated_plan(rel):
        return True
    if rel == "docs/DECISIONS.md":
        return True
    if rel in ("docs/FIXES.md", "docs/VERIFICATION.md"):
        return line.lstrip().startswith("|")
    return False


def state_sheets(tree):
    found = []
    pattern = os.path.join(tree.root, "docs", "design", "*-states.html")
    for path in sorted(glob.glob(pattern)):
        found.append(os.path.relpath(path, tree.root).replace(os.sep, "/"))
    return found


def check_retired_phrases(tree):
    """A retired claim cannot return to a live document.

    The list is the audit's own: one active transfer, multiple server clients,
    pre-alpha, TO RETRY, an unqualified recursive-folder claim, and the old
    single-port wording.  The design states sheets are included because they
    are read as current UI copy (DR6, DR13).
    """
    found = []
    for rel in markdown_files(tree) + state_sheets(tree):
        lines = tree.lines(rel) or []
        if rel.endswith(".md"):
            stream = unfenced(lines)
        else:
            stream = list(enumerate(lines, 1))
        for number, line in stream:
            if phrase_allowed(rel, line):
                continue
            for label, pattern in RETIRED_PHRASES:
                if pattern.search(line):
                    found.append(finding(
                        rel, number, "retired-phrase", label,
                        "retired phrase is live: %s" % label,
                    ))
            if RECURSIVE_RE.search(line) and not QUALIFIER_RE.search(line):
                found.append(finding(
                    rel, number, "retired-phrase", "recursive folders",
                    "recursive-folder claim is not qualified as local, remote,"
                    " planned, or deferred",
                ))
    return found


CHECKS = (
    check_links,
    check_code_paths,
    check_fences,
    check_inventory,
    check_ownership,
    check_references,
    check_build_tokens,
    check_status_block,
    check_traceability,
    check_retired_phrases,
)


def collect_findings(tree):
    found = []
    for check in CHECKS:
        found.extend(check(tree))
    return sorted(
        found,
        key=lambda f: (f.file, f.line, f.check, f.text, f.message),
    )

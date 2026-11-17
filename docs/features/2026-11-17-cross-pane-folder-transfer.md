# Cross-pane folder transfer: recursive copy, verified move, and the report

**Date:** 2026-11-17
**Status:** v1 behavior, implemented on the host; the console rows M28-M32
are PENDING. This document summarizes the feature; the plan owns the
requirements and the units.

## Problem

The app had two panes and a working FTP client, but a folder could not cross
the network boundary. A folder had to be rebuilt one file at a time, a move
could not be expressed at all, and a failed copy left the user guessing what
arrived. The local panes already had COPY, MOVE, and PASTE; the network edge
spoke a different vocabulary (`FOLDER NOT SUPPORTED`).

## Users

- **Primary:** the console organizer. They move builds, saves, and media
  between the memory card and a server, and they expect a folder to behave like
  a folder.
- **Secondary:** the cautious mover. They choose MOVE and need the source to
  survive until the copy is proven.

## Behavior

- **A folder is one transfer.** DOWNLOAD on a remote folder, or UPLOAD on a
  local folder, walks the whole tree before the first byte, shows `SCANNING`
  with the folder count, and then copies item by item. An existing destination
  folder merges; the conflict question offers OVERWRITE ALL, SKIP EXISTING, or
  CANCEL, names up to 16 destinations and counts the rest.
- **Skipped entries do not stop the tree.** An unsafe name, a read-only
  destination subtree, a refused listing, or a folder over an existing file is
  skipped with its reason and the rest proceeds. A tree beyond 4,096 entries is
  refused before the first byte.
- **A move verifies before it deletes.** The move asks once, naming the roots
  and the entry count, and warns that any skip or unverified entry keeps every
  source. Each destination folder is re-listed; a file counts only when its
  exact name and its moved bytes match. The delete runs bottom-up, checks every
  source root first, stops at the first failure, and never removes a verified
  copy.
- **The report is the record.** `skipped.txt` lands in the folder above the
  transferred root, with the direction, both roots, every entry the transfer
  did not complete, and its reason. It is replaced on every transfer that
  reaches its copy phase; a cancel before the first byte writes none. A
  directory is never replaced, and a file the transfer itself wrote under that
  name takes `skipped 2.txt`.
- **Read-only is per folder.** A folder whose `perm` facts grant no write,
  create, delete, or mkdir dims UPLOAD, PASTE, MOVE, NEW FOLDER, RENAME, and
  DELETE with `READ ONLY`; DOWNLOAD and REFRESH stay live. A 550 fails only its
  entry, and a server that reports no `perm` stays enabled.
- **Free space is a preflight.** Every download sums its known bytes before the
  first byte and refuses a known shortfall with `NOT ENOUGH SPACE`. Only `ux0:`
  reports free space today; an unknown value skips the check and renders `--`.
- **The remote ledger has eight rows.** DOWNLOAD, MOVE, NEW FOLDER, RENAME,
  DELETE, REFRESH, DISCONNECT, SWITCH. MOVE reads `TO LOCAL PANE` and warns
  that the source is deleted after the copy verifies. While a local clipboard
  is armed, PASTE takes DOWNLOAD's slot. The panel scrolls in whole rows with
  an indicator. During a folder job the remote write rows and a folder open dim
  with `TRANSFER IN PROGRESS`; cursor, marks, and the view switch stay live.

## Scope

- Remote-to-remote copy is not offered; FTP has no copy command.
- A server that reports no file sizes can copy but not verify a move.
- The report is a record, not an import; retrying means selecting the entries
  by hand.
- No background transfer, sleep/resume, sync, or hidden-file toggle.

## Decisions

- [D100](../DECISIONS.md) — one folder job; the eight-row ledger supersedes
  D65's six-row maximum and its folder refusal.
- [D101](../DECISIONS.md) — the verified move and the report; supersedes R42's
  cross-volume rollback clause for cross-pane moves.
- [D102](../DECISIONS.md) — per-folder read-only and the free-space preflight.

## Plan

[`2026-11-17-1111-feat-cross-pane-folder-transfer-plan.md`](../plans/2026-11-17-1111-feat-cross-pane-folder-transfer-plan.md)
owns the Product Contract, KTD1 through KTD11, the eight units, and the
verification contract.

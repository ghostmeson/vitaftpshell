# Remote clipboard: copy from the server, paste into either local view

**Date:** 2026-11-18
**Status:** planned; implementation not started. The console rows are PENDING.
This document summarizes the feature; the plan owns the requirements and the
units.

## Problem

The clipboard held local paths only. Every remote action decided its destination
before the user acted: DOWNLOAD and MOVE both landed in the left local view. A
user who wanted a server folder in the right local view had to park the left pane
there first, walk back to the server, and download. The remote pane therefore
read as a place you visit and issue commands from, while the local panes behaved
like a file system. The gap was a missing verb, not a missing model: COPY,
PASTE, and CLEAR already existed for local work.

## Users

- **Primary:** the console organizer moving builds, saves, and media between a
  server and either local folder, who wants the destination chosen after the
  source.
- **Secondary:** the cautious mover, who needs a copy to stay a copy and a
  destructive set to stay one press with its verification.

## Behavior

- **One clipboard, one origin.** COPY on either pane freezes the marked set, or
  the row under the cursor, together with each entry's type, size, and whether
  the size was known. The set is tagged local or remote. Arming a new set
  replaces the old one without a prompt; the dock band announces it.
- **A remote set pastes as a download.** PASTE on a local pane copies every
  frozen entry into the current folder. A folder recurses, an existing
  destination merges, and the conflict question, the free-space preflight, the
  destination protection table, and the one-folder-job rule apply unchanged.
- **A remote set cannot paste onto the server.** PASTE on the remote pane is dim
  with `SAME SERVER`. There is one session, no FXP, and no relay through local
  temp.
- **COPY keeps, MOVE clears.** A copied set stays armed after a paste, so one
  clip can feed several folders. A moved set clears, because a second paste would
  target sources that are already gone.
- **The session owns the clip.** A disconnect, a session loss, or a reconnect
  clears a remote-origin clipboard and says so. A path frozen against a dead
  session can copy the wrong bytes and, for a move, delete them.
- **The remote ledger gains two rows.** With no clip: DOWNLOAD, COPY, MOVE, NEW
  FOLDER, RENAME, DELETE, REFRESH, DISCONNECT, SWITCH. While a set is armed:
  PASTE first, then COPY, MOVE, CLEAR, then the remaining rows. The panel scrolls
  in whole rows with an indicator.
- **MOVE stays one-shot.** A remote MOVE remains the verified move to the left
  local view: copy, re-list, match, delete bottom-up, and write `skipped.txt`. It
  does not arm, because its destination is already implied and its destructive
  contract is the point.

## Limits

- No remote-to-remote copy or move. The refusal is visible, not hidden.
- No unified namespace: the server is still addressed through its pane, and a
  path is never typed.
- A file that changes on the server between arm and paste copies its current
  bytes. Its frozen size also bounds the single-file free-space check, so a
  file that grew can pass the check and then fill the card mid-transfer; a
  folder paste re-sums during its scan.

## Evidence

Host units land before the console run. The console row and the revised ledger
row are recorded in `../VERIFICATION.md`; until they pass, this feature is
implemented on the host only.

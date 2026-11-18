# Local workspace: second pane, all volumes, and list navigation

**Date:** 2026-11-17
**Status:** v1 behavior, implemented on the host; matrix rows M24-M27 are
pending and the `imc0:` leg is BLOCKED on the PCH-1000. This document summarizes
the feature; the plan owns the requirements and the units.

## Problem

The app is a console-first workspace, but its local side was narrower than the
console. It saw three mounts and one local window, so a user with an internal
card, a game card, or a reason to inspect the app folder needed another tool. A
user moving files between two folders had to walk the single window back and
forth, losing the source selection and the place in the list.

Small frictions made every navigation cost more than it should. Connecting from
SITES left the user on the site list after the connection succeeded. The stick
scrolled the view while the cursor stayed behind, so the next D-pad press
jumped the view back. Lists stopped dead at the first and last rows. Copying a
file into the folder it came from silently created a copy name without telling
the user.

## Users

- **Primary:** the local organizer. They move builds, saves, and media between
  the memory card, a game card, USB storage, and the app's own folder, and they
  want the console to be the only file manager they need.
- **Secondary:** the anonymous-first collector. A successful connect now lands
  in the remote view, so the site list is a door rather than a detour.

## Behavior

- `ALL DRIVES` lists `ux0:`, `ur0:`, `uma0:`, `imc0:`, `gro0:`, `grw0:`, and
  `app0:` in that order when mounted. An absent mount is an absent row. Save
  data (`savedata0:`) and the protected system partitions (`vs0:`, `os0:`) never
  appear. The list is built on entry, so a card inserted or removed while it is
  open appears on the next entry.
- `gro0:` and `app0:` are read-only. The row value reads `DRIVE RO`, and a
  write, create, paste, rename, move, delete, or download destination there is
  refused with `READ-ONLY MOUNT`; COPY and a read stay allowed.
- The right pane can become a second local browser from ACTIONS. The left pane
  is always local and stays the anchor. The switch is a view change, not a
  disconnect: the FTP session, its folder, cursor, scroll, marks, and a running
  transfer survive it.
- Each view keeps its own folder, cursor, scroll, marks, and listing. The right
  local view opens at `ALL DRIVES` the first time, then returns to its last
  folder for the session. A folder that disappears falls back to `ALL DRIVES`
  when the failure is found.
- Both local panes share one clipboard. A download targets the left local view;
  an upload targets the remote view only while it is shown. With both panes
  local, UPLOAD is dim on both with `NO REMOTE VIEW`, and the dock never prints
  `REMOTE`.
- A paste whose items all already live in the destination asks CANCEL or SAVE
  DUPLICATES. SAVE DUPLICATES uses the copy rule (`A copy.TXT`, then
  `A copy 2.TXT`) and never overwrites the source; a folder duplicates under its
  copy name. Any other paste with an existing destination asks the one
  OVERWRITE ALL / SKIP EXISTING / CANCEL question, counts only real
  replacements, and names already-here items as staying and skipped.
- A successful connection started from SITES closes SITES and returns to the
  WORKSPACE with the remote view active and its listing requested. A failure of
  every class stays on SITES with its reason; a success after the user left
  SITES switches the view without stealing the screen or focus.
- The stick moves the selection by its deflection, one row at the deadzone edge
  to five at full deflection, at the 100 ms cadence, and the view follows. The
  workspace list and the site book wrap at both ends; Down on the last workspace
  row enters the dock only while a lane has an active or queued item.
- The switch is an ACTIONS row only on the right pane: `SWITCH TO LOCAL` on the
  remote view, `SWITCH TO REMOTE` on the local view. The right panel holds seven
  rows and both panels grow to 360 px.

## Requirements

The [local workspace plan](../plans/archive/2026-11-16-2011-feat-local-workspace-plan.md)
owns the numbered requirements, R1 through R26:

- **Volumes:** the seven-mount list, the read-only value and refusal, the
  protected roots, free space, and the server's unchanged three-mount
  exposure.
- **Second local pane:** the switch rows, preserved view state, the full local
  peer, the shared clipboard, the both-local refusals and hint, the connect
  return, and the seven-row panel.
- **Same-folder paste:** duplicate or cancel for an all-same-folder paste, the
  mixed-batch question, the already-here skip, and no self-copy.
- **List navigation:** the stick step, workspace wrap, the dock entry, and the
  site-book wrap and window reset.

## Units

The local workspace plan's units:

| Unit | Outcome |
|---|---|
| U1 | One volume table with server-exposure, read-only, and protected-data flags; `PATH_READ_ONLY`; the `app0:` alias; the server filter |
| U2 | `ALL DRIVES` from the table, `DRIVE RO`, and the read-only action refusals |
| U3 | The three-view state model and the third listing, marks buffer, and row arena |
| U4 | The two switch actions, the seven-row panel, and the both-local dock hint |
| U5 | The connect return, the failure reasons, and the screen-key order |
| U6 | The stick step, list wrap, the dock entry, and the site-book wrap |
| U7 | The duplicate confirm, the mixed batch, and the one copy-name owner |
| U8 | The exact-parent cross-view reload and the vanished-folder fallback |
| U9 | This document, the four decisions, the design sheet, the v1 plan corrections, and the matrix rows |

## Evidence

| Matrix row | Proves |
|---|---|
| M24 | The seven mounts, the read-only refusals, the absent rows, and the save-data exclusion; the `imc0:` leg is BLOCKED on the PCH-1000 |
| M25 | The second local view, the switch, preserved state, the shared clipboard, the both-local dock, and the cross-view reload |
| M26 | The connect return and the reason for every failure class |
| M27 | The stick, the workspace wrap, the dock entry, and the site-book window |

Host suites cover the same behavior now: `test_path`, `test_server`,
`test_fsop_plan`, `test_uistate`, `test_handoff`, `test_input`,
`test_integration`, `test_app_thread`, `test_render_workspace`, and
`test_render_screens`. A host pass is not a console claim; the rows stay
PENDING until the code-freeze pass (D95).

## Decisions

- **D96** — the local panes list every user-accessible mount; the server keeps
  its fixed three. Supersedes the three-volume local scope in D53 and the live
  summaries.
- **D97** — the right pane's local and remote views over one FTP session, the
  left local anchor, preserved per-view state, and the session-only mode.
- **D98** — the same-folder duplicate question and the mixed-batch skip.
  Supersedes D60's silent same-folder copy and its cross-pane-movement clause.
- **D99** — `app0:` is a comparison-only alias for `ux0:app/VFTPSHELL`; the
  install tree stays writable through its `ux0:` spelling. The platform equality
  is `[C]` until the console confirms it.

## Scope

Deferred: sleep prevention and resume integrity, remote recursive transfer,
persisting the pane mode or either pane's folder, a third pane or a second FTP
session, exposing the new mounts through the FTP server, live mount polling, and
write protection for the installed-app tree beyond the alias comparison.

Outside the product: a PC-first workflow, and SFTP, FTPS, HTTP, WebDAV, or SMB.

## Plan

[`2026-11-16-2011-feat-local-workspace-plan.md`](../plans/archive/2026-11-16-2011-feat-local-workspace-plan.md).

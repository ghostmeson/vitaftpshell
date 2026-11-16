---
title: Local Workspace: Second Pane, All Volumes, and List Navigation - Plan
type: feat
date: 2026-11-16
topic: local-workspace
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-brainstorm
execution: code
---

# Local Workspace: Second Pane, All Volumes, and List Navigation - Plan

## Goal Capsule

- **Objective:** A Vita user can do all local file work from the console: both panes browse local storage, every user-accessible mount is reachable, a successful connect lands in the workspace, and list navigation keeps the cursor with the view and wraps at the ends.
- **Means:** A second local view in the right pane behind an ACTIONS switch, an expanded volume allowlist, a connect-success return from SITES, and selection-following stick input with list wrap.
- **Product authority:** `STRATEGY.md` (the console is the workspace), `docs/DESIGN.md` (layout and input), and `docs/DECISIONS.md` D53, D60, D61, D69, D72, and D85.
- **Open blockers:** None.
- **Not active scope:** Sleep and resume integrity and remote recursive folder transfer are separate plans; the FTP server's exposed volume set is unchanged.

---

## Product Contract

### Summary

The workspace becomes complete for local work. ALL DRIVES lists every user-accessible mount the console has, the right pane can become a second local browser, a successful connect returns to the workspace, and list navigation follows the cursor and wraps.

### Problem Frame

The app is a console-first workspace, but its local side is narrower than the console. It sees three mounts and one local window, so a user with an internal card, a game card, or a reason to inspect the app folder has to reach for another tool. A user moving files between two folders must also walk the single window back and forth, losing the source selection and the place in the list.

Small frictions make every navigation cost more than it should. Connecting from SITES leaves the user on the site list after the connection already succeeded. The stick scrolls the view while the cursor stays behind, so the next D-pad press jumps the view back to the top. Lists stop dead at the first and last rows.

Copying a file into the folder it came from is the sharpest case: the destination already exists and is the source, so the current overwrite-or-skip prompt has no answer that does not write the file onto itself.

The result is that ordinary file organization feels like a transfer tool with a browser attached, and the console cannot yet be the only workspace for local files. That matters because "the console is the workspace" is the product's reason to exist, not a convenience.

### Key Decisions

- **The right pane is the only switchable pane.** One FTP session and one remote state are easier to reason about than two, and the left pane stays a stable local anchor. Governs R7, R8, and R15.
- **Switching is a view change, not a disconnect.** The session and both views keep their folder, cursor, scroll, and marks, and a running transfer continues. Governs R8, R9, and R13.
- **The second local view is a full peer of the left pane.** It carries the same actions, marks, free space, and protection, and it shares one clipboard with the left pane. Governs R10, R11, and R12.
- **The switch is an ACTIONS row, so the panel grows to seven rows.** The user asked for the switch in ACTIONS, and the sixth-row cap is a layout constraint rather than a product rule. Governs R14.
- **ALL DRIVES becomes volume-complete for user mounts while the server keeps its writable three.** A file manager should reach the console's storage; an anonymous server should not gain new writable surfaces by accident. Governs R1 through R6.
- **Same-folder paste asks duplicate or cancel; any other existing destination keeps the existing prompt.** Overwrite cannot change a file onto itself, and a batch with both kinds of collision should not stack two prompts. Governs R16 through R20.
- **Connect success returns to the workspace, but only from SITES.** The connect is the user's request to see the remote, and a failure should keep the list in front for an edit or retry. Governs R21 and R22.
- **The stick moves the selection and the workspace lists wrap.** The stick is a second navigation control, not a detached scroll; a list that stops at the end forces a trip back through every row. Governs R23 through R25.
- **The local-local mode is session state.** Each launch starts local plus remote, because the remote is half the product; the mode is not persisted. Governs R15.
- **The v1 plan's stale input text is corrected, not the bindings.** The code and `DESIGN.md` already agree that Triangle opens ACTIONS and Select opens SITES; only the plan text is wrong. Governs R26.

```mermaid
stateDiagram-v2
    [*] --> Remote
    Remote --> Local: SWITCH TO LOCAL
    Local --> Remote: SWITCH TO REMOTE
    Local --> Remote: connect from SITES
    note right of Remote : kept: folder, cursor, scroll, marks, session, transfer
    note right of Local : kept: folder, cursor, scroll, marks
```

### Requirements

**Volumes**

- R1. ALL DRIVES lists `ux0:`, `ur0:`, `uma0:`, `imc0:`, `gro0:`, `grw0:`, and `app0:` when mounted; an absent mount is absent, not an error row, and no system-only mount appears.
- R2. `app0:` and `gro0:` are read-only: the row carries a read-only mark, and a write, rename, move, or delete there is refused with a reason and changes nothing.
- R3. Every listed volume root is protected: a rename, move, or delete of the root is refused by name. The system data root of a listed volume, at minimum `imc0:data`, is protected like `ur0:data` and `ux0:data`.
- R4. Free space follows the volume; a value the platform cannot report renders `--`.
- R5. The FTP server exposes only `ux0:`, `ur0:`, and `uma0:`; a server path on another mount is refused and creates nothing.
- R6. Path canonicalization accepts the new mount tokens in every role while keeping the same normalization and case rule, and the protected-path table grows to cover the volume roots and data roots R3 names. It also maps `app0:` to its install tree for same-path comparisons, so a copy or move between the two spellings is treated as the same folder.

**Second local pane**

- R7. ACTIONS on the right pane offers `SWITCH TO LOCAL` on the remote view and `SWITCH TO REMOTE` on the local view; the left pane offers no switch.
- R8. A switch changes only which view the right pane shows. The FTP session, its folder, cursor, scroll, marks, and any running transfer are preserved, and switching back restores the view.
- R9. Each right-pane view keeps its own folder, cursor, scroll, and marks. The local view opens at ALL DRIVES the first time, then returns to its last folder for the session, or to ALL DRIVES when that folder no longer exists. When a local operation completes, any other local view showing an affected folder reloads, keeps its cursor on the same name where it survives, and prunes marks for names that are gone; a view showing an unaffected folder stays as it is.
- R10. The right local view is a full local browser: the same actions, marks, free space, protection, and clipboard behavior as the left pane, subject to R12.
- R11. The clipboard is shared: COPY or MOVE in either pane, a pane switch, and PASTE in the other pane paste into the active pane's folder.
- R12. While both panes are local, UPLOAD is disabled on both with `NO REMOTE VIEW`, Cross on a file or marked set performs no transfer and reports the same reason, and the idle dock hint does not advertise an upload; the switch row stays live.
- R13. A successful connection from SITES switches the right pane to remote and makes it active.
- R14. The ACTIONS panel on the right pane holds seven rows, with the switch row last, after DELETE on the local view and after DISCONNECT on the remote view; the left pane keeps six rows. Both panels grow to 360 px tall (bottom 436, 4 px above the dock), keeping the 44 px row pitch and the target panel aligned.
- R15. Each launch starts with the left pane local and the right pane remote and disconnected; the pane mode is not persisted. With both panes local and no session, the idle dock names the active local pane, its path, and its marks, and never prints `REMOTE`.

**Same-folder paste**

- R16. A paste whose items all already live in the destination folder asks SAVE DUPLICATES or CANCEL; CANCEL changes nothing.
- R17. SAVE DUPLICATES names each item with the existing copy rule (`A copy.TXT`, then `A copy 2.TXT`) and creates no overwrite.
- R18. A same-folder MOVE stays refused with `ALREADY IN THIS FOLDER`.
- R19. Any other paste with at least one existing destination asks the existing OVERWRITE ALL, SKIP EXISTING, and CANCEL prompt once; items already in the folder count as existing, that batch offers no duplicate choice, and an already-here item in a mixed MOVE stays in place and counts as skipped instead of failing the batch.
- R20. The same-folder question is decided by the destination path, so it fires in either pane, including when the paste is in the other local pane at the folder the items came from.

**Connect return**

- R21. On a successful connection started from SITES, SITES closes and the WORKSPACE returns with the remote pane active and its first listing loading.
- R22. On a failed connection, SITES stays in front with the reason so the user can edit or retry.

**List navigation**

- R23. The left stick moves the active pane's selection, and the view scrolls to keep the selection visible; it never scrolls the view independently of the cursor.
- R24. The workspace row list wraps: Up on the first row selects the last, and Down on the last selects the first, except that Down on the last row focuses the transfer dock when a transfer is active or queued, as it does today.
- R25. Wrap and stick-following apply to the workspace panes; SITES, ACTIONS, CONFIRM, and the dock's Down entry keep their current behavior.

**Behavior source**

- R26. The implementation corrects the behavior documents: the v1 plan's stick sentence and its Triangle/Select sentence; DESIGN's six-row cap, ACTIONS geometry, ALL DRIVES paragraph, and no-session dock state; AGENTS.md's status and settled-scope sentences; README.md; and docs/PROJECT.md. The server's volume sentences stay as they are.

### Key Flows

- F1. Switch the right pane to local.
  - **Trigger:** The right pane shows remote and the user opens ACTIONS.
  - **Steps:** Choose `SWITCH TO LOCAL`; the pane shows ALL DRIVES; the FTP session stays connected.
  - **Outcome:** A second local view at ALL DRIVES, the left pane unchanged, and ACTIONS now offering `SWITCH TO REMOTE`.
  - **Covers:** R7, R8, R9, R14.
- F2. Copy between two local folders.
  - **Trigger:** A file is copied or marked in the left pane and the right pane is local at another folder.
  - **Steps:** COPY; L or R to the right pane; navigate; PASTE.
  - **Outcome:** The item is copied into the active pane's folder, and both panes keep their own folders and selections.
  - **Covers:** R10, R11.
- F3. Paste into the folder the items came from.
  - **Trigger:** PASTE where every item already lives in that folder.
  - **Steps:** SAVE DUPLICATES or CANCEL.
  - **Outcome:** Duplicates with the copy rule, or nothing changed.
  - **Covers:** R16, R17, R20.
- F4. Connect from SITES.
  - **Trigger:** Select opens SITES; the user connects to a site.
  - **Steps:** The connection succeeds; SITES closes; the workspace returns with the remote pane active.
  - **Outcome:** The remote view is loading its first listing. A failure stays on SITES with the reason.
  - **Covers:** R13, R21, R22.
- F5. Navigate with the stick and wrap.
  - **Trigger:** The stick is held in a workspace list.
  - **Steps:** The selection moves and the view follows; at the end of the list, the selection wraps.
  - **Outcome:** The cursor is always visible and never left behind by a scroll.
  - **Covers:** R23, R24.

### Acceptance Examples

- AE1. Switch preserves remote state.
  - **Given:** The right pane is connected at `/pub` with two marks and a running download.
  - **When:** `SWITCH TO LOCAL`, browse, then `SWITCH TO REMOTE`.
  - **Then:** `/pub`, the marks, and the download are as they were; the dock still reports the transfer.
  - **Covers:** R8, R9.
- AE2. Read-only mount refuses a mutation.
  - **Given:** `app0:` is open and a file is under the cursor.
  - **When:** DELETE or RENAME is chosen.
  - **Then:** The action is refused with the read-only reason, and nothing changes.
  - **Covers:** R2.
- AE3. Absent mount is absent.
  - **Given:** No USB storage and no game card.
  - **When:** ALL DRIVES opens.
  - **Then:** `uma0:`, `gro0:`, and `grw0:` are absent rows, not errors; the present mounts are listed.
  - **Covers:** R1.
- AE4. Same-folder duplicate.
  - **Given:** `A.TXT` is copied and pasted into its own folder.
  - **When:** SAVE DUPLICATES is chosen.
  - **Then:** `A copy.TXT` is created and `A.TXT` is untouched; a second run creates `A copy 2.TXT`.
  - **Covers:** R16, R17, R20.
- AE5. Mixed paste.
  - **Given:** The clipboard holds `ux0:data/A.TXT` and `ux0:other/B.TXT`, and `ux0:data/B.TXT` already exists; the destination is `ux0:data`.
  - **When:** PASTE runs.
  - **Then:** One OVERWRITE ALL, SKIP EXISTING, and CANCEL prompt appears; OVERWRITE leaves the already-here `A.TXT` unchanged and replaces `B.TXT`, SKIP leaves both, and no duplicate is created.
  - **Covers:** R19.
- AE6. Connect success and failure.
  - **Given:** SITES holds a good site and a site with a bad password.
  - **When:** The good site connects.
  - **Then:** SITES closes and the workspace returns with the remote pane active and loading.
  - **When:** The bad site connects.
  - **Then:** SITES stays with the login reason.
  - **Covers:** R13, R21, R22.
- AE7. Stick and wrap.
  - **Given:** A 40-row folder with the cursor on the first row.
  - **When:** The stick is held down.
  - **Then:** The cursor moves with the view; after the last row, the next step selects the first, and Up from the first selects the last.
  - **Covers:** R23, R24.
- AE8. Upload while both panes are local.
  - **Given:** Both panes show local folders.
  - **When:** ACTIONS opens on the left pane.
  - **Then:** The panel keeps six rows, UPLOAD is dim with `NO REMOTE VIEW`, and no switch row appears.
  - **When:** ACTIONS opens on the right pane.
  - **Then:** UPLOAD is dim, and `SWITCH TO REMOTE` is the seventh row.
  - **Covers:** R7, R12, R14.
- AE9. Wrap with a transfer running.
  - **Given:** A transfer is active and the cursor is on the last row.
  - **When:** Down is pressed.
  - **Then:** The dock takes focus instead of wrapping; Up from the first row still selects the last.
  - **Covers:** R24, R25.
- AE10. Cross with both panes local.
  - **Given:** Both panes are local and a hidden remote session is connected.
  - **When:** Cross is pressed on a local file.
  - **Then:** No transfer starts, the status line says `NO REMOTE VIEW`, and the switch row is the way back.
  - **Covers:** R12.

### Success Criteria

- A user can copy or move a file between two local folders without changing the source pane's folder after the copy.
- Both panes list the same mounted volumes, and a write on a read-only mount changes nothing.
- A successful connect always lands in the remote view; a failed one stays on SITES with the reason.
- The cursor is never left behind by a scroll, and a workspace list wraps at both ends except for the dock entry when a transfer can be cancelled.
- The existing host suites pass, new coverage proves these requirements, and the hardware matrix gains rows for the volumes, the second pane, the connect return, and list navigation.

### How This Work Fits Together

<!-- ce-section: work-relationships -->

This plan covers the local workspace. The broader breakdown is the current understanding, not a committed roadmap.

- Power and resume integrity is a separate plan. It shares the activity signals this plan touches (server ON, transfers, local jobs) but can proceed independently.
- Remote recursive folder transfer is a separate requirement. It shares the remote pane and the transfer queue, and it is independent of this plan.
- The hardware matrix grows with rows for this plan; the code-freeze pass runs them with the rest.

### Scope Boundaries

**Deferred for later**

- Sleep prevention and resume integrity, including the server's behavior across a suspend.
- Remote recursive folder transfer.
- Persisting the pane mode or either pane's folder across launches.
- A third pane, two FTP sessions, or two remote views.
- Exposing the new mounts through the FTP server.

**Outside this product's identity**

- A PC-first workflow or desktop-client port.
- SFTP, FTPS, HTTP, WebDAV, or SMB.

### Dependencies / Assumptions

- The mount set is runtime: `imc0:` exists on PCH-2000 only, `gro0:` and `grw0:` need a game card, and `uma0:` needs USB storage; `app0:` is always present.
- `app0:` and `gro0:` are read-only; `grw0:` is expected writable but unconfirmed, so a refused write there surfaces the platform reason rather than a read-only promise.
- The ACTIONS panel can grow to seven rows, and the design update belongs to this work.
- The v1 plan's stick and binding text is stale; the code and `DESIGN.md` already describe the behavior this plan requires.
- The settings store keeps one `last_local`; the right local view's folder is session-only.
- No kernel plugin, daemon, or boot component is added.
- The implementation records two superseding decisions: one for the volume scope, because the settled v1 scope names three volumes, and one for D60's cross-pane-movement and silent-same-folder-copy clauses, which R11 and R16 replace.

### Outstanding Questions

- Resolve before planning: none.
- Deferred to planning: the volume labels and the read-only mark's copy and glyph; the switch row's title, target, and description; the duplicate prompt's title and warning; and whether `grw0:` is writable on this firmware.

### Sources / Research

- `docs/DESIGN.md` — pane identity, the ACTIONS panel geometry, the six-row cap, the clipboard states, and the input legend.
- `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` — the stick sentence, the Triangle/Select sentence, the three-volume local-pane sentence, and the server exposure sentence.
- `docs/DECISIONS.md` — D53 (ALL DRIVES startup), D60 (the clipboard), D61 (destructive confirms), D69 (copy/move source), D72 (two lanes), D85 (path-scoped server refusal).
- `src/core/uistate.c` — the pane pairing, the action sets, marks, the stick handler, and the session events.
- `src/core/fsop_plan.c` — same-folder move refusal and the copy-name rule.
- `src/ui/actions.c` — the 44 px row loop and the six visible slots.
- `src/app/worker.c` — the hardcoded volume list and the settings fields.
- `agent-wiki/11-reference.md` — the filesystem mount table.
- `AGENTS.md` — the settled three-volume scope.

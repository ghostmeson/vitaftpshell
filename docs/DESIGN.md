# Design

**Status:** all screens are settled: WORKSPACE, SITES, SITE EDITOR, SERVER,
ACTIONS, CONFIRM, and TEXT ENTRY. The design phase is complete and the
architecture plan is written. Two amendments landed after the close, both
user-directed: the risk mark and the high-risk confirmation (D30, D31), and the
2000-row cap's `FIRST 2000` notice. The fixed constraints below bind every screen.

## Visual authority

The visual language is [`vita-cockpit/docs/DESIGN.md`](../../vita-cockpit/docs/DESIGN.md).
That document is binding. This project inherits its palette, type scale, spacing
scale, panel anatomy, and restraint rules. If this document and the COCKPIT
document disagree, the COCKPIT document wins unless a numbered decision here
supersedes it.

## Fixed now

- **Screen:** 960×544, no scaling. The Vita's display is the only target.
- **Field:** true black `C_FIELD`. Panels use `C_PANEL`.
- **Palette:** `C_FIELD`, `C_PANEL`, `C_HAIR`, `C_DIM`, `C_WHITE`, `C_YELLOW`,
  `C_CYAN`, `C_GREEN`, `C_GREEN_DIM`, `C_RED`. Yellow is the alert channel.
  Red is not an alert.
- **Type:** the hand-built 5×7 bitmap face. Uppercase, ASCII labels, `..` for
  truncation. Sizes come from the COCKPIT ladder; do not invent a size between
  steps. The face has no `?` or `!`; a UI title is a statement (`DELETE 3 ITEMS`,
  `DISCARD CHANGES`).
- **Spacing:** `SP_1`–`SP_6` from the COCKPIT theme. A gap off the scale is a
  bug.
- **Panel language:** satellite panels get one chamfer; the hero treatment is
  reserved for the workspace's primary region. Brackets mark the hero only.
- **Restraint:** no icon that encodes nothing, no second alert colour, no glow
  except the wordmark and hazard band, no animation beyond progress, scrolling,
  and a running-state cue.
- **Numbers:** a metric or size the platform cannot report renders `--`. A readout
  is never a guess.

## WORKSPACE

Direction A: two equal ledgers with a full-width dock (D17). The six states are
in [`design/2026-11-08-workspace-a-states.html`](design/2026-11-08-workspace-a-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Header | 0 | 0 | 960 | 64 |
| Local pane | 18 | 76 | 456 | 352 |
| Remote pane | 486 | 76 | 456 | 352 |
| Transfer dock | 18 | 440 | 924 | 64 |
| Control legend | 18 | 510 | 924 | 16 |

Margin 18; pane gutter 12; dock gap 12; legend gap 8. Panes are satellites: a
14 px top-right chamfer, `C_PANEL` fill, 1 px outline at 45 % cyan.

### Pane

- **Band:** title at `y+8` (px 2, tracked 2); path at `y+26` (px 1.5); a hairline
  at `y+43`; rows from `y+48`.
- **Row:** 30 px pitch; ten visible. Name at `x+40` (px 2). Type or size
  right-aligned at `x+w-14` (px 1.5): `DIR`, `UP`, a size, or `--`. The mark
  square is 6×6 at `x+19`; the risk diamond is 6×6 at `x+27`; the cursor bar is
  2 px at `x+8`; the scrollbar is 2 px at `x+w-6`. The name column starts at
  `x+40` on every row so names stay aligned when a risk mark appears.
- **Risk mark:** a path the shared risk table classifies high draws a 6×6 yellow
  diamond at `x+27` (D31). It marks the `tai` folders and their contents, volume
  roots, the `ur0:data` and `ux0:data` folders themselves, and the files that
  control loading. The mark never replaces the type or size column.
- **Truncated listing:** a directory longer than the 2000-row cap shows
  `FIRST 2000` in yellow on the band, left of the mark count. The rows shown are
  the first 2000 in the source's order.
- **Values:** a known size is white; `--`, `DIR`, and `UP` are dim. A size the
  source does not report is never shown as zero.
- **Active pane:** a 2 px yellow left edge and a yellow title. The inactive pane
  keeps a dim title, a `C_CYAN_DIM` path, a `C_HAIR` cursor bar, and a 2.5 %
  wash. The active cursor is a 2 px white bar over a 6 % wash.
- **Marks:** pane-scoped. The band shows the count in yellow and the total known
  size in white. Marks survive directory changes, L/R, and a completed batch
  (D18); they clear when the pane's connection or volume root changes, or when
  the user unmarks each row.
- **Identity:** the remote band shows the site and path; the local band shows
  the path and free space. The site identity also stays in the header (D19).
- **Truncation:** names and paths end in `..`. A path too deep for the band is
  shown in full in ACTIONS, which is still to be designed.

### Header

| Item | Position | Type |
|---|---|---|
| Wordmark `VITAFTPSHELL` | 18, 8 | px 4, yellow, COCKPIT halo and chroma |
| Screen name | 18, 42 | px 2, tracked 1, white |
| Site identity | right 942, y 22 | host px 2 cyan, `:21` micro `C_CYAN_DIM` |
| Connection state | right 942, y 25.5 | micro, green `CONNECTED` or dim `NOT CONNECTED` |
| Server | right 942, y 45.5 | micro: `SERVER` dim, `ON` green / `OFF` dim, `n CLIENT(S)` white |

The wordmark is the only loud element; the halo and chroma are the only glow
allowed outside the hazard band.

### Transfer dock

- **Geometry:** 64 px strip. State word at 32, 450 (px 2, tracked 2); subject at
  +16 (px 2); path micro cyan; right readouts right-aligned at 928 (micro); a
  4 px bar at y 473 spanning 32..928; line three at 483 (micro).
- **Idle:** `IDLE` dim; the active pane and path; the mark count and total on the
  right; the action hint (`X TO DOWNLOAD n ITEMS`) on line three.
- **Active:** `DOWNLOAD` or `UPLOAD` yellow; filename; `n OF m`; a yellow
  continuous bar; transferred and total bytes, percentage, and rate. When the
  total is unknown, the total and percentage render `--`.
- **Done:** `DONE` green; filename; `n OF m`; a full yellow bar; size and elapsed
  time; average rate.
- **Failed:** `FAILED` yellow; filename; `n OF m`; the bar stops at the failure
  point in dim; the reason in white; `NO PARTIAL KEPT` dim. A data failure ends
  with `X TO RETRY`; a control failure leaves the reconnect to the pane.
- **No session:** `IDLE` dim; `REMOTE`; `NO MARKS`; `NO SELECTION`.
- **Focused dock:** only focusable when a transfer is active or queued. Down from
  the last row enters it; the 1 px outline turns white; the legend becomes
  `[X] CANCEL  [O] TO LIST`; Cross opens the cancel CONFIRM; Circle returns to
  the last row. The dock’s state colours do not change.
- The terminal state stays until the next transfer starts (R67). The dock is
  never modal and never covers a row.

### Pane states

- **Reading:** `READING` white px 2, centred in the list area. The band keeps the
  path being read.
- **Empty:** `EMPTY FOLDER` dim px 2, centred.
- **Session lost:** `SESSION LOST` yellow px 2; the mapped reason in micro dim;
  `X RECONNECT  O DISMISS` in micro with the keys white and the words dim. The
  block is centred; the band keeps the last path dim; marks clear; the dock keeps
  the failed transfer.
- **Data-channel failure with a healthy control channel:** the rows stay; the
  dock shows FAILED and the retry; the session stays connected.

### Input

- L/R switches the active pane and preserves both path and selection.
- Cross opens a directory, or transfers the selected file or marked set.
- Circle goes to the parent; at a volume root or `/` it says so.
- Square marks; Triangle opens SITES; Start opens SERVER; Select opens ACTIONS.
- Down from the last row enters the dock only when a transfer is active or
  queued.
- On the session-lost state, Cross reconnects and Circle dismisses.

### Legend

- Standard: `[X] OPEN/SEND  [O] UP  [□] MARK  [△] SITES  L/R PANE  START SERVER
  SELECT ACTIONS`.
- Reconnect: `[X] RECONNECT  [O] DISMISS  L/R PANE  START SERVER  SELECT ACTIONS`.

## SITES

Direction C: a hero card for the selected site above a full table (D20). The
five states are in
[`design/2026-11-08-sites-c-states.html`](design/2026-11-08-sites-c-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Header | 0 | 0 | 960 | 64 |
| Selected-site card | 18 | 76 | 924 | 110 |
| Site table | 18 | 198 | 924 | 230 |
| Dock | 18 | 440 | 924 | 64 |
| Control legend | 18 | 510 | 924 | 16 |

### Card

- 28 px hero chamfer, brackets on the two square corners.
- `SELECTED SITE` at 32, 86 (micro, dim); the site name at 32, 100 (px 3); the
  host:port at 32, 128 (micro, cyan).
- `LOGIN` label at 600, 100; the value right-aligned at 928. `PASSWORD` at 600,
  130; the value right-aligned at 928. Values are px 2 and white.
- Footer at 160: a login site says `PASSWORDS ARE STORED UNENCRYPTED`; an
  anonymous site says `NO PASSWORD STORED`; a failed login says
  `LOGIN FAILED  -  <reply>` in yellow. `SQUARE TO EDIT` is right-aligned on the
  same line.
- The card is a preview; editing happens on SITE EDITOR.

### Table

- `ALL SITES` at 32, 206 (px 2, tracked 2); the count right-aligned at 928
  (micro, dim): `n SAVED`, or `n IN MEMORY` when saving is unavailable.
- A hairline at 241; six 30 px rows from 246.
- Each row: name at `x+32` (px 2, white), host:port at `x+300` (micro,
  `C_CYAN_DIM`), `ANON` (dim) or `LOGIN` (cyan) right-aligned at `x+w-14`. The
  selected row gets a 2 px white bar and a 6 % wash. A scrollbar appears only
  when the book exceeds six rows.
- Long names and hosts end in `..`; the editor holds the full values.

### States

- **First run:** one seeded `FTP.GNU.ORG` row, selected, anonymous. Nothing
  auto-connects.
- **Empty:** the card is not drawn; the table becomes a full-height panel
  (18, 76, 924, 352) with `ALL SITES`, `0 SAVED`, and a centred
  `NO SITES` + `TRIANGLE TO ADD A SITE`.
- **Login failed:** the card footer carries the server reply in yellow; the site
  stays selected and editable; the table is untouched; the header stays
  `NOT CONNECTED`.
- **Save unavailable:** `SAVE UNAVAILABLE` in yellow beside `ALL SITES`, and the
  count reads `n IN MEMORY`. The book works; it will not persist.

### Dock and legend

- Dock: `READY` dim; the selected site (px 2); its host:port (micro, cyan);
  `LOGIN` or `ANON` on the right; `X TO CONNECT` on line three. The empty state
  uses `IDLE`, `NO SITES`, and `TRIANGLE TO ADD A SITE`.
- Legend: `[X] CONNECT  [□] EDIT  [△] NEW  SELECT DELETE  [O] CLOSE
  START SERVER`.
- Deletion is confirmed on CONFIRM; SITES stays behind it.

## SITE EDITOR

Direction A: the SITES card as a live preview above six field rows (D22). The
four states are in
[`design/2026-11-08-site-editor-a-states.html`](design/2026-11-08-site-editor-a-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Header | 0 | 0 | 960 | 64 |
| Preview card | 18 | 76 | 924 | 110 |
| Field panel | 18 | 198 | 924 | 230 |
| Dock | 18 | 440 | 924 | 64 |
| Control legend | 18 | 510 | 924 | 16 |

### Card

- The SITES card, titled `SITE PREVIEW`.
- The name at 32, 100 (px 3); the host:port at 32, 128 (micro, cyan); `LOGIN`
  and `PASSWORD` at 600, 100 and 600, 130, values right-aligned at 928.
- Footer at 160: the password note, or the validation error. The right side
  says `UNSAVED` (white) or `NO CHANGES` (dim).
- A new login site shows `PASSWORD  SET`; a saved one shows `STORED`.

### Fields

- `SITE FIELDS` at 32, 206; a hairline at 241; six 30 px rows from 246:
  NAME, HOST, PORT, ANONYMOUS, USERNAME, PASSWORD.
- Label at 50 (px 2); value right-aligned at 928 (px 2). The focused row has a
  2 px white bar at 26, a 6 % wash, and a white label.
- Values: white normally; `--` dim for an empty field; `NOT USED` dim when
  anonymous disables the credential rows; `ON` green and `OFF` dim on the
  anonymous row; an error turns the field label and value yellow.
- Passwords are ten dots; the 5×7 face has no asterisk. The value is never shown.
- Anonymous `ON` disables USERNAME and PASSWORD and skips them in the focus
  order. Cross on the anonymous row toggles it.

### States

- **New site:** anonymous by default; NAME and HOST empty; PORT 21; the
  credential rows `NOT USED`; the card previews `NEW SITE` and `--`.
- **Edit existing, clean:** the saved values; `NO CHANGES`; the dock offers only
  `CIRCLE TO CLOSE`.
- **Invalid field:** saving focuses the first invalid field, turns it yellow,
  and puts the reason on the card footer (`NAME IS REQUIRED`); nothing is
  written.
- **First save with a password:** the card footer states
  `PASSWORDS ARE STORED UNENCRYPTED` in yellow before the first save. It is a
  statement, not a gate; Square still saves.

### Dock and legend

- Dock: `EDITING` in yellow; the site name, or `NEW SITE`; line three says
  `SQUARE TO SAVE  -  CIRCLE TO CANCEL`, or `CIRCLE TO CLOSE` when clean.
- Legend: `[X] EDIT FIELD  [□] SAVE  [O] CANCEL`.
- Triangle and Start are not offered while editing, so an unsaved edit cannot be
  lost by opening another screen.

**Settled after review:** Circle on a dirty editor opens CONFIRM
`DISCARD CHANGES` (`KEEP EDITING` / `DISCARD`); a clean editor closes
immediately. A login site may save with an empty password: the card shows
`PASSWORD  NOT SET`, the footer `NO PASSWORD STORED`, and the server reply
reports a refused login.

## SERVER

Direction A: the console layout (D25). The six states are in
[`design/2026-11-08-server-a-states.html`](design/2026-11-08-server-a-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Header | 0 | 0 | 960 | 64 |
| Status panel | 18 | 76 | 552 | 352 |
| Warning panel | 582 | 76 | 360 | 200 |
| Client panel | 582 | 288 | 360 | 140 |
| Dock | 18 | 440 | 924 | 64 |
| Control legend | 18 | 510 | 924 | 16 |

### Status

- `STATUS` at 32, 84. The big state at 32, 108 (px 5): `OFF` dim, `STARTING`
  yellow, `ON` green.
- Address at 32, 156: `FTP://` micro dim, the IP px 3 (`C_CYAN` when listening,
  dim otherwise), `:1337` micro `C_CYAN_DIM`.
- Error line at 32, 190 (micro, yellow).
- Readouts at 32, 214 / 244 / 274: PORT, CLIENTS, ACCESS. Labels px 2 dim;
  values px 3 white, right-aligned at 538. The PORT row takes a 2 px white bar
  and a 4 % wash when focused.
- The slab is 32, 340, 488 × 40: `START SERVER` yellow when OFF; `STOP SERVER`
  and `CANCEL` dark. A 2 px white outline offset 4 marks focus.

### Warning

A 582, 76, 360 × 200 panel with a yellow accent and `WARNING` at 596, 84. Eight
micro lines from 596, 108 on a 16 px stride: the anonymous read-write risk in
yellow, the excluded app folder and `tai` denials in dim, then the launch/stop
lifecycle in dim. One of those lines says that an upload replaces a file of the
same name (D41).

### Client

A 582, 288, 360 × 140 panel. `CLIENT` at 596, 296; `CONNECTED` green at the
right when a client is on. With no client, `NO CLIENT` and `ONE CLIENT AT A
TIME`. With a client: the IP at 320, the operation at 342 and its path at 345.5,
the bytes or `NO TRANSFER` at 362, and `ONE CLIENT AT A TIME` at 382.

### Dock and legend

- `IDLE` + `SERVER OFF` + `PORT 1337` + `X TO START`.
- `STARTING` + `SERVER` + `PORT 1337` + `X TO CANCEL`.
- `RUNNING` + `n CLIENTS` + `PORT 1337` + `X TO STOP`.
- A server transfer takes the dock: `UPLOAD` or `DOWNLOAD`, the file, the bar,
  bytes, percentage, and rate.
- Legend: `[X] START SERVER` / `STOP SERVER` / `CANCEL` / `EDIT PORT`,
  `[O] CLOSE`, and `UP/DOWN PORT` or `UP/DOWN START`.

### States

- **OFF at launch:** the warning is visible before the first start; the address
  is dim; the start slab is focused; the header says `SERVER OFF`.
- **Starting:** `STARTING` yellow; a cancel slab; the address dim; the header
  says `SERVER STARTING`.
- **ON, no client / browsing / transferring:** the stop slab is focused; the
  address is live; the client panel fills as the client acts.
- **Bind failure:** the error names the port; the port row takes focus; the
  start slab stays available; the header says `SERVER OFF`.
- **No network:** `NO NETWORK - CONNECT TO WI-FI` uses the same line; the
  address reads `--`; the start slab keeps focus.
- **Second client:** the PC receives a busy reply; the Vita still shows one
  client and the client panel keeps the active one.
- **Stop during a write:** CONFIRM names the in-flight write before the session
  closes.
- **Upload overwrite:** a file received in server mode replaces an existing file
  of the same name without a console prompt (D41); the warning panel says so.

The header repeats the server state on every screen: `SERVER OFF`,
`SERVER STARTING`, or `SERVER ON n CLIENT(S)`.

## ACTIONS

Direction A: the action ledger with the target panel (D26), with local copy/move
using the marks as the source and the current folder as the destination (D27).
The four states are in
[`design/2026-11-08-actions-a-states.html`](design/2026-11-08-actions-a-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Action panel | 18 | 76 | 552 | 352 |
| Target panel | 582 | 76 | 360 | 352 |
| Dock | 18 | 440 | 924 | 64 |
| Control legend | 18 | 510 | 924 | 16 |

### Action panel

- `LOCAL ACTIONS` or `REMOTE ACTIONS` at 32, 84 (px 2, tracked 2); the path at
  32, 102 (micro, cyan); the band state right-aligned at 556: `n ITEM(S)`,
  `n MARKED`, or `VOLUME ROOT`.
- A hairline at 119; rows from 124 at 44 px, six maximum.
- Each row: label at 50 (px 2), description at 50 (micro, `y+23`). Disabled rows
  are dim and carry the reason (`MARK TO COPY`, `PROTECTED PATH`, `VOLUME ROOT`).
  A destructive row’s description is yellow (`CANNOT BE UNDONE`). The selected
  row gets a 2 px white bar and a 6 % wash.

### Target panel

- `TARGET` at 596, 84; the pane at 596, 108; the path at 596, 126.
- Multi: the count at 596, 148 (px 5), `MARKED` and the total at 169. Single: the
  name at 596, 148 (px 3) and the size at 178 (px 2). Protected: the root at 148
  (px 3) and `VOLUME ROOT` at 178 (px 2, yellow).
- A hairline at 210; `DESTINATION` at 224 with the value right-aligned at 928.
- The selected action at 300 and its consequence at 320 (micro); the note at
  400 (micro).
- The target is the single source of truth for where an action goes.

### States

- **Local single, no marks:** copy and move are dim (`MARK TO COPY`,
  `MARK TO MOVE`); upload, new folder, rename, and delete use the cursor; the
  upload destination is the remote pane.
- **Local multi:** copy and move go `TO THIS FOLDER`; rename is dim (`ONE ITEM
  ONLY`); delete is flagged; the marks stay after the operation (D18).
- **Remote single:** download to the local pane; rename, delete, new folder.
- **Protected root:** copy, move, rename, and delete are dim with their reasons;
  new folder is the only action.

### Dock and legend

- Dock: `READY`, the selected action and its target, the size or destination on
  the right, and `X TO RUN`.
- Legend: `[X] RUN  [O] CLOSE  UP/DOWN ACTION`.

## CONFIRM

The one interrupt: the centered 600 × 300 panel (D28). The item list is a fixed
five-row viewport; a longer set scrolls (D29). The six states are in
[`design/2026-11-08-confirm-a-states.html`](design/2026-11-08-confirm-a-states.html).

### Geometry

| Region | x | y | w | h |
|---|---|---|---|---|
| Panel | 180 | 122 | 600 | 300 |
| Title | 210 | 140 | 540 | 21 |
| Context | 210 | 168 | 540 | 11 |
| Item list | 210 | 190 | 540 | 120 |
| Count and warning | 210 | 314 / 336 | 540 | 25 |
| Options | 260 / 500, or 210 / 395 / 580 | 366 | 200 or 170 | 40 |

### Panel

- The title is px 3 in yellow; the context is micro in `C_CYAN_DIM`.
- The item list is five rows at a 24 px pitch. The sizes are right-aligned at
  750; a 2 px scrollbar sits at 758 when the set overflows. A long set scrolls;
  the count, known size, warning, and options never move.
- The total is micro dim (`12 ITEMS - 3.7 KB KNOWN`); the warning is px 2 yellow.
- The options: two 200 px slabs at 260 and 500, or three 170 px slabs at 210,
  395, and 580. Three options use the micro label to keep padding.
- The safe option is dark and focused first; the destructive or committing
  option is the yellow slab. The focus outline is 2 px white, offset 4.

### Input

- Up and Down scroll the item list when it overflows.
- Left and Right choose an option; Cross confirms the focused option; Circle
  cancels from anywhere.

### States

- **Delete:** three or many items; the item viewport, the count and known size,
  `THIS CANNOT BE UNDONE.`, CANCEL / DELETE.
- **Batch conflict:** `2 DESTINATIONS EXIST`; the batch count; CANCEL / SKIP
  EXISTING / OVERWRITE ALL; the replace warning.
- **Cancel a transfer:** the item, the progress, the partial removed; KEEP
  DOWNLOADING / CANCEL.
- **Stop the server during a write:** the client’s write and the partial removed;
  KEEP RUNNING / STOP SERVER.
- **Discard editor changes:** the site, the lost changes; KEEP EDITING / DISCARD.
- **High-risk write:** `WRITE INSIDE <PATH>`; the path; `A BAD EDIT CAN STOP THE
  CONSOLE BOOTING.` and `THERE IS NO UNDO.`; CANCEL / CONTINUE. Used for a write,
  create, rename, or delete inside `ur0:tai`, `ux0:tai`, or any other high-risk
  path (D30, D31). `boot_config.txt` never reaches this panel; it is denied.

## TEXT ENTRY

The system IME is a Vita surface. The app supplies a contract and never draws the
keyboard. The three states are in
[`design/2026-11-08-text-entry.html`](design/2026-11-08-text-entry.html).

### Contract

- The app opens the IME with a title, an initial value, a password flag, and a
  maximum length; the IME returns UTF-8 or cancellation.
- Cancellation keeps the previous value and the caller’s focus; no error is
  shown.
- A password uses the IME’s masked mode and never reaches the session log.

| Context | IME title | Masked | Max |
|---|---|---|---|
| Site name | `SITE NAME` | No | 64 |
| Host | `HOST` | No | 255 |
| Port | `PORT` | No | 5 |
| Username | `USERNAME` | No | 64 |
| Password | `PASSWORD` | Yes | 128 |
| New folder | `NEW FOLDER NAME` | No | 255 |
| Rename | `RENAME` | No | 255 |

The port is validated by the app at 1024–65534, so the data port at control plus one
stays valid (D39). Names and hosts follow the field rules; an empty password is
allowed (D24).

### States

- **Open:** the calling screen stays behind; the IME owns the lower area and its
  own OK / CANCEL.
- **Cancelled:** the field keeps its previous value and the caller returns with
  the same focus.
- **Unavailable:** the open fails; the card footer says
  `TEXT ENTRY UNAVAILABLE` with `FIELD UNCHANGED`; the previous value is kept;
  Circle backs out.

## Screen inventory

The requirements plan fixes these screens; the design phase decides how they
look and how focus moves.

| Screen | Purpose | Required states |
|---|---|---|
| WORKSPACE | Two live panes, transfer dock, header | local only, connected, disconnected, transferring, empty folder, error |
| SITES | Saved site list | empty, populated, selected, deleting |
| SITE EDITOR | Name, host, port, user, password, anonymous | new, edit, invalid field, password warning |
| SERVER | Start/stop, port, IP, client count, warning | OFF, starting, ON, client connected, stop, error |
| ACTIONS | Context actions for the active pane | local, remote, multi-select, read-only |
| CONFIRM | Destructive confirmation or batch conflict | delete, overwrite all, skip existing, cancel |
| TEXT INPUT | System IME for paths, names, and fields | open, cancelled, unavailable |
| ERROR / RECONNECT | Inline failure and reconnect | network lost, login failed, transfer failed, server failed |

## Design phase complete

The screen inventory is settled. The planning questions that were deferred here
are answered in the plan: UNSAFE authid, the passive data port, the threading
model, the 2000-row cap, the site and settings format, the log format, MLSD, the
server foundation, and the local `tai` policy.

Two amendments landed after the close, both user-directed:

- **Risk marks and the high-risk confirmation (D30, D31).** The local manager may
  write inside `ur0:tai` and `ux0:tai`, so a high-risk row draws a yellow diamond
  and the action confirms with a boot warning. `boot_config.txt` stays denied.
- **`FIRST 2000` (KTD7).** A directory longer than the cap says so on the band
  instead of pretending the list is complete.

## Process

1. Use `frontend-design` to create rough, direction-only mockups.
2. Put them in `docs/design/` with dated filenames.
3. Review one screen at a time with the user.
4. Record the settled choices here and in `docs/DECISIONS.md`.
5. Only then hand geometry to the implementation plan.

## Non-goals

- A desktop client layout.
- A VitaShell clone.
- A second alert colour.
- Decorative animation or a value that pulses.
- A theme system or alternate HUD layouts.
- Pixel-perfect mockups before the behavior is settled.

## Open design questions

None for the screens. The architecture and implementation plan owns the planning
and product questions listed under Design phase complete.

# Decisions

Numbered records. Add a new entry when a choice would surprise a future reader.
If a decision is reversed, add a superseding entry; do not rewrite the old one.

## D1. The console is a peer, not a remote endpoint

**Date:** 2026-11-08
**Status:** accepted

**Context:** VitaShell serves the card but cannot connect out; a PC client sees
the Vita as a remote device. Either shape keeps the PC in the middle.

**Decision:** Local files and a saved FTP site are two live panes on the Vita.
Moving a file is one action on the console.

**Consequences:** The UI is designed around a two-pane workspace, not a tabbed
single pane. A desktop-client port and a server-only VitaShell clone are both
ruled out.

## D2. v1 ships client, local manager, and server

**Date:** 2026-11-08
**Status:** accepted

**Context:** The strategy calls the server a compatibility surface, but the
first release is meant to replace VitaShell's transfer role.

**Decision:** The client, local file manager, and anonymous FTP server ship in
v1.

**Consequences:** v1 is a replacement release, not a client MVP. The server
shares the FTP engine but adds a listener, a LAN trust boundary, and its own
screen. The server remains secondary to the workspace in design priority.

## D3. The server is anonymous read-write on the three Vita volumes

**Date:** 2026-11-08
**Status:** accepted

**Context:** VitaShell's server is simple and anonymous. Requiring an account
would add a credential surface the product does not otherwise need.

**Decision:** The server exposes `ux0:`, `ur0:`, and `uma0:` without login and
with read-write access. It is OFF at launch.

**Consequences:** Anyone on the LAN can change files while it is ON. The UI must
keep the ON state and client count visible, warn before the first start, and
deny protected paths. This is not a secure-server feature.

## D4. Passwords are always remembered, unencrypted

**Date:** 2026-11-08
**Status:** accepted

**Context:** Saved sites are meant to reconnect like FileZilla. A prompt on
every connect would make the saved-site list half-useful.

**Decision:** Each login site stores its password. The site file is not
encrypted, and the editor states that before the first save.

**Consequences:** Credentials on the card are readable by anything that can read
`ux0:data`. The app must not claim encryption, must mask the password in the UI,
and must never write it to the session log.

## D5. Multi-select and recursive folders are v1

**Date:** 2026-11-08
**Status:** accepted

**Context:** A file manager that moves one file at a time is a downloader with a
list. Homebrew transfers are usually folders, not single files.

**Decision:** Multi-select, recursive folder copy/move/delete, recursive upload,
and recursive download are v1.

**Consequences:** The queue, conflict prompt, progress, cancel, and partial-file
cleanup must handle a batch. The local operation planner needs a recursion and
copy-into-itself guard.

## D6. GPLv3, with VitaShell as the local-manager foundation

**Date:** 2026-11-08
**Status:** accepted

**Context:** VitaShell is GPLv3 and already contains a proven Vita file manager.
A permissive rewrite would cost more and lose that work.

**Decision:** vitaFTPShell is GPLv3 and may reuse VitaShell code in the local
manager. Reused code keeps its notices and stays inside this project.

**Consequences:** This project is an island in the MIT workspace. Its code must
not be copied into the MIT projects. A future permissive fork would require
replacing the reused code.

## D7. Plain FTP only in v1

**Date:** 2026-11-08
**Status:** accepted

**Context:** The day-one target is `ftp.gnu.org`, and the anonymous public sites
are plain FTP. FTPS and SFTP add crypto, key handling, and compatibility work.

**Decision:** v1 supports plain FTP only. FTPS, SFTP, HTTP, and WebDAV are out.

**Consequences:** Credentials and data cross the network in clear. The site
editor should not imply otherwise. A site that requires TLS or SSH fails with a
clear not-supported message.

## D8. No background daemon or kernel component

**Date:** 2026-11-08
**Status:** accepted

**Context:** The workspace has already paid for a shell plugin that failed on
hardware. A boot-time transfer daemon adds a bootloop risk for a convenience.

**Decision:** Transfers and the server run only while the app is open. v1 ships
no kernel plugin, `*KERNEL` entry, or boot component.

**Consequences:** Closing the app ends its transfers and server. The user must
keep the app open. This is a deliberate boundary, not a temporary limitation.

## D9. One active transfer, one server client

**Date:** 2026-11-08
**Status:** accepted; the one-transfer clause is superseded by D72 and D82, the
one-client clause by D86.

**Context:** Concurrent writers make the state model and the error model much
harder to prove on a console with one screen and one user.

**Decision:** One transfer is active at a time and additional items queue FIFO.
A server-side transfer occupies the same slot. The server accepts one client at
a time and refuses a second with a busy reply.

**Consequences:** The dock is the single place where transfer state lives. The
one-client context is historical: D86's four-session rule and R50 govern, and a
second server client is an accepted session, not a future feature.

## D10. First run seeds an anonymous example site

**Date:** 2026-11-08
**Status:** accepted

**Context:** The primary flow starts with an anonymous public archive. A first
launch with an empty site list makes the user type a host before seeing value.

**Decision:** First run includes an editable `ftp.gnu.org` anonymous site. It is
not auto-connected.

**Consequences:** The example can go stale or be removed by the user. It is a
starting point, not a directory service.

## D11. Visual design is a separate phase

**Date:** 2026-11-08
**Status:** accepted

**Context:** The requirements fix screens, behavior, and input. Pane geometry,
row density, and focus order are easier to judge as mockups than as prose.

**Decision:** Screen-by-screen design and mockups are deferred to a design phase
using `frontend-design`, with the user reviewing each screen. This contract does
not fix layout.

**Consequences:** No implementation may invent geometry before that pass. The
design phase can change layout without reopening product requirements.

## D12. Protected paths and boot-config denial

**Date:** 2026-11-08
**Status:** accepted; the server's app-data and `tai` write denials are
superseded by D87. The volume-root protection and the `boot_config.txt` denial
stand.

**Context:** The app can reach every mounted volume, and the server can be
anonymous read-write. A wrong delete or a boot-config write can cost the console.

**Decision:** Volume roots, `ur0:tai`, and `ux0:tai` cannot be renamed, moved, or
deleted. The server denies all access to `ur0:tai/boot_config.txt` and the app's
own data folder under `ux0:data`, and denies every write, create, rename, and
delete inside `ur0:tai` and `ux0:tai`. The local manager and downloads never
write, rename, or delete `boot_config.txt`.

**Consequences:** The file manager is not a raw root shell. A refused operation
names the protection and changes nothing. The server's path resolver must check
the normalized path, and a remote-supplied filename is validated before a local
path is built.

## D13. Persistence on ux0:, with an in-memory fallback

**Date:** 2026-11-08
**Status:** accepted

**Context:** The memory card can be absent, and the app must still browse `ur0:`
or run a first session.

**Decision:** Sites and settings live under `ux0:data`. If `ux0:` is unavailable,
the app runs with an in-memory list and says saving is unavailable.

**Consequences:** The app never creates a directory on a missing volume. The
user can test locally but loses saved sites at exit.

## D14. Passive FTP only

**Date:** 2026-11-08
**Status:** accepted

**Context:** The Vita is behind a home router; active mode requires the Vita to
accept inbound connections for the client role, which is the wrong default.

**Decision:** The client uses passive mode only. The server offers passive data
connections only.

**Consequences:** A server that requires active mode is out of scope. The server's
passive port strategy is an implementation-plan decision.

## D15. No PC-first workflow and no VitaShell parity

**Date:** 2026-11-08
**Status:** accepted

**Context:** The market already has desktop clients and VitaShell. Matching
either feature for feature would lose the console-first bet.

**Decision:** The product does not design around a desktop client and does not
chase VitaShell parity. Server mode is compatibility, not a feature race.

**Consequences:** Archive extraction, USB mounting, media playback, and every
VitaShell utility stay out. A feature request that only makes sense at a desk is
a candidate for refusal, not a roadmap item.

## D16. The session log is local and password-free

**Date:** 2026-11-08
**Status:** accepted

**Context:** The strategy metrics need transfer completion, crash-free sessions,
connect-to-first-listing, and server-matrix outcomes. There is no telemetry.

**Decision:** The app writes a bounded local session log with connection,
transfer, and server outcomes. Passwords never enter it.

**Consequences:** The log is evidence for the user and the release checklist, not
an analytics channel. Its format and rotation are deferred to planning.

## D17. The workspace uses two equal ledgers

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three WORKSPACE compositions were drawn at 1:1 with the real 5×7
face: A, two equal panels; B, one hero frame around both lists and the dock; C,
one expanded list with the other collapsed to a context strip. The product's
core claim is that both sides are live and a transfer is one action.

**Decision:** The WORKSPACE uses A: two equal 456 px panes with their own title
bands, a 12 px gutter, and a full-width transfer dock below them.

**Consequences:** Both trees are always visible and comparable, which is the
strongest reading of the console-as-peer bet. The active side is carried by a
2 px yellow left edge and a yellow title. B and C remain in
`docs/design/2026-11-08-workspace-directions.html` as rejected alternatives.
The state refinements are in
`docs/design/2026-11-08-workspace-a-states.html`; the settled geometry lands in
`docs/DESIGN.md` after review.

## D18. Marks persist after a completed batch

**Date:** 2026-11-08
**Status:** accepted

**Context:** R9 makes marks pane-scoped and persistent across directory changes
and pane switches, but it does not say what a completed transfer does. Clearing
on success would prevent an accidental repeat; keeping them preserves the set
the user built.

**Decision:** A completed batch does not clear the marks. They stay until the
user unmarks each row, or until the pane's connection or volume root changes.

**Consequences:** The dock's `DONE` state is the signal that the batch finished;
the marked rows remain selected, so pressing Cross again repeats the batch only
by the user's choice. Clearing a large set is one Square press per row in v1; a
bulk clear would be a new action, not a hidden behavior.

## D19. The site identity appears in the header and the remote pane

**Date:** 2026-11-08
**Status:** accepted

**Context:** R4 requires the remote pane to show the site name and host:port;
R66 requires the header to show the connection identity. On WORKSPACE both would
repeat the same string.

**Decision:** Keep both, with different roles. The header is the global status,
present on every screen; the remote pane's copy is local context and is dim.
The header's site is cyan; the pane's site is dim and its path is cyan.

**Consequences:** The identity appears twice on WORKSPACE. The pane copy is
quiet and small; if it proves noisy on hardware, amend R4 rather than drop the
header identity.

## D20. SITES uses a hero site card and a full table

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three SITES compositions were drawn: A, a 552 px ledger with a
selected-site detail and a password note; B, a single 924 px ledger with a
one-line summary strip; C, a compact hero card for the selected site above a
full-width table. The site book is a launcher, and the selected site's login
state is what the user checks before connecting.

**Decision:** SITES uses C: a 924 × 110 hero card for the selected site above a
924 × 230 table that lists every site in 30 px one-line rows.

**Consequences:** The selected site is unmistakable and its login and password
state are readable without opening the editor. The selected site appears twice
(card and table); the card is a preview, not a second list. The unencrypted
password warning stays in the editor; the card shows only `STORED` or
`NOT NEEDED`. The states are in
`docs/design/2026-11-08-sites-c-states.html`; the geometry lands in
`docs/DESIGN.md` after review.

## D21. The SITES card discloses unencrypted password storage

**Date:** 2026-11-08
**Status:** accepted

**Context:** D4 stores passwords unencrypted and the editor warns before the
first save. D20 left the card showing `STORED` without the risk, so the site book
was the one place credentials are listed without the disclosure.

**Decision:** A login site's card footer says
`PASSWORDS ARE STORED UNENCRYPTED`; an anonymous site says `NO PASSWORD STORED`.
`SQUARE TO EDIT` stays right-aligned on the same line. A failed login replaces
the note with the error.

**Consequences:** The risk is visible wherever credentials are listed, without a
new panel or an alert colour. This supersedes D20's consequence that the warning
stays in the editor; the editor still states it before the first save.

## D22. SITE EDITOR uses the preview card above the field table

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three editor layouts were drawn: A, the SITES card as a live
preview above six 30 px field rows; B, a single tall form with the site name as
its title; C, a two-column identity/login form. The editor changes the same
fields the SITES card displays.

**Decision:** SITE EDITOR uses A. The SITES card is the live preview; six field
rows sit below it; the card footer carries the password note or the validation
error; Square saves and Circle cancels.

**Consequences:** The site looks the same before and after editing, and the
password disclosure stays on the card. The editor's legend is only
`EDIT FIELD`, `SAVE`, `CANCEL`; Triangle and Start are not offered while editing,
so an unsaved edit cannot be lost by opening another screen. The states are in
`docs/design/2026-11-08-site-editor-a-states.html`; the geometry lands in
`docs/DESIGN.md` after review.

## D23. Leaving a dirty editor asks first

**Date:** 2026-11-08
**Status:** accepted

**Context:** Circle cancels the editor. A typed host or password is easy to lose
and the editor has no undo.

**Decision:** Circle on a dirty editor opens a CONFIRM prompt,
`DISCARD CHANGES`, with `KEEP EDITING` and `DISCARD`. A clean editor closes
immediately.

**Consequences:** One extra press protects typed credentials. The prompt belongs
to CONFIRM; the editor itself stays a single screen. The dock says
`CIRCLE TO CANCEL` only while the form is dirty.

## D24. A login site may have no password

**Date:** 2026-11-08
**Status:** accepted

**Context:** Some FTP servers accept a username with an empty password. Requiring
a password would reject those servers; allowing it leaves a login that may fail.

**Decision:** A login site may save with an empty password. The card shows
`PASSWORD  NOT SET`, the footer says `NO PASSWORD STORED`, and a refused login is
reported by the server reply through R15.

**Consequences:** The editor does not reject a valid server, and an empty
credential is visible rather than hidden. D4 still applies: any entered password
is stored unencrypted and the footer says so.

## D25. SERVER uses the console layout

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three server compositions were drawn: A, a status panel with a
dedicated warning panel and a client panel; B, status, warning, and client
bands; C, one instrument. The server is a control surface with a safety-critical
warning and one client slot.

**Decision:** SERVER uses A: a 552 px status panel with the state, address,
port, clients, access, and start/stop/cancel slab; a 360 px warning panel; a
360 px client panel.

**Consequences:** The warning has its own surface and cannot be mistaken for a
readout; the address and controls are one block. The client panel carries the
client identity, operation, path, and bytes; the dock carries the server status
or the active server transfer. The states are in
`docs/design/2026-11-08-server-a-states.html`; the geometry lands in
`docs/DESIGN.md` after review.

## D26. ACTIONS uses the action ledger and target panel

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three ACTIONS layouts were drawn: A, a 552 px action ledger beside a
360 px target panel; B, one full-width action table; C, the active pane replaced
by the actions with the other pane visible. The copy/move destination has to be
unambiguous.

**Decision:** ACTIONS uses A. The target panel states the current item or marked
set, the path, the destination, and the selected action’s consequence.

**Consequences:** The action list carries a description and a disabled reason
per row; the target panel carries the destination and consequence; the dock
repeats the selected action. C remains a rejected pane-mode alternative. The
states are in `docs/design/2026-11-08-actions-a-states.html`.

## D27. Local copy and move use the marks as the source and the current folder as the destination

**Date:** 2026-11-08
**Status:** accepted

**Context:** R29 and R31 require copy and move across volumes, but the
requirements never say how the destination is chosen. A destination picker would
add a screen and more presses. R9 marks persist across directory changes.

**Decision:** Copy and move act on the marked set; the active local folder is the
destination. The user marks items, navigates to the destination folder, opens
ACTIONS, and chooses COPY or MOVE `TO THIS FOLDER`. Copy and move need at least
one mark; rename and delete use the cursor. A copy into the same folder
duplicates; a move into the same folder is refused. A marked folder cannot be
copied or moved into itself or a descendant (R31).

**Consequences:** No picker; the marks, count, and destination path are visible
in ACTIONS; the marks stay after the operation (D18). A single-item copy or move
is one mark and a navigation.

## D28. CONFIRM uses the centered interrupt panel

**Date:** 2026-11-08
**Status:** accepted

**Context:** Three placements were drawn: A, a 600 × 300 centered panel; B, a
924 × 184 footer sheet; C, a 924 × 352 full screen. A destructive confirm is the
one interrupt the product allows.

**Decision:** CONFIRM uses A. The panel carries the title, context, item list or
consequence, the count, the warning, and the options. The safe option is focused
first; the destructive or committing option is the yellow slab; Circle cancels
from anywhere.

**Consequences:** The field behind stays quiet and the panel is the only focus.
The item list is a fixed five-row viewport; a longer set scrolls rather than
growing the panel. The states are in
`docs/design/2026-11-08-confirm-a-states.html`.

## D29. A long confirm list scrolls in a five-row viewport

**Date:** 2026-11-08
**Status:** accepted

**Context:** A delete or a batch conflict can name many items. The 600 × 300 panel
cannot grow, and the options must not cover a name.

**Decision:** The item list is a fixed five-row viewport. When the set is longer,
Up/Down scrolls it, a 2 px scrollbar shows the position, and the count, known
size, warning, and options stay fixed. The panel stays 600 × 300. The CONFIRM
input row gains Up/Down scrolling.

**Consequences:** Three items or three hundred share one panel; the options never
move; the count and known size state the whole set; every name is reachable by
scrolling. R58’s naming requirement is satisfied by the list plus the count and
size. The plan’s input table carries the added scroll.

## D30. The local manager may write inside the tai folders, behind a high-risk confirmation

**Date:** 2026-11-08
**Status:** accepted; the write permission stands, but the high-risk confirmation
and the server's blanket `tai` denial are superseded by D87. It supersedes D12's
consequence that the file manager is not a raw root shell.

**Context:** D12 forbids renaming, moving, or deleting `ur0:tai` and `ux0:tai`,
and R53 makes the server deny every write inside them. It never said what the
local manager may do to the files inside: `config.txt`, `plugins.txt`, a
`*.suprx`. A file manager that refuses the files its owner selects is not doing
the job, and the user directed that everything editable stay editable with the
risk carried by the interface.

**Decision:** The local manager may write, create, rename, and delete inside
`ur0:tai` and `ux0:tai`, after a high-risk confirmation that names the path and
says a bad edit can stop the console booting. `ur0:tai/boot_config.txt` remains
denied outright (R34). The `tai` directories themselves and the volume roots still
cannot be renamed, moved, or deleted (R33). The server keeps its blanket denial
(R53).

**Consequences:** The local and server rules differ on purpose: the person holding
the console is not the anonymous client on the LAN. The refusal for
`boot_config.txt` is a denial, not a warning, and it is the one path the app never
mutates. The risk table is shared policy, so a new high-risk path is one entry.

## D31. High-risk paths carry a row mark and a warning confirmation

**Date:** 2026-11-08
**Status:** accepted for the row mark; the warning confirmation is retired by
D87.

**Context:** D30 allowed the write, so the risk had to be visible before the act
rather than only inside the dialog. The design system allows a mark only when it
encodes something.

**Decision:** A high-risk row draws a 6×6 yellow diamond at `x+27` and its name
starts at `x+40`; every row reserves that column so names stay aligned. ACTIONS
names the risk on its target panel. The confirmation uses a high-risk body titled
`WRITE INSIDE <PATH>` with the boot warning and `CANCEL` / `CONTINUE`.

**Consequences:** The change is one glyph and one CONFIRM body, recorded in
`docs/DESIGN.md`; no new screen. The mark and the dialog say the same thing, which
is the point: the mark makes the user look, the dialog makes them decide.

## D32. The app ships one UNSAFE eboot

**Date:** 2026-11-08
**Status:** accepted

**Context:** R51 and R52 require the server to browse and write `ux0:`, `ur0:`,
and `uma0:`. A safe eboot has restricted filesystem access; COCKPIT's D42 records
that even reading `uma0:` capacity through `sceIoDevctl` returns `0x80010030`
under the safe authid and works under `UNSAFE`, confirmed on hardware.

**Decision:** One eboot, built with `UNSAFE`, with no safe variant.

**Consequences:** Installing requires "Enable Unsafe Homebrew" and the install
flow warns. The app runs with system-wide permissions it does not otherwise need.
That cost is accepted because a server that cannot write two of its three volumes
is not the product. The permission level is not used for anything beyond file
access.

## D33. Transfer success is judged by attributed cause class

**Date:** 2026-11-08
**Status:** accepted

**Context:** A raw completion rate punishes the app for the network and for the
remote server. A WiFi drop, a 421, or a peer that closes mid-transfer is not an
app defect, and counting it as one makes the metric useless for judging a release.

**Decision:** Every started transfer records an outcome and one cause class —
`app`, `server`, `network`, `storage`, or `user` — plus the stage it reached. The
release gate is zero app-class failures, and at least 95% completion over
transfers that were neither user-cancelled nor network-interrupted, minimum 20
attempts. Every failure still appears in the report with its class.

**Consequences:** The strategy's transfer completion rate is computed over the
attributable subset and published with the class breakdown beside it. A single
unexplained failure blocks the release, which is stricter than a percentage alone.
The class is assigned where the failure is mapped, in the protocol engine, not by
the UI.

## D34. Every remote delete confirms

**Date:** 2026-11-08
**Status:** accepted

**Context:** R24 required a confirmation only for a recursive remote delete. A
single remote file deleted by a mis-press is just as unrecoverable, and the local
pane already confirms every delete.

**Decision:** Every remote delete opens the same CONFIRM the local delete uses,
naming the items and saying it cannot be undone. A recursive delete also names the
count and total known size.

**Consequences:** One destructive rule for the whole app. A remote clean-up costs
one more press per item.

## D35. A failed data-channel transfer offers one-press retry

**Date:** 2026-11-08
**Status:** accepted; superseded in part: one-press retry is retired for v1, and
a failed item is retried by running it again.

**Context:** R26 distinguishes a dropped data channel from a dropped control
channel. Without resume, the useful recovery for the first is to run the item
again.

**Decision:** After a data-channel failure with a healthy control channel, the
dock offers `X TO RETRY` and re-queues only the item that failed. A control-channel
failure routes to reconnect instead.

**Consequences:** The dock gains one transition; resume stays out of v1. A retry
of a large file starts from zero and says so.

**Superseded (2026-11-16):** One-press retry is retired for v1. No retry command
was built, and the dock no longer offers `X TO RETRY`; a failed item is retried
by running it again. The decision above stays as the record of what was chosen
at the time.

## D36. A cross-volume move is all-or-nothing

**Date:** 2026-11-08
**Status:** accepted

**Context:** A move across volumes is a copy, a rename into place, and a delete of
the source. If the delete fails, the two volumes disagree about whether the file
moved, and the user is left to work out which copy is real.

**Decision:** If the source delete fails, the destination is removed as well, so
the operation leaves nothing changed. If that rollback also fails, the dock names
both paths and reports `MOVE INCOMPLETE`. Free space is checked before the copy
when the platform reports it.

**Consequences:** R42 carries the rule. A failed move costs a second full copy in
the worst case, which is the price of never leaving a duplicate the user did not
ask for.

## D37. The server stops and reports when the network drops

**Date:** 2026-11-08
**Status:** accepted

**Context:** With WiFi down the server has no address to advertise and no client
can reach it. Staying ON would claim a state the app cannot back.

**Decision:** A network loss while the server is ON stops the listener and the
session, returns to OFF, and names the reason: `NETWORK LOST - SERVER STOPPED`.

**Consequences:** The port is free for a restart, and the ON state always means a
listening socket. A user who reconnects starts the server again.

## D38. One worker thread owns every long operation

**Date:** 2026-11-08
**Status:** accepted

**Context:** A single cooperative loop would keep the model simple, but name
resolution is a bounded blocking call and would freeze the frame; R70 forbids a
stalled UI. Thread-per-role would put two writers near the one transfer slot.

**Decision:** One worker thread owns the client session, the server listener and
its slot, the transfer queue, and the local batch job. The main thread owns the UI
and draws. Commands and events cross fixed-capacity rings; listing snapshots are
published by a single index write.

**Consequences:** Each piece of shared state has exactly one writer, and the one
transfer slot is enforced by the thread that runs it. The cost is two structures
to hand off and no direct call from the UI into the engine.

## D39. The server's passive data port is the control port plus one

**Date:** 2026-11-08
**Status:** accepted; the port rule is superseded by D92, and the entry stays as
history.

**Context:** The server runs one data transfer at a time, so a single data
port cannot collide with itself. An ephemeral port would work on the LAN but makes
router forwarding impractical, and the screen edits only one port.

**Decision:** The data listener binds the control port plus one, on the LAN
address, and is advertised in the PASV reply. The port editor accepts 1024–65534
so the pair stays valid.

**Consequences:** A home router needs two rules, not a range. A bind failure on
either port is reported on the same error line, naming both.

**Superseded (2026-11-16):** D92 replaces the single-port rule. The server binds
only the control port at start and gives each session slot its own data port on
that slot's first PASV; a control-port bind at start leaves the server OFF, and
a data-port bind at PASV answers 425 and leaves the server ON.

## D40. The FTP server is written in the core; nothing is vendored

**Date:** 2026-11-08
**Status:** accepted

**Context:** VitaShell's server is GPLv3 and proven on hardware, and libftpvita is
an MIT server library. Either could be reused. Both are whole implementations
with their own path handling and their own globals, and this project's safety
rules live in one path table that the client, the server, and the local manager
must share.

**Decision:** Both roles are written in `src/core/ftp/` against RFC 959 and RFC
3659, sharing the reply parser, the listing formatter, the path table, and the
transfer planner. VitaShell and libftpvita are behavioral references only. The
project stays GPLv3 by choice, and COCKPIT's MIT graphics layer is adapted with
its notice.

**Consequences:** One path-rule source of truth, so R53's denials cannot drift
from the client's checks. The server command loop is new code and is proven by
the scripted-client tests and the M5–M7 hardware rows. `docs/THIRD_PARTY.md`
records that nothing is vendored.

## D41. A server-side upload overwrites without a console prompt

**Date:** 2026-11-08
**Status:** accepted

**Context:** R41 asks before overwriting an existing destination. In client mode
the person at the console answers. In server mode the writer is an anonymous PC
and the console may be unattended, so a console CONFIRM would hold the one
transfer slot with no one to answer it while the PC waits for a reply.

**Decision:** A file received in server mode overwrites an existing destination
through the templed write, with no console prompt. The SERVER warning says that
uploads replace files of the same name. Client-side and local transfers keep the
R41 prompt.

**Confirmed by the user** in the planning session that wrote this plan.

**Consequences:** The one slot is never parked on a question nobody can answer.
The overwrite is disclosed where the server is started, not at the moment it
happens. If the disclosure proves insufficient on hardware, the alternative is a
550 refusal, recorded here so it is one decision away.

## D42. The server's trust boundary is canonical paths, two rename operands, and a peer-checked data socket

**Date:** 2026-11-08
**Status:** accepted; extends D12's protected set.

**Context:** The server takes paths from an anonymous LAN client, and the exposed
volumes are case-insensitive. A denial table matched against the client's spelling
is defeated by `UR0:/TAI/./BOOT_CONFIG.TXT`, a repeated separator, or a different
case, and a rename that checks only its destination can move a file out of the
load path. The data listener is reachable by any host on the LAN, not only by the
control client.

**Decision:** Denial, protected, and risk checks run on one canonical form:
`.` and empty segments removed, separators collapsed, `..` clamped, and the volume
and every segment compared case-insensitively while the original bytes go to the
filesystem. `ur0:data` and `ux0:data` join the protected set for every role. A
rename is refused when either operand is denied. A data connection is accepted
only from the control connection's peer address, and the backlog is drained when
PASV arms. Any server command that mutates a path takes the transfer slot, so a PC
cannot delete what the Vita is writing.

**Confirmed by the user** in the planning session that wrote this plan, including
the peer-address rule and the wider 450.

**Consequences:** The remote control and the local pane enforce the same table,
and a case variant can no longer reach a denied file. A PC whose data connection
comes from a translated address (a second NAT) is refused; that is the correct
trade for not handing the file to a stranger. The slot now covers mutating commands
as well as data commands, which is a wider 450 than R38 previously stated.

## D43. One shared FTP core, event callbacks, explicit deadlines, and an ABOR grace

**Date:** 2026-11-09
**Status:** accepted; implementation shape for U6/U7.

**Context:** The plan named `client.c` and `server.c`, but both roles need the
same control-line cap, the same remote path normalizer, and the same templed
write. Duplicating them would let a fix to the 4096-byte line cap or the
`.vftp.part` discipline drift between the client and the server. The client also
has to tolerate both legal ABOR reply orders (`426` then `226`, or `226` then a
late `426`) without letting the trailing reply become the next command's answer.

**Decision:** `src/core/ftp/ftp.c` owns the shared line reader, remote path
normalizer, cause/stage names, and templed write. `client.c` and `server.c` own
their state machines and report through one event callback each; they own no UI,
no listing arena, and no queue. The client budget is 10 s total with 3 s resolve,
3 s connect, 2 s greeting/login, and 2 s first listing caps; a control command has
5 s and a data operation a 15 s idle deadline. After an abort the client keeps a
250 ms drain grace: a `225`/`226` starts it, a trailing `426` is consumed, and a
new command is refused during it rather than allowed to claim the stale reply.
Recursive folder planning, the conflict gate, and the transfer queue remain
U8/U16/U17; U6 proves the single-file transfer and the high-risk gate that the
planner consumes.

**Consequences:** The engine is three files, not two, and the host suite drives
both roles through scripted peers. A cancel can report the transfer done and
still refuse the next command for up to 250 ms; that is the cost of not shifting
the reply stream. A server that answers ABOR with one definitive refusal ends
the grace immediately and keeps the session. A listing name that does not fit the
256-byte entry is dropped, not truncated, so a later action cannot target a
different file. The adapters and the worker still own the sockets, files, rings,
and listing generations.

## D44. vitaFTPShell is a standalone public repository

**Date:** 2026-11-09
**Status:** accepted; supersedes the workspace-boundary language in D6.

**Context:** vitaFTPShell was built inside a multi-project workspace whose other
projects are MIT-licensed. The project now has its own public repository,
[`ghostmeson/vitaftpshell`](https://github.com/ghostmeson/vitaftpshell), with its
own history, issue tracker, and release artifacts. It no longer shares a source
tree or a repository with those projects.

**Decision:** vitaFTPShell is a standalone GPLv3 project. Its repository contains
only this app, its tests, and its documentation. The project remains GPLv3 by
choice (D6, D40) and may still reuse VitaShell code under the same license. The
COCKPIT-derived graphics files keep their MIT notice in
[`LICENSES/COCKPIT-MIT.txt`](../LICENSES/COCKPIT-MIT.txt). The "MIT workspace"
boundary language in D6 is retired; D6's choice of GPLv3 and its reuse allowance
stand.

**Consequences:** Contributions, issues, and releases happen in one repository.
No cross-repository copying rule is needed, but every third-party notice stays
with the code. The COCKPIT-derived files remain identifiable so the MIT notice is
never lost.

## D45. A cross-volume move is atomic per file, not per batch

**Date:** 2026-11-09
**Status:** accepted; narrows R42's all-or-nothing wording for a multi-file batch.

**Context:** R42 and D36 call a cross-volume move all-or-nothing. U17 showed that
a whole-plan rollback cannot keep that promise for a folder or batch: once a
file's source is deleted, removing its destination to "undo" a later failure
deletes the only surviving copy. The executor reproduced that loss with two
moves and with a folder move.

**Decision:** Each file move is copy, rename, delete source. If the source delete
fails, a destination the plan created is removed; if that removal also fails the
operation reports `MOVE INCOMPLETE: <source> -> <destination>`. A destination
that already existed is left with the new bytes and reports
`MOVE FAILED - DESTINATION KEPT`, because its old bytes were replaced at commit
and removing the new file would leave neither. A batch that fails after some
moves leaves the completed moves and reports `MOVE FAILED - n OF m MOVED`; no
rollback removes a moved file. A copy rollback removes only a destination the
plan created, never one that existed before it.

**Consequences:** R42's guarantee is per file, not per batch. The dock can show a
partial batch with a moved count; the user can retry the remainder. The local
manager never deletes the only copy to fake an all-or-nothing batch. The plan's
U17 scenario is updated to say so.

## D46. Planner and handoff caps are finite and refuse, never truncate

**Date:** 2026-11-09
**Status:** accepted; implementation shape for U8/U15/U16/U17.

**Context:** A recursive plan and a cross-thread ring both need a bound before
the Vita memory measurement in U14. A truncated plan or a silently dropped
record is worse than a refusal: it can leave a folder half-copied or a stop
command unseen.

**Decision:** The planner uses a 256 KiB path arena, 2048 steps, and 2048
rollback records. A plan that would exceed any of them is refused with
`PLAN TOO LARGE`; it is never truncated. The transfer queue holds 64 descriptors.
The command ring holds 64 records, the event ring 128, and the bulk lane 8 KiB;
a normal command that overflows is counted in `lost`, while `SERVER STOP` and
`CANCEL` have dedicated slots and are never dropped. Progress is a one-record
seqlock latch; the server state is latched separately. Each pane has three
listing generations.

**Consequences:** A batch larger than 64 transfers or a plan larger than 2048
steps must be split into runs; the UI reports the refusal rather than silently
dropping items. The numbers are measured again in U14 and can be raised only with
a decision and a memory measurement.

## D47. UI commands carry explicit paths; ACTIONS uses the cursor or the marks

**Date:** 2026-11-09
**Status:** accepted; implementation shape for U9.

**Context:** The first reducer pass stored the active folder in the ACTIONS target
and emitted one TRANSFER command with a mark count. The worker cannot read
`UiState`, so it would have transferred the cursor item instead of the marked set
and could delete the folder the user was standing in instead of the cursor row.

**Decision:** The ACTIONS source is the cursor row for rename, delete, and new
folder, and the marked set for copy, move, and delete when marks exist. The
current folder is the copy/move destination; the other pane is the transfer
destination. Cross on a file with marks emits one `TRANSFER` command per mark;
without marks it emits one for the cursor. A high-risk action confirmed inside
the reducer emits `RUN_ACTION` with the approval flag; only a confirm requested by
the worker returns a `CONFIRM` command. The worker's save result clears the
editor's dirty flag through its own event. Commands carry paths, never pointers
into `UiState`.

**Consequences:** A marked batch uses one command per item and is bounded by the
command ring and the queue. The worker never infers a source from the current
folder. The reducer remains the only writer of `UiState`.

## D48. Adapters are constructed once; the net address is host-order bits

**Date:** 2026-11-10
**Status:** accepted; implementation shape for U12 and U13.

**Context:** The composition root must construct each platform adapter once and
the policy must not learn a Sce type. The NetPort contract said "network byte
order", which is ambiguous for a uint32 on a little-endian ARM: the fake and the
socket adapter could disagree on how to print the same address.

**Decision:** Each adapter exposes one plain-C constructor (`psp2_net_port`,
`psp2_fs_port`, `psp2_input_port`, `psp2_ime_port`, `psp2_clock_port`,
`psp2_log_port`). `main` calls each once and passes the ports to `app_create`.
`NetAddress.addr` holds host-order bits (octet 1 in bits 31..24); the net adapter
converts with `htonl`/`ntohl` at the socket boundary. The FS adapter reports
`mtime` unknown, uses the `0x3001` devctl free-space call and the
`sceAppMgrGetDevInfo` fallback only for `ux0:`, and returns
`rename_replaces() == 0` so KTD12 always uses the aside fallback. The app target
compiles the real core/gfx/ui/app/adapter sources and links `vita2d`, the SDK
stubs the adapters call, `m`, and `pthread`.

**Consequences:** A new adapter is a new constructor plus a new file on the
allowlist; it never changes `main`'s policy. Printing a peer is now exact on
hardware and on the fake. The aside fallback costs one extra rename on every
overwrite until a hardware session proves `sceIoRename` replaces.

## D49. One worker thread; inline worker stepping for the host test

**Date:** 2026-11-10
**Status:** accepted; implementation shape for U13.

**Context:** The product needs a worker that never blocks the frame loop, while
the integration gate needs deterministic, single-threaded observation of the
same code. Two implementations would let the tested one drift.

**Decision:** `worker_step` is the only unit of work. In the product, `app_start`
runs it on a POSIX thread with an 8 ms sleep; `app_create(..., 0)` leaves the
thread off and `app_tick` calls the same `worker_step` inline. Commands, events,
the confirm latch, listings, and the site snapshot all use `src/core/handoff.c`.
The site book snapshot is 16 KiB, carries no passwords, and is read with a
generation number; the save result is a separate event; the worker emits a full
site only for the editor. `worker->done` is atomic and `worker_step` returns
before ticking when it is set.

**Consequences:** A host test exercises the real worker and reducer without a
thread. The product's only concurrency is the handoff and the atomic done flag.
The 16 KiB bulk lane is part of D46's cap; raising it needs a memory measurement
and a new decision.

## D50. The renderer reads state; 36 preview states are the evidence

**Date:** 2026-11-10
**Status:** accepted; implementation shape for U10/U11.

**Context:** U10/U11 require every settled state to render from state alone and
the geometry to be asserted without a golden image. Glyph rectangles cannot name
the string that was drawn, so the draw-call trace alone cannot prove the copy.

**Decision:** `src/ui/render.c` dispatches the current screen and draws the
shared header; every screen reads `const UiState` and returns nothing. A
`font_set_trace` hook records each string, position, size, and colour for the
host suites; the app leaves the hook NULL. `tools/preview` builds 36 fixtures
from `UiState` and writes one PNG per settled state. Two settled-composition
amendments are recorded here: the SERVER warning uses ten micro lines so the
D41 upload-overwrite disclosure fits without shrinking the panel, and the
TEXT ENTRY unavailable notice sits on a black scrim, never the selection wash.
The sites table has a `first` row so the selected row stays visible past six.

**Consequences:** The render suites assert the DESIGN geometry and copy, and the
preview is evidence rather than a golden image. A new screen state needs a
fixture, a draw-call assertion, and a preview file.

## D51. High-risk writes and the one slot are enforced in the worker too

**Date:** 2026-11-10
**Status:** accepted; the one-slot and external-busy clauses are superseded by
D72, D82, and D85; the high-risk confirmation is retired by D87. The hard
protected-path and boot_config enforcement stands.

**Context:** A reducer-only risk check cannot see the final name of a new folder
or rename, and a marked batch can contain a high-risk item that is not the
cursor. A reducer-only slot check cannot see a PC's server transfer.

**Decision:** The reducer checks the destination and every marked source before
it emits a local write, and opens `UI_CONFIRM_HIGH_RISK` when any result is
`PATH_RISK_HIGH`. The worker re-checks the built plan, asks for the same
confirmation when the final path is high risk, and refuses a plan whose fact
scan overflowed `WORKER_FACTS_MAX` or `WORKER_REMOTE_DEPTH` with `PLAN TOO
LARGE` instead of copying a prefix. The worker gates the client pump on
`ftp_server_busy`, and sets `ftp_server_set_external_busy` while an app
transfer or queued item owns the slot, so a server data command answers 450.
A local action refuses while a transfer or server data command is active.
Same-folder copy candidates are statted before planning so a second copy does
not silently overwrite the first.

**Consequences:** Safety no longer depends on the screen that emitted the
command. A local batch waits for the slot instead of running beside a transfer.
A batch larger than the fact cap is refused, not partially executed.

## D52. IME initialization stays an adapter-owned hardware claim

**Date:** 2026-11-10
**Status:** accepted; implementation shape for U12.

**Context:** The IME init order is an open decision. The app needs UTF-8 in and
UTF-8 or cancellation out, and the password must never reach a log.

**Decision:** `psp2_ime.c` configures the common dialog once before the first
dialog, initializes the IME dialog per open, and converts bounded UTF-8 to
UTF-16 and back inside the adapter. Every step that has not run on hardware
carries a `[C]` comment naming the claim. The reducer keeps the previous value
and focus on cancellation; the app draws no keyboard. If `begin` fails, the
app reports `TEXT ENTRY UNAVAILABLE` and the field is unchanged.

**Consequences:** U14 may need to change the init order or load the IME module;
that change stays in the adapter and never reaches policy.

## D53. The local pane opens at ALL DRIVES; Triangle/Select swap

**Date:** 2026-11-10
**Status:** accepted; supersedes R2's default local path for the v1 workspace and
the DESIGN input table's Triangle/Select bindings.

**Context:** The first hardware session showed that starting inside `ux0:data`
made the console feel like a folder view rather than a file manager, and the
user asked for VitaShell's all-drives root. The same session found the settled
Triangle/Select bindings backwards for the user's hands.

**Decision:** The local pane always opens at a virtual root that lists the
mounted `ux0:`, `ur0:`, and `uma0:` volumes. Entering a volume shows its root;
Circle from a volume root returns to `ALL DRIVES`. The virtual root disables
copy, move, rename, delete, and new folder; a volume root keeps new folder as
its only action. Triangle opens ACTIONS and Select opens SITES in the
WORKSPACE; the SITES screen's own Triangle (new site) and Select (delete) are
unchanged. The workspace legend follows the new bindings. A download whose
local destination is the virtual root lands in `ux0:data` when it exists,
otherwise the first mounted volume root.

**Consequences:** The last local path is still saved but is not the startup
view. The local pane shows a `DRIVE` value for a volume row. A transfer from
the remote pane at the virtual root has a real destination instead of a
synthetic path. R2's "last local path" startup rule is deferred until a
settings surface exists.

## D54. The session log is always-on, structured, and diagnostics-first

**Date:** 2026-11-10
**Status:** accepted; supersedes the 64 KiB, one-generation log in D39.

**Context:** The first hardware session produced a 9-line log that could not
explain a folder listing, a failed connection, or a server start. The user
asked for a log that records the app, its actions, button presses, what they
did, and every error, so a user can upload it for diagnosis.

**Decision:** The log is always on and records structured events for:
app start/stop with version and build; screen changes; every button edge (not
repeat ticks); every command emitted and received; IME begin/result/cancel
with field name and masked flag, never the value; local and remote listing
begin/open/read/end with path and count; queue enqueue/depth; transfer
start/done/fail with bytes, total, cause, and stage; connect/login/failure with
stage and cause; server start/stop/client; site/settings saves; and every
error with a mapped reason. The log keeps 256 KiB per file and three
generations: `session.log`, `session.old.log`, `session.old2.log`.

The app never writes the log directly. It pushes fixed `HandoffLogRecord`
entries to the worker, which is the one writer; a full log ring counts the lost
entries and writes a `log` event with the count. Passwords, IME values, and raw
server reply text never enter a record. A server-class error logs
`server reply withheld` instead of the reply text. Paths, site names, and hosts
remain, because they are the diagnostic context the user asked for.

**Consequences:** A normal session writes more often to `ux0:data`; the log is
unencrypted and contains paths and site names. The session log is now three
files; a user should upload all three. A button sequence that is not in the
current file may be in `session.old.log` or `session.old2.log`.

## D55. Listings apply inside the begin/end pair; actions report in the workspace

**Date:** 2026-11-10
**Status:** accepted; governs the listing handoff and the file-manager action
feedback.

**Context:** The first b18 hardware session showed folders and files, but every
file action appeared dead. The host integration test found two independent
causes. First, the action ledger treated the *pane path* as the protected item,
so at `ux0:` every selected child inherited `VOLUME ROOT` and copy, move,
rename, and delete were disabled. Second, a run stayed on ACTIONS, which has no
status line, so a refused plan, a failed delete, and a successful operation all
looked identical. While reproducing this, the test also exposed a listing race:
the worker publishes rows on one channel and events on another, so the app
could apply a new listing before draining its begin event; the later begin then
cleared the rows and left an EMPTY folder.

**Decision:** The action lock follows the selected item, not the folder, so a
child of a volume root is actionable while the root itself is not. Running an
action leaves ACTIONS for WORKSPACE, where the transfer dock shows progress and
the result; a confirmation returns to WORKSPACE after its run. The idle dock's
third line shows `state->status` in yellow until the next key, on both
WORKSPACE and ACTIONS. A published listing is applied when its end event is
drained, after the begin event that cleared the pane and before the end event
that clears `READING`; the rows and the event therefore stay in one order.

**Consequences:** The local pane no longer disables every file action at a
volume root. A disabled action or a worker error is visible without leaving the
ledger, and an action that runs is visible in the dock. The listing handoff has
one apply point; a future second apply path would reintroduce the empty-folder
race and needs a new decision.

## D56. The IME follows the SDK sample, and its failure code is logged

**Date:** 2026-11-11
**Status:** accepted; closes the IME initialization order left open in AGENTS.md.

**Context:** The b18 session log shows five `command cmd=open_ime` entries each
followed by `ime cmd=begin detail=unavailable`. New folder and rename cannot
work without the system text entry. The adapter had four differences from the
IME sample shipped in `vitasdk/share/gcc-arm-vita-eabi/samples/ime`:

- it did not call `sceAppUtilInit` before `sceCommonDialogSetConfigParam`;
- it left `SceImeDialogParam.supportedLanguages` at zero and `languagesForced`
  false;
- it did not call `sceCommonDialogUpdate` (the sample uses
  `vita2d_common_dialog_update`) every frame, so the dialog cannot advance;
- it treated `SCE_COMMON_DIALOG_STATUS_NONE` immediately after init as an error.

**Decision:** The IME adapter follows the sample: apputil init first, a
common-dialog config with a concrete English/CROSS value instead of the
`MAX_VALUE` sentinels, `SCE_IME_LANGUAGE_ENGLISH` with `languagesForced`, and a
short NONE grace. `draw_present` calls `vita2d_common_dialog_update` every
frame. `TextInputPort` gains `last_error`, a plain integer the app writes to
the session log; policy never interprets it. The typed value, the password
flag, and the field title still follow the D54 rules: no value is logged.

**Consequences:** `SceAppUtil_stub` is linked. `src/gfx/draw.c` remains the only
file that includes `vita2d.h`; the IME adapter does not reach into the renderer.
The sample sequence and the NONE grace are `[C]` until the b20 run proves them;
the raw error code makes the next failure classifiable instead of another
`unavailable`.

## D57. The worker runs on a 128 KiB stack and fsop_run keeps a small frame

**Date:** 2026-11-12
**Status:** accepted; fixes the b20 rename crash.

**Context:** The complete b20 core shows the app worker thread, named `pthread`,
stopping with `0x30004` (data abort) at `fsop_run+0x16`, called from `run_plan`.
The `STACK_INFO` note gives the worker stack `0x4d2c` (19,756 bytes); the b20
`fsop_run` frame was `0x4c54` (19,540 bytes). The compiler had inlined
`run_copy_file`'s 8 KiB buffer into `fsop_run`, so a rename that copies nothing
still reserved most of the worker stack. The host tests ran `fsop_run` on the
main test thread's large stack, so they could not see it.

**Decision:** The worker is created with a 128 KiB stack through
`pthread_attr_setstacksize`; if the attribute or the size is refused, the app
falls back to the pthread default. `run_copy_file` is `noinline`, so its copy
buffer exists only during an actual copy. The rollback-index array uses
`uint16_t` because `FSOP_ROLLBACK_MAX` is 2048. The resulting b21 frames are
about 5,476 bytes for `fsop_run` and 11,324 bytes for `run_copy_file`.

**Consequences:** The worker costs 128 KiB of memory, which is small against the
listing arena. The stack-size request is a `[C]` platform claim until the b21 run
shows it; the fallback keeps the app starting if the Vita refuses it. The
`fsop_run` frame is now a budgeted resource: a future 8 KiB local array in that
function would reintroduce the bug, and the copy buffer must stay in its own
function. Host tests still do not reproduce a small worker stack; the core dump
is the evidence for this class of defect.

## D58. "No selection" is not the current folder

**Date:** 2026-11-12
**Status:** accepted; supersedes the implicit fallback in D55's action source.

**Context:** The b21 run made rename work, then showed that inside a folder the
ACTIONS ledger still offered copy, move, upload, rename, and delete with the
cursor on `..`. `ui_actions_prepare` fell back to `pane->path` when there was no
row, so those actions targeted the folder the user was standing in. In an empty
folder this made the folder itself the delete/rename target.

**Decision:** `UiActions` carries `has_selection`, true for any selected row
other than `..` or for any marked item. With no selection, copy, move, upload,
download, rename, and delete are disabled with `NO SELECTION`; NEW FOLDER is the
only live action and still creates in the current folder. The ledger path names
the current folder for context, but the target panel says `NO SELECTION` and the
dock shows the selected action's reason. `handle_actions_key` refuses a
non-new-folder action if `has_selection` is false, even if an enabled flag was
forced.

**Consequences:** The cursor on `..` no longer makes the current folder a file
target. Marks remain a selection for copy, move, and delete even when the cursor
is on `..` (D18/D27). NEW FOLDER keeps the current folder as its destination, so
it remains available in an empty folder. A future action that needs the current
folder without a selection must be added to the explicit NEW FOLDER exemption.

## D59. The advertised address is host order; free space is a device query

**Date:** 2026-11-12
**Status:** accepted; fixes the b22 server-address and card-warning defects.

**Context:** The b22 SERVER panel showed `192.29.168.192` while the console's
system TTY recorded `192.168.29.192`; the PC `nc` and FileZilla both timed out.
`psp2_net.c` stored `inet_pton`'s network-order result directly in
`NetAddress.addr`, but the port's convention is host order everywhere else
(`net_connect` applies `htonl`). In the same run the Vita warned about the memory
card after the server started. The worker was calling `sceIoDevctl(0x3001)` for
free space on every subfolder, including `ur0:` and `uma0:`. VitaShell's FTP
library uses `INADDR_ANY`, a 128-connection backlog, and a 4 MiB net pool; its
file browser queries free space on the device entry, not each folder.

**Decision:** `net_link` converts the resolver result with `ntohl` before it
becomes a `NetAddress`. The net pool is 4 MiB and the listener backlog is 128,
matching `ftpvitalib`. Free space is a per-volume value: the worker caches it,
queries at the volume root or on first use, and the adapter queries only `ux0:`
(devctl `0x3001`, then `sceAppMgrGetDevInfo`); `ur0:` and `uma0:` report unknown
and the UI renders `--`.

**Consequences:** The SERVER panel shows the address the console actually has,
so a PC can reach the control port. Other volumes lose a free-space number but
no longer trigger repeated card-slot devctl calls. Each session now has its own
PASV data listener (D92), where the old rule was one listener on `control+1`;
VitaShell creates an ephemeral PASV socket per command. If
the control connection works but PASV does not, that is the next server change.
The control accept still uses the non-blocking `select` on the worker; if it
does not accept at the corrected address, the fallback is `ftpvitalib`'s
blocking accept on a dedicated thread.

## D60. Local COPY/MOVE use a clipboard and PASTE

**Date:** 2026-11-12
**Status:** accepted; supersedes D27's same-folder destination for local
COPY/MOVE.

**Context:** The b23 run made FTP work and exposed the file-manager actions.
COPY/MOVE always used the current folder as the destination. A MOVE therefore
targeted the folder the source was already in, and a COPY offered to overwrite
its own source. The user could not choose a destination at all. VitaShell's file
manager solves this with a copy/cut list and a paste action.

**Decision:** COPY and MOVE do not touch the filesystem. They freeze the marked
paths (maximum 64) into a local clipboard and return to WORKSPACE. The user
navigates to the destination; ACTIONS then shows PASTE first and CLEAR second
instead of COPY/MOVE. PASTE runs one COPY or MOVE command per clipboard path
into the current folder. A same-folder MOVE is disabled with `ALREADY HERE`; a
same-folder COPY is allowed and uses a free copy name. The clipboard is
independent of the marks and survives volume changes. CLEAR discards it.

**Consequences:** Local copy/move is a two-step action, as in VitaShell. The
67th mark cannot be armed; the UI reports `TOO MANY ITEMS`. Cross-pane movement
remains UPLOAD/DOWNLOAD, not COPY/MOVE. A clipboard source that is deleted before
PASTE fails safely with `SOURCE NOT FOUND`. The marks remain the visible
selection; the clipboard is a separate frozen list.

## D61. Destructive confirmations name every full path and are the only gate

**Date:** 2026-11-12
**Status:** accepted; hardens the DELETE flow.

**Context:** The DELETE confirmation could list already-deleted marks because
marks survived a completed delete. It showed bare names, so marks from different
folders were ambiguous. A high-risk delete replaced the item list with a
path-only high-risk dialog, so the user approved a set they could not see.

**Decision:** Marks are pruned against the listing that replaces them: a mark
whose parent is the listed folder and whose row is gone is removed; marks in
other folders survive (D18). DELETE always opens one `UI_CONFIRM_DELETE` with the
full path of every source, `high_risk` set if any source is high-risk, the risk
path, and the full list. The choice focus starts on CANCEL; the DELETE choice
sets `value=1` for the worker so the approved high-risk plan runs without a
second confirmation. `boot_config.txt` and protected paths are refused before
the dialog; the worker repeats the check. The dock direction for a delete is
`DELETE`.

**Consequences:** The next delete cannot silently include a previous one. A
cross-folder marked set is named by full path. A high-risk delete still requires
the DELETE choice but shows the items. A delete of a folder shows `DIR` for that
row and the full path; the worker's recursive plan remains the authority, and
the warning says the operation cannot be undone.

## D62. The FTP client gets a real connect window and names its failure

**Date:** 2026-11-12
**Status:** accepted; first FTP-client diagnostic decision.

**Context:** The only seeded site, `ftp.gnu.org:21`, is healthy: from the Mac,
`nc -vz ftp.gnu.org 21` succeeds and `curl ftp://ftp.gnu.org/` receives
`220 GNU FTP server ready.` followed by anonymous `230`. The b18 Vita log failed
at `stage=connect` after about 3.4 s, which is the old 3 s connect deadline. The
log carried neither the resolved IPv4 address nor the raw `NetResult`, and the
UI showed `NOT CONNECTED` while the worker was trying, so a network block and a
client bug looked identical.

**Decision:** The client connect deadline is 12 s, the greeting/login window
5 s, and the session budget 25 s. On `FTP_CLIENT_CONNECTING` the worker logs
`connect` with the resolved IPv4, port, and `stage=begin`; on failure it logs
`connect_fail` with the same address, port, mapped detail, and raw `NetResult`
value. The resolver result is rejected when it is `0.0.0.0`, falling back to
`gethostbyname`. SITES and the remote pane show `CONNECTING` until the session
succeeds or fails. The seeded site is unchanged; it is not the defect.

**Consequences:** A slow Vita WiFi handshake has room to complete. A failure now
has a location and a class: `value=2` at the correct address means the Vita's
network path drops port 21; `4` is a refusal; `9` is the socket/select layer.
If `value=2` appears at `209.51.188.20`, the next test is a LAN FTP server on the
PC, because no client change can open a blocked external port. The worker may
block for up to 12 s during the connect; the main thread keeps drawing the
CONNECTING state.

## D63. PASV uses the control peer's address, not the advertised host

**Date:** 2026-11-12
**Status:** accepted; supersedes the foreign-PASV refusal rule.

**Context:** The b27 localhost log shows the control connection, greeting, and
login succeeding, then `connect_fail stage=data cls=server value=0`: the server
advertised its LAN address in the 227 reply, the client's control peer was
`127.0.0.1`, and `reply_parse_pasv` refused the different host. The server
accepted one client and lost it immediately. The older external
`ftp.gnu.org` run failed at the first listing after the old 2 s window.

**Decision:** A 227 reply contributes only its data port. The client connects
to the control peer's IP with that port, ignoring the advertised host. This is
the FTP-bounce-safe choice because the untrusted server cannot redirect the
data connection to a third host, and it makes localhost, NATed servers, and
servers with multiple addresses work. The first-listing window is 10 s, command
10 s, data idle 20 s, and the session 35 s. A data connect failure stores its
`NetResult`, so `connect_fail ... value=` names the data-layer result too.

**Consequences:** Localhost no longer fails at `PASSIVE REFUSED`. A server whose
227 host is private while the control host is public now works. The app's own
server still advertises its LAN address; the client ignores it and connects to
the control peer, which is correct for both `127.0.0.1` and the LAN IP. If an
external data connection still times out at the advertised port, the network is
blocking the passive data port; test a LAN FTP server before changing the
client again. The server's data listener is a per-session port (D92), not an
ephemeral PASV port, so the port a transfer connects to is the one the 227
reply named.

## D64. Remote sessions are explicit, refreshable, and self-healing

**Date:** 2026-11-13
**Status:** accepted.

**Context:** b27/b28 proved the FTP control/login and PASV data paths, but the
remote pane still failed in use:

- Opening a remote folder left the pane on `READING` forever. The client's CWD
  completion fired `OPERATION_DONE` while the operation slot was still held; the
  worker's immediate `remote_list` saw `BUSY`, emitted `LIST FAILED`, and the UI
  had no path that cleared the eager `loading` flag.
- `REMOTE ACTIONS` listed NEW FOLDER, RENAME, and DELETE, but the worker sent
  them to the local `FsPort` planner. A remote path has no volume token, so the
  actions failed or were refused as `INVALID PATH`; remote DELETE never reached
  a confirmation.
- There was no way to close a session. Selecting another site while the client
  was `READY` returned `CONNECT REFUSED` because the old session was still held.
- A half-open control channel (server killed without a FIN, NAT expiry) kept the
  header on `CONNECTED` indefinitely; only an operation would notice.
- Remote marks never pruned after a listing because `prune_marks` used the Vita
  `path_parent`, which rejects `/`-rooted remote paths.

**Decision:**

1. **Deferred refresh.** A completed remote command sets a worker
   `refresh_remote` flag. The listing starts on the next worker step, after the
   client has released its operation slot. `OPERATION_DONE` is documented as
   non-reentrant; a consumer must not start the next operation inside it.
2. **Remote actions are real FTP commands.** NEW FOLDER, RENAME, DELETE, and
   directory DELETE go through `ftp_client_mkdir`, `rename`, `delete`, and
   `rmdir`, one command at a time through a small worker queue. `UiMark` carries
   the row type so a marked directory uses RMD. The actions are not routed to
   the local filesystem and skip the local protection table; those rules are
   for Vita paths only.
3. **DISCONNECT is a remote action.** The remote ledger is DOWNLOAD, NEW
   FOLDER, RENAME, DELETE, DISCONNECT. It closes the control and data sockets
   immediately (no QUIT round trip), clears the worker queue and pane, clears
   the site identity, and returns to WORKSPACE with `DISCONNECTED`. It is live
   while the pane is `READING` (the escape from a hung listing) and disabled
   only during an actual client transfer.
4. **Connecting always starts a new session.** Selecting a site tears down any
   existing client session first. A pre-existing `READY` session can no longer
   cause `CONNECT REFUSED`.
5. **Idle keepalive.** A `READY` client with no activity for 20 s sends `NOOP`
   and declares the session `LOST` after 5 s without a reply. A half-open
   control channel therefore cannot stay `CONNECTED`. The keepalive is an
   operation, so a user command in that short window returns `BUSY` and the UI
   clears `READING` with `BUSY - TRY AGAIN`.
6. **Row actions need a settled, connected pane.** While the remote pane is
   disconnected or `READING`, DOWNLOAD, NEW FOLDER, RENAME, and DELETE are dim
   with `NOT CONNECTED`/`READING`. The row arena is not read while loading; a
   remote error clears the eager `READING` flag so a failed CWD returns to the
   previous folder with the error in the status/dock.
7. **A lost session clears identity, keeps rows.** The site name/host and the
   remote band's site are cleared on a drop; the rows remain, marks clear, and
   the pane offers RECONNECT. The header switches to `NOT CONNECTED`. A new
   session's first listing clears the old rows even when the root path matches.

**Consequences:** Navigation, refresh after upload/mutation, remote rename and
delete, manual disconnect, and site switching all have one serialized FTP path.
A dead server is detected within 25 s even if the transport never reports the
close. Remote DELETE still requires the existing full-path confirmation; a
non-empty directory returns the server's refusal. Remote folder *transfer* is
recursive by R22 but is not part of this decision; the current engine still
downloads/uploads one file per queue item. R21's refresh command remains
unbound to an action.

## D65. Remote refresh, unsafe names, upload preflight, and the folder-transfer limit

**Date:** 2026-11-13
**Status:** accepted; items 2, 4, 5, and 6 stand. **Superseded 2026-11-17 by
D100 for item 1's six-row maximum and item 3's folder refusal:** the folder
transfer is built on the host and its console rows are PENDING.

**Context:** The b32 remote pane fixed navigation, disconnect, and remote
mutations. Four gaps remained: R21 had a `UI_CMD_REFRESH` with no action, R44
allowed a remote listing name with `/` or `:` to be flattened into a local
name, a download showed no total until the transfer finished, and an upload
silently overwrote an existing remote file. A recursive folder transfer
(R22/R23) was also still open; the user chose not to implement it in this pass.

**Decision:**

1. **REFRESH is a remote action.** The remote ledger is DOWNLOAD, NEW FOLDER,
   RENAME, DELETE, REFRESH, DISCONNECT (six rows, the settled maximum). REFRESH
   emits `UI_CMD_REFRESH`; the existing worker path lists the current remote
   folder. It is dim while the pane is disconnected or `READING`.
2. **Unsafe remote names are refused.** A listed name is unsafe when it is
   empty, `.`/`..`, or contains `/`, `\`, or `:`. The row stays visible with the
   yellow risk diamond; DOWNLOAD, RENAME, and DELETE show `UNSAFE NAME`, and
   Cross/Square return `UNSAFE NAME` without building any local path. The worker
   repeats the name check before a single-file download as a backstop.
3. **Folder transfers are refused by the current build; remote recursion is
   planned v1.** R22/R23 require a local folder UPLOAD and a remote folder
   DOWNLOAD to transfer recursively. That behavior is planned under its own
   design and not built yet. Until it lands, the current build refuses a folder
   source: those two actions are dim with `FOLDER NOT SUPPORTED`; a marked set
   containing a folder refuses the whole transfer with the same status, and the
   worker refuses a `UI_ROW_DIR` source as a backstop. A file transfer still
   works.
4. **Uploads preflight the remote destination.** When FEAT advertises MLST, the
   client sends `MLST <destination>` before a STOR. A file that exists asks
   OVERWRITE ALL / SKIP EXISTING / CANCEL once per batch; SKIP drops each
   existing file, OVERWRITE applies to the rest, and CANCEL clears the batch. A
   directory destination fails with `DESTINATION IS A FOLDER`. A server without
   MLST is not probed and keeps the prior overwrite behavior; this limit is
   named in the docs. On the Vita server and any MLST server, the prompt is the
   R41 behavior.
5. **Download totals come from the listing.** The selected row or mark size
   rides on the transfer command, the worker's `XferItem`, and the client's
   `FtpClientTransfer`; the dock shows the total before the first byte. An
   unknown size remains `--`.
6. **CANCEL at a batch conflict ends the batch.** The queue is cleared and the
   dock reports `CANCELLED`; it no longer advances to the next item.

**Consequences:** Remote browsing now has a refresh path, unsafe names cannot
become local paths, uploads ask before replacing an existing file on an MLST
server, and a partly-known download shows its size. Remote recursion is a v1
requirement, not a deferred one: R22/R23 own the behavior, it is planned under
its own design, and the UI refuses a folder instead of sending it as one
RETR/STOR until that behavior lands. The implementation should reuse the same
name, total, and conflict rules.

## D66. A session teardown owns the batch, and the queue pump iterates

**Date:** 2026-11-13
**Status:** accepted; closes the Phase 6 review pass.

**Context:** The review found that DISCONNECT and CONNECT cleared the queue
and the batch counters but not the OVERWRITE ALL / SKIP EXISTING latch, so an
answer given on one site could silently replace files on the next. It also
found `pump_queue` calling itself once per skipped or failed item; with a
2.4 KiB frame and a 128 KiB worker stack, a 64-item SKIP batch could overflow.
A local folder that stopped at the 2000-row cap was emitted as a failed
listing, the truncation flag reached the pane one listing late, and a
high-risk delete confirmation lost the console-boot sentence.

**Decision:**

1. Every path that ends a remote session (DISCONNECT, CONNECT, and
   LOST/FAILED/STALLED) calls one `reset_remote_session`: the transfer queue,
   the batch counters, the conflict answer, the remote action queue, and the
   upload preflight state are dropped together. A batch never resumes across
   a login.
2. `pump_queue` iterates; after releasing a skipped or failed item it loops to
   the next item instead of calling itself. The worker step already calls it
   every frame.
3. A listing that stops at `WORKER_LIST_CAP` is a successful truncated listing
   (`ok=1`, `truncated=1`); only a real `list_next` failure is a failed
   listing. The truncation flag is stored before the rows are applied, so the
   badge belongs to the same listing.
4. A high-risk DELETE confirmation carries the full boot warning, not only the
   ordinary "cannot be undone" line.

**Consequences:** A stale overwrite answer cannot cross a session; the queue
can no longer exhaust the worker stack; a 2000+ entry folder browses as a
truncated success; and the destructive confirmation says what is at risk. The
plan's R2, R19, R21-R23, R26, R36, R41, and M1 now describe the shipped
behavior.

## D67. The unsafe-name rule guards transfers, not navigation

**Date:** 2026-11-13
**Status:** accepted; fixes the b38 regression.

**Context:** b36/b37 marked every remote listing row containing `/`, `\`, `:`,
or a dot segment as unsafe and refused Cross on it. The Vita server's own volume
roots are `UX0:`, `UR0:`, and `UMA0:`, so the client could no longer enter any
drive while FileZilla could. R44 exists to keep a remote-supplied name out of a
local path; a CWD builds no local path.

**Decision:** A directory row opens even when its name is unsafe. Files still
refuse Cross, DOWNLOAD/RENAME/DELETE still show `UNSAFE NAME`, and Square still
refuses marking an unsafe row. The risk diamond stays on the row because its
transfer and destructive actions are restricted.

**Consequences:** The volume roots are reachable again. The unsafe check is now
scoped to the operation that can actually be harmed by the name, so a future
navigation path cannot inherit the transfer refusal by accident.

## D68. A refused action reports inside the pane, not only in the dock

**Date:** 2026-11-13
**Status:** accepted; b39.

**Context:** The b37 volume-root regression refused Cross with only the small
dock status line, and a refused CWD kept the old rows while setting a pane error
the pane never drew. The user read the silence as a software bug. The settled
error surface is inline (R65), and a recoverable refusal does not justify a
modal interrupt (R67).

**Decision:** A refused or failed action sets the pane's inline error and the
dock status. The pane band replaces its path line with the error text in yellow,
fitted with `..`. The notice clears on the next workspace key or a successful
listing. A failed listing keeps its centered `LISTING FAILED` state, and a lost
session keeps its centered detail; neither is cleared by a key.

**Consequences:** The user sees the reason next to the rows they acted on
instead of guessing. The path is temporarily hidden while the notice is up; the
dock status and the next successful listing restore it.

## D69. COPY and MOVE use the marked set or the cursor row

**Date:** 2026-11-14
**Status:** accepted; b40.

**Context:** COPY and MOVE were enabled only after a Square mark, while DELETE,
UPLOAD, and DOWNLOAD already fell back to the cursor row. With nothing marked,
the local ACTIONS list showed COPY and MOVE unavailable with `MARK TO COPY` /
`MARK TO MOVE`, which read as a connection or permission fault. R29 already
covers one item or a multi-selection.

**Decision:** When the marked set is non-empty it is the source; otherwise the
row under the cursor is the single source. Marks still win when the cursor is
elsewhere. A folder under the cursor is a valid COPY or MOVE source. The root,
protected-path, and no-selection refusals stay.

**Consequences:** One item no longer needs a mark before it can be copied or
moved, and UPLOAD is the only local action gated on the remote session.

## D70. Cyan cursor, yellow activity and risk, red failure

**Date:** 2026-11-14
**Status:** accepted; b40. Supersedes the palette sentence in D68 and the
"red is not an alert" rule.

**Context:** The report asked for a prominent selection. A yellow row with
white text would have about 1.1:1 contrast on this palette, and yellow already
means activity, marks, risk, and warnings. The theme already defined `C_RED`
without using it.

**Decision:** The active cursor row is a cyan slab with near-black text. The
mark square and risk diamond stay yellow behind a 1 px dark keyline. Yellow
continues to mean network activity, risk, warnings, and a waiting local job;
red means a failure or a refusal. Neutral messages such as `CANCELLED`,
`DISCONNECTED`, and `READING` stay white or dim. Failures and refusals use red
in the pane band, the dock failed state, the editor, SERVER, SITES, and the text
entry notice. Risk and destructive warnings stay yellow.

**Consequences:** Selection and failure no longer compete for the same colour.
The inactive pane keeps its thin bar and faint wash so only the driven pane
shows the slab.

## D71. The dock names the operation and reports one terminal per batch

**Date:** 2026-11-14
**Status:** accepted; b40.

**Context:** The dock hard-coded `DONE`, RENAME and NEW FOLDER reported as COPY,
and local multi-item work ran as `1 OF 1` per item. A batch with a failed middle
item could end green on the last item.

**Decision:** The terminal word is the past tense of the operation. A batch
reports once, with its item count, skipped count, and cancelled count. A clean
or mixed-but-successful set is green; an all-skipped or cancelled set is
neutral; a partial failure is yellow with the reason; a total failure or an app
shortfall is red. A single item omits the count and never shows `1 OF 1`. The
local pane refreshes once at the terminal.

**Consequences:** The dock answers "what happened" instead of "something ended".
The batch protocol carries a batch id, item index, total, and last flag so the
terminal can be emitted once.

**Amended 2026-11-14 (b41):** a terminal with no success at all reads `FAILED`,
not the past-tense operation word in red. `COPIED` in red beside a failure reason
claims the opposite of what line three says. A partial batch keeps the operation
word, in yellow.

## D72. One local job may run beside one network transfer

**Date:** 2026-11-14
**Status:** accepted; b49 ships the lanes, b55 the corrected local guard.

**Context:** R38 and D9 gave one slot to the client, the server, and local work.
A local copy therefore stalled or was refused while a transfer ran, although the
two usually touch different files.

**Decision:** One network transfer and one local filesystem job may run at once
when their canonical paths do not overlap. Two network transfers, two local
jobs, and any overlapping read/write pair still serialize. A reservation covers
the path and its temp/aside siblings; an ancestor/descendant conflict applies to
a tree delete, rename, or recursive move, not to a directory listing. The server
answers 450 on a held path, and disarms the data connection. (The "or while an
app transfer occupies the network lane" clause was removed by D85; the
path-scoped guard is the only exclusion.) The worker interleaves the local executor with
the client, server, and queue; there is no second worker thread.

**Amended 2026-11-14:** the containing directory is deliberately **not** reserved
by a file transfer. While `ux0:data/big.iso` transfers, copy, move, delete, and
rename of unrelated files in `ux0:data`, and NEW FOLDER inside it, all remain
allowed. Deleting, renaming, or moving `ux0:data` itself conflicts only because
it is an ancestor of a held path — that is the existing ancestor/descendant rule,
not a folder lock. A folder-wide lock was considered and rejected: it would
freeze the pane the user is most likely working in and disable unrelated work.

**Consequences:** Unrelated local work no longer waits for a transfer. The
single-slot sentence in `AGENTS.md` and `docs/PROJECT.md` is superseded; the
reservation table owns mutual exclusion.

## D73. A path collision is a confirm, not a silent refusal

**Date:** 2026-11-14
**Status:** accepted; b49 ships the lanes, b55 the corrected local guard. Supersedes the R67 interrupt
list.

**Context:** With two lanes, a later operation can target a path the first one
holds. A plain refusal loses the batch; continuing would risk data loss.

**Decision:** The later operation asks once per batch with WAIT, SKIP, or
CANCEL, naming the blocker. WAIT holds it head-of-line and resumes when the
blocker releases, re-applying the existing-destination rule if a destination
appeared. A server client cannot answer a prompt, so it receives 450 and the
data connection is disarmed. Destructive confirms, a path collision, and
unrecoverable errors are the only interrupts.

**Consequences:** A recoverable collision joins the interrupt set. The confirm
is once per batch, not once per item.

## D74. The active pane glows

**Date:** 2026-11-14
**Status:** accepted; b40. Supersedes the "only the wordmark glows" restraint.

**Context:** The active pane was a 2 px left edge and a yellow title; with the
cursor row now cyan, the pane needed a stronger cue that did not rely on the
row.

**Decision:** The active pane draws a full yellow outline that follows the chamfer
plus a static additive bloom just outside it. Only the lost pane loses its
bloom; a healthy active pane keeps it. The inactive pane keeps its dim cyan
outline and 2.5 % wash.

**Consequences:** Yellow now identifies the pane being driven as well as network
activity and risk. The glow is static; no pulse or animation is added.

## D75. A local batch can be cancelled mid-file

**Date:** 2026-11-14
**Status:** accepted; b40.

**Context:** R35 requires local operations to be cancellable, but the worker
passed `NULL` as the executor's cancel callback. The dock offered CANCEL while
the worker was blocked inside the copy, so the command could not arrive until
the operation finished.

**Decision:** The cancel confirm emits `UI_CMD_CANCEL_TRANSFER` on the dedicated
cancel slot. The executor polls a peek of that slot at every chunk and step; it
removes the partial and rolls back. The popped command is consumed once and
does not reach the next job. A between-item cancel ends the batch. The terminal
is `CANCELLED n OF m`; completed items stay.

**Consequences:** CANCEL now stops a local copy at the next chunk. A single
slow filesystem call can still delay the stop; the 5 s ceiling is measured on
hardware in M11.

## D76. A refused sync is not a failed commit

**Date:** 2026-11-14
**Status:** accepted; b43. Refines KTD12.

**Context:** b40's local copy reported `DESTINATION COMMIT FAILED` after all
253,379 bytes had been written. `ftp_temp_finish` treated the durability sync of
the temp file and the post-rename sync of the destination as commit conditions,
collapsing sync, close, and rename into one message. The post-rename sync opens
the committed file read-only and calls `sceIoSyncByFd`; nothing in the operation
needs that call to succeed, and the file is already visible under its final name.
Failing the whole copy at that point leaves a correct file reported as failed
and invites the user to retry a completed operation.

**Decision:** The temp data is still synced before the close and the destination
is still synced after the rename, but a refused sync is recorded (`sync_failed`,
logged as `sync skipped`) and does not fail the operation. `close` and `rename`
decide the commit. A close failure removes the temp; a rename failure keeps or
restores the previous destination. `ftp_temp_finish` reports which of those two
steps failed so the dock names it.

**Consequences:** A platform that refuses sync-by-fd no longer breaks every copy,
download, and upload. The durability property is "old or new, never neither"
against a process kill; power-loss durability still needs the directory-sync gap
in D48 closed. A genuine close or rename failure is now distinguishable in the
dock and the log.

## D77. The worker stack is a build-enforced budget

**Date:** 2026-11-14
**Status:** accepted; b43. Supersedes the b21 stack-size note.

**Context:** b20 overflowed the worker stack on a rename; b21 raised the stack to
128 KiB and moved the copy buffer out of `fsop_run`. b40 reintroduced the class:
`local_batch_preflight` declared `Path existing[UI_CONFIRM_MAX]` (64 × 1024
bytes), which the compiler folded into `handle_action` for a 71,680-byte frame,
and `local_batch_execute` called `handle_action` recursively. The b40 core dumps
show the worker at 158,472 bytes of a 128 KiB stack (stop `0x10006`, stack
overflow), deepest chain `handle_action → execute_action → fact_tree → fs_stat`.

**Decision:** Large per-operation arrays live in the heap-owned `Worker`, never
in a worker-path stack frame. A batch preflights once, then advances exactly one
item per `worker_step`; `handle_action` never calls itself. The worker stack is
192 KiB, and `build.sh` runs `tools/check_worker_stack.py` against the linked ELF:
`handle_action` must stay under 16 KiB, the fixed copy chain under 64 KiB, and
`handle_action` must not branch to itself. A build that breaks the budget fails
before a VPK exists.

**Consequences:** The b40 crash class is caught on the host build, not by a
console run and a core dump. A future local buffer that would cost more than a
few KiB has to be a `Worker` member and is visible in one place.

## D78. The dock shows both lanes, network first

**Date:** 2026-11-14
**Status:** accepted; b48/b49 (b41 stage).

**Context:** U25 lets a local job and a network transfer run together, but the
dock had one set of slots. A progress event from the local copy overwrote the
network transfer, and the cancel confirm could not say which lane to stop.

**Decision:** `UiState` keeps a network dock and a local dock. While both run,
the network lane keeps the word, bar, bytes, and rate, and the local lane is a
compact badge on line three (cyan running, yellow waiting, white `CANCELLING`,
red failed, dim terminal). When only one lane runs it owns the full dock; the
local terminal persists until the next local item. Events route by their
direction (`XFER_DIR_LOCAL_*` is the local lane). A focused dock with both lanes
asks `KEEP / CANCEL NETWORK / CANCEL LOCAL`, KEEP focused first; a single lane
keeps the two-choice confirm. A server-mode transfer is never cancelled from the
dock: it ends through the server-stop confirm, so when a server transfer and a
local job run together the prompt offers only KEEP and CANCEL LOCAL.

**Consequences:** Both lanes are visible without stealing focus or blanking each
other. `UI_CMD_CANCEL_NETWORK` and `UI_CMD_CANCEL_LOCAL` target one lane; the
old `UI_CMD_CANCEL_TRANSFER` still means "the running lane" for the two-choice
prompt.

## D79. The UI thread gets a stack budget

**Date:** 2026-11-15
**Status:** accepted; b51.

**Context:** The b49 review measured the UI dispatch at 183,424 bytes of stack.
The command buffer held 64 `UiCommand` values at 2,832 bytes each, and the
dispatch could re-enter itself through the IME-unavailable and command-ring-full
paths, so two frames reserved 362,496 bytes. The main-thread stack is 256 KiB:
the workspace fact-check note records the COCKPIT dumps with the stack base at
`+0x3f000`, and the project sets no `sceUserMainThreadStackSize`. D77 gated
worker frames only, so nothing would have caught this.

**Decision:** The command buffer is a heap-owned member of `App`. The two
synthetic events raised from inside the dispatch are deferred to the next frame
instead of re-entering it. `tools/check_worker_stack.py` also checks the UI
dispatch frame (4 KiB ceiling, no self-call) and the recursive folder scan
(its frame times `WORKER_REMOTE_DEPTH + 1`, 8 KiB ceiling). With the move the
dispatch frame is 80 bytes; the scan product is 1,188 bytes.

**Consequences:** A large local in the UI dispatch or in the folder scan fails
the build instead of the console. The 256 KiB figure is recorded in
`docs/VERIFICATION.md` beside the gate numbers.

## D80. A silent server session is closed

**Date:** 2026-11-15
**Status:** accepted; b53.

**Context:** The b49 review rejected the missing control-session timeout as
pre-existing, but one peer that connects and sends nothing holds the only
client slot until the user restarts the server. The review's tradeoff is real:
a client that idles mid-browse loses its session.

**Decision:** A control session with no received bytes and no active data
transfer for 30 seconds receives `421 Idle timeout.` and is closed, with one
session log line. Any received byte and any active transfer refresh the
deadline. Recovery is a reconnect; the server keepalive is not a substitute
because it is a client-side timer.

**Consequences:** A passive desktop client that browses slowly reconnects. The
panel shows the connected client until the close, so the state is visible.
M7 records the idle close.

## D81. The transfer sidecar has one owner

**Date:** 2026-11-15
**Status:** accepted; b53.

**Context:** `.vftp.part` and `.vftp.old` were built in four places, the
reservation module carried an unused recogniser, neither listing filtered the
debris, and a user file whose name ended in the suffix could be deleted by the
next write to its base name.

**Decision:** `core/path.c` owns the suffixes, the builder, and the two
predicates. The executor, the FTP temp write, and the reservation pair test use
them. `path_check_write` and a rename destination refuse a sidecar name, so the
app never creates one. `.vftp.part` is hidden from the local pane and the server
listing because it is never a valid file; `.vftp.old` stays visible so a kept
copy is reachable and is never mistaken for the user's file. The store keeps its
private `.old` and `.tmp` names inside the app data folder.

**Consequences:** Changing the convention is one edit. A file that arrives from
outside the app with a sidecar name is still removed by the next write to its
base name; that open risk is recorded in `docs/FIXES.md`. The local pane can
still show the store's `.old` debris.

## D82. The local lane starts beside the network lane

**Date:** 2026-11-15
**Status:** accepted; b55.

**Context:** D72 says a local job and a network transfer may run together, but
the worker still refused every local action while a network transfer or server
data command was active. The refusal also began a local batch before refusing
it, so a later CANCEL reported a job that never started.

**Decision:** `handle_action` refuses only when a local run or a held local plan
owns the lane. It begins the batch after that guard. Cross-lane overlap is the
reservation table's decision: a colliding item asks WAIT/SKIP/CANCEL on either
lane, and a WAIT holds the item head-of-line with a visible WAITING state.

**Consequences:** The last single-slot refusal is gone; D9 and D51's matching
clauses are superseded. The network lane can wait on a local hold and the local
lane on a network hold, and neither stops the other.

## D83. A confirm carries the prompt it answers

**Date:** 2026-11-15
**Status:** accepted; b55.

**Context:** The worker latched one pending prompt and consumed any
`UI_CMD_CONFIRM`, ignoring which dialog the answer came from. A site-delete or
stop-server answer arriving while a transfer prompt was deferred cleared the
wrong state and neither action applied.

**Decision:** Every worker confirm request carries a sequence. `PendingWork`
remembers it, the reducer echoes it on the emitted command, and the worker
accepts only its own id. A mismatch falls through to the local branches and
leaves the deferred prompt latched, so the app shows it when the screen frees.

**Consequences:** A local dialog can no longer satisfy a transfer prompt, and
the same-kind ambiguity between the three transfer prompts is closed. UI-owned
dialogs emit id 0.

## D84. The launcher icon is a centred two-arrow mark

**Date:** 2026-11-15
**Status:** accepted; b56. Superseded by D103.

**Context:** `sce_sys/icon0.png` was a placeholder. The Vita bubble launcher
masks icon art with a rounded square, so a border or an edge-to-edge panel is
clipped and reads as a broken shape. The product's settled shape is two live
panes, one network and one local, exchanging files.

**Decision:** The icon is a 128×128 true-black tile with one centred mark: a
yellow arrow pointing right above a cyan arrow pointing left, drawn in
`C_YELLOW` and `C_CYAN` with no frame, panel, glow, or text. The mark occupies a
centred 70×60 box, inside the bubble's straight edge. It ships as an indexed
8-bit PNG because `tools/check_assets.sh` and the installer require that format.

**Consequences:** The launcher icon no longer clips at the bubble mask and reads
at small size. LiveArea art remains open; a change to the mark is a new decision.
Red and green stay out of the icon so it can never be mistaken for a failure or
a server alert.

## D85. The server is not globally busy while the app transfers

**Date:** 2026-11-15
**Status:** accepted; b58.

**Context:** D72 and D82 made the canonical-path reservation table the only
cross-lane exclusion, but the server still carried the one-slot
`external_busy` gate. The worker set it from the whole network lane, so the
server refused `PASV`, `STOR`, `RETR`, and every mutating command with 450
whenever the app had a transfer active, regardless of path. The b57 trace
proved the self-deadlock: the app's own client sent `PASV` after `transfer_start`
and the server answered `450 Transfer in progress`; the transfer failed with
zero bytes. A PC client was also refused for unrelated paths.

**Decision:** Remove `ftp_server_set_external_busy` and the global 450 gate.
The server refuses a data or mutating command only when the path guard reports
a held path, or when the server itself already has a data transfer active.

**Consequences:** The app's client and its local server can transfer between
different paths, and a PC can work in unrelated paths while the console
transfers. A same-path collision still answers 450 through the reservation
table. D51's external-busy clause and D72's "or while an app transfer occupies
the network lane" clause are superseded; the held-path clause stands.

## D86. The server accepts four clients, one data transfer at a time

**Date:** 2026-11-15
**Status:** accepted; b60, hardware-observed.

**Context:** FileZilla opens a separate control connection for transfers and
keeps another for browsing. The one-client server refused the second connection
with `450 Busy`, so an upload from FileZilla could not start even though the
app's own client and its server worked. The user chose to allow multiple clients.

**Decision:** The server accepts up to four control sessions
(`SERVER_MAX_CLIENTS`). Each session owns its login, current directory, line
reader, reply buffer, idle deadline, last command, and rename state, plus its own
passive data port, bound on that slot's first PASV and held until stop (D92).
One data transfer runs at a time; a second session's data or `PASV` command
answers 450 `Another client is transferring`, and closing a non-owner session
does not abort the owner's transfer. A fifth control connection gets `450 Busy`
and is closed. The SERVER panel shows the client count and the most recent peer.

**Consequences:** FileZilla can hold its browse and transfer connections, and
the app's own client can stay connected while a PC browses. Concurrent file
transfers still serialize on the one data slot; that is the next extension if
it is needed. D9's one-client clause and R50's "one client at a time" text are
superseded. The single passive-port sentence above is amended by D92; the
four-session and ownership rules stay in force.

## D87. Access is unrestricted except boot_config and system roots

**Date:** 2026-11-15
**Status:** accepted; b61/b62, warning panel removed by D88.

**Context:** The v1 safety posture kept the app data folder out of the FTP
server, made the `tai` folders read-only to it, and asked for a second high-risk
confirmation before local writes to `tai` or boot-critical file types. The user
asked to remove those restrictions and accepted the exposure: an anonymous LAN
client can read stored sites and logs, and can write plugins into `tai`.

**Decision:** The server may read, write, create, rename, and delete in
`ux0:data/vitaFTPShell` and under `ur0:tai`/`ux0:tai`. The local manager no longer
opens the high-risk confirmation. The hard denials remain: `ur0:tai/
boot_config.txt` for every role, and the volume roots, `ur0:data`, `ux0:data`,
`ur0:tai`, and `ux0:tai` themselves cannot be renamed, moved, or deleted. The
ordinary destructive confirmation stays. `path_is_high_risk` still classifies
paths for the yellow row mark and the action target, but it no longer gates an
operation.

**Consequences:** D30's high-risk prompt, D31's confirmation, D51's high-risk
worker gate, and the server's app-data/`tai` write denials are superseded. The
SERVER warning was trimmed in b62 and removed entirely by D88;
`boot_config.txt` remains the one file every role refuses.

## D88. The SERVER warning panel is removed

**Date:** 2026-11-15
**Status:** accepted; b63; user-directed.

**Context:** The SERVER warning panel carried the anonymous read-write notice,
the `boot_config.txt` denial, the overwrite note, and the launch/stop lifecycle.
The user asked to remove the warning; the b62 trim kept the notice and the boot
denial.

**Decision:** Remove the warning region from LAYOUT_SERVER. The client panel
takes the full right column at 582, 76, 360 × 352. SERVER keeps the STATUS
readouts `ACCESS READ-WRITE`, `CLIENTS`, the ON/OFF state, the client panel, and
the header's `SERVER ON n CLIENT(S)` repetition. The `boot_config.txt` denial
and the overwrite behavior remain path and transfer rules.

**Consequences:** SERVER no longer discloses anonymous read-write or the
`boot_config.txt` denial; those remain in the README, the project docs, and the
path policy. The layout table has five regions. D87's warning sentence is
superseded. The user accepted the reduced disclosure.

## D89. A transfer that loses its final reply keeps a complete download

**Date:** 2026-11-15
**Status:** accepted; host-tested; console pending.

**Context:** A server can send every payload byte and then go silent before the
final 226. The client had no deadline in that state, so the lane latched until
the user cancelled, and cancelling deleted a complete file. Removing the data
deadline was deliberate; the payload is finished, only the reply is missing.

**Decision:** Arm a deadline equal to the command timeout when the data socket
reaches EOF, for RETR and STOR alike. On expiry, keep a download only when the
expected size is known and met: commit the temp to its final name and report
`FINAL REPLY MISSING` with the file kept. A short or unknown-size download is
removed. An explicit cancel still discards (KTD8). The control session ends so a
late reply cannot be read by the next command; remaining queued items are named
in the terminal. An upload that loses the reply ends with nothing kept.

**Consequences:** The dock keeps the operation word in yellow for a kept
download and drops `NO PARTIAL KEPT`. The terminal records a network cause, so
D33 counts it as network-interrupted, not app-class.

## D90. A session may read metadata for the path it is transferring

**Date:** 2026-11-15
**Status:** accepted; host-tested; console pending.

**Context:** The path reservation guard answered 450 for the exact file a
session was uploading, so a PC's `SIZE`/`MLST` of its own destination looked
busy. The guard is lane-aware only by lane, and four sessions share one lane.

**Decision:** The server lets the owning session read `SIZE`, `MDTM`, and `MLST`
for the path of its active or pending transfer. An upload destination answers
550 because the new file does not exist until the commit; the old entry's size
is never returned. Every write, delete, rename, listing, and every other session
stays guarded. The reservation check stays lane-blind by design; per-session
exclusion is the server's job.

**Consequences:** FileZilla can probe its own upload without a false busy. A
foreign session still gets 450, and no cross-session read is opened.

## D91. The upload preflight falls back from MLST to SIZE

**Date:** 2026-11-16
**Status:** accepted; host-tested; console pending. Supersedes D65 section 4.

**Context:** D65 section 4 and R41 said a server without MLST is never probed,
so an upload to such a server kept the stale-overwrite behavior. Servers that
do not advertise MLST often still answer SIZE, which is enough to detect an
existing destination and to report its size. The user asked this pass to close
that gap without changing D86-D90.

**Decision:** The upload preflight prefers MLST when the server advertises it.
If MLST is refused with 500/501/502/504, the worker asks SIZE once for the same
destination; 213 means the file exists (and carries its size), 550 means it is
missing, and 500/501/502/504 means the server cannot report it. A server that
offers neither probe starts the STOR directly, as before. The probe choice
sticks for the session and resets with the session; a result whose destination
no longer matches the active item is discarded. A SIZE-only probe cannot report
a type, so a destination the current remote listing shows as a folder is still
refused with `DESTINATION IS A FOLDER`; a destination outside that listing
relies on the server's STOR refusal.

**Consequences:** A PC or NAS server with SIZE but no MLST now gets the
one-per-batch overwrite prompt instead of a silent replace. D65 section 4 is
superseded; D65's other clauses and D41's server-side overwrite stay in force.
R41 is amended to name both probes.

## D92. The server gives each session slot its own passive data port

**Date:** 2026-11-16
**Status:** accepted; host-tested; console pending. Supersedes D39's port rule
and amends the single-port sentences in D59, D63, and D86; their other rules
stay in force.

**Context:** The server held one passive listener on the control port plus one
and told its sessions apart only by the control peer's address. Two sessions
from one PC (FileZilla's browse and transfer connections) share that address,
so a data connection arriving on the shared port could not be attributed to the
session that armed it. The port editor also accepted 65534 as a control port,
which leaves no room for four consecutive data ports.

**Decision:** The server binds only the control port at start. Each of the four
session slots owns `control + 1 + slot`, bound on that slot's first PASV and
held until the server stops. A PASV reply advertises the arming session's own
port, and a data connection is attributed by the port it arrives on. When a
slot's data-port bind fails, that PASV answers 425 and the server stays ON with
no session armed; a control-port bind failure at start leaves the server OFF.
The SERVER port editor and the settings loader accept 1024–65531, so the last
slot's port cannot exceed 65535.

**Consequences:** A router forward for the server becomes four ports, one per
slot; session slot 0 keeps the old port, one above the control port, so a
single-client setup is unchanged. The port identifies the owner while a session
holds its slot: a slot's listener outlives its session, so a stale data
connection from the previous session's PASV that arrives after the slot is
reused can still match the new owner's control peer by address. Closing that
window needs the deferred ephemeral-per-PASV listener. A data-port conflict costs that one PASV
instead of stopping the server. D39 is superseded, and D59's and D63's port
clauses and D86's single-PASV-port sentence are amended; the address,
client-PASV, four-session, and ownership rules in those entries stand.

## D93. One fact, one home

**Date:** 2026-11-16
**Status:** accepted; user-directed.

**Context:** Status prose was copied into every summary, each copy pinned to a
different build, so a build left at least two of them false. The scope statements
drifted the same way. A fact written twice is a fact with two versions.

**Decision:** Each kind of fact has exactly one owner: `PROJECTS.md` the board,
`docs/VERIFICATION.md` the hardware evidence, `docs/FIXES.md` the defect index,
`docs/DECISIONS.md` the choices, the v1 plan the behavior, `STRATEGY.md` the
direction, and `docs/DESIGN.md` the layout. `docs/README.md` carries the
ownership table. Every other document links to the owner and carries only a
short current state. History stays in git, dated plans, and the evidence and
fix records.

**Consequences:** A summary that copies a volatile fact is a defect, not an
update. Changing a fact edits its owner and the links to it. Structural drift is
checkable, and `tools/check_docs.py` reports a markdown file missing from the
ownership table.

## D94. The documentation gate is the mechanical check on the docs

**Date:** 2026-11-16
**Status:** accepted; user-directed.

**Context:** The only link and fence check lived in the workspace-root
instructions and ran by hand; the host suite checked layering, tests, previews,
and assets, never prose. Nothing caught a summary drifting from its owner.

**Decision:** `tools/check_docs.py` is authoritative for broken markdown links
and cited `docs/` paths, unbalanced fences, plan and design indexing, the
ownership table, R/U/KTD/D references and whole-set ranges, build tokens outside
their owners, the shared status block, the traceability table, and the
retired-phrase list. It runs from `tools/test/run.sh` and CI and fails on a
finding. While the sweep was in progress, a baseline file recorded the violations
that predate the gate: entries key on file, check, and matched text,
may only be removed, and a regeneration that would add one fails; the file is
deleted when the count reaches zero. Archived files, dated plan bodies, and
historical evidence rows are exempt from the phrase check, and the decision
record keeps its own wording so superseded history survives.

**Consequences:** A documentation change is checked like code, and the baseline
is a count of remaining drift that the sweep drives to zero before deleting the
file. The gate proves structure and known wording, not the truth of prose nobody
has written; a new scope claim still needs review, and the gate does not claim
otherwise.

## D95. v1 verification is one console pass at code freeze

**Date:** 2026-11-16
**Status:** accepted; it supersedes the board's install-and-run step, not any
product rule.

**Context:** b74 is packaged and host-tested, and the matrix still carries rows
written for earlier builds. The owner chose not to run a console row per build:
the v1 code is not complete, so a row that passes now proves a build that will
change. The matrix is the test script for the finished product, and it should be
read as one.

**Decision:** `docs/VERIFICATION.md` is the one home for the matrix. A row is
added or amended when the behavior it proves lands, names what the user does,
and states what pass means. The matrix is executed once on the v1 code-freeze
build, as a test user would; that pass promotes rows from PENDING or PART. No
interim build claims a console row, and host evidence never becomes `HW`. A row
is PASS only with its observed result and evidence. A defect found in the final
pass is fixed, and the affected rows run again on the new build.

**Consequences:** b74 stays host-only and every row stays PENDING or PART. The
board's next action is the remaining code, not a console run. The matrix grows
as v1 completes, including the remote-recursion rows when R22/R23 land, and the
plan points at the matrix instead of duplicating it, so a new row cannot drift.

## D96. The local panes list every user-accessible mount

**Date:** 2026-11-17
**Status:** accepted; user-directed. It supersedes D53's three-volume list and
the three-volume local-manager scope in `AGENTS.md`, `README.md`, and
`docs/PROJECT.md`. The FTP server's exposure is not superseded.

**Context:** The local side was narrower than the console. `ALL DRIVES` offered
`ux0:`, `ur0:`, and `uma0:` only, so a user with an internal card, a game card,
or a reason to inspect the app folder needed another tool. A file manager that
cannot reach the console's storage is a transfer tool with a browser attached,
which is not the product's reason to exist. An anonymous server, however, must
not gain new writable surfaces because the local list grew.

**Decision:** One volume table in `src/core/path.c` serves every role. The local
panes list `ux0:`, `ur0:`, `uma0:`, `imc0:`, `gro0:`, `grw0:`, and `app0:` in
that order when mounted. An absent mount is an absent row, not an error, and
the list is built on entry so a mount inserted or removed while it is open
appears on the next entry. Save data (`savedata0:`) and the protected system
partitions (`vs0:`, `os0:`) are not in the table and never appear.

`gro0:` and `app0:` are read-only. Their row value reads `DRIVE RO`; a write,
create, paste, rename, move, delete, or download destination there is refused
with `READ-ONLY MOUNT` and changes nothing, while a copy from the mount and a
read stay allowed. The mount spelling decides, so the install tree stays
writable through its `ux0:` spelling (D99).

Every listed volume root is protected from rename, move, and delete. `ux0:data`,
`ur0:data`, and `imc0:data` are protected as system data roots; the `ur0:tai`
and `ux0:tai` rules are unchanged. Free space is queried at the volume root and
cached per volume (D59); a value the platform cannot report renders `--`.

The FTP server keeps a fixed three-mount allowlist, `ux0:`, `ur0:`, and
`uma0:`, independent of the local list. It filters the same table in both its
path mapping and its root listing, so a server path on `imc0:`, `gro0:`,
`grw0:`, or `app0:` is refused and creates nothing.

**Consequences:** The local workspace reaches every user-accessible mount while
the anonymous server's writable surface is unchanged. A read-only destination
fails before a plan exists instead of surfacing an adapter error. `gro0:` is
assumed read-only and `grw0:` is assumed writable; both are `[C]` until the
console confirms them, and a refused `grw0:` write surfaces the platform reason
rather than a read-only promise. The PCH-1000 cannot mount `imc0:`, so that
volume's matrix leg is BLOCKED and the host fakes are its only coverage.

## D97. The right pane has local and remote views over one session

**Date:** 2026-11-17
**Status:** accepted; user-directed. It extends D53's start view and supersedes
its single-local-pane assumption. It does not change the FTP session rules.

**Context:** Moving files between two folders forced the single local window to
walk back and forth, losing the source selection and the place in the list. A
second FTP session or a switchable left pane would double the remote state the
UI and the worker have to keep coherent. The user asked for a right-pane switch
that keeps the session and its work alive.

**Decision:** The UI has three views: the left local view, the right remote
view, and a right local view. The left view is always local and is the stable
anchor; the right pane shows one of its two views, selected by `right_view`.
`UiPane` is the view key, so each view owns its folder, cursor, scroll, marks,
and listing. The left-local value stays at index 0.

The switch is a view change, not a disconnect. The FTP session, its folder,
cursor, scroll, marks, and any running transfer are preserved, and switching
back restores the view. The right local view opens at `ALL DRIVES` the first
time and returns to its last folder for the session; a view whose folder no
longer exists falls back to `ALL DRIVES` when the failure is discovered. The
mode is session state: every launch starts left local plus right remote, and
the mode and the right local folder are not persisted.

The right local view is a full peer of the left: the same actions, marks, free
space, protection, and one shared clipboard. A download targets the left local
view; an upload targets the remote view only while it is shown. With both panes
local, UPLOAD is dim on both with `NO REMOTE VIEW`, Cross on a file starts no
transfer, the idle hint is row-aware (`X TO OPEN` on a folder or volume,
`NO REMOTE VIEW` on a file or marked set), and the dock never prints `REMOTE`.

The switch is an ACTIONS row only on the right pane: `SWITCH TO LOCAL` on the
remote view, `SWITCH TO REMOTE` on the local view. The right panel grows to
seven rows and both panels grow to 360 px, with the 44 px pitch kept and the
target panel aligned. A switch is handled in the reducer, emits no worker
command, and stays live while the remote view is lost or disconnected.

A successful connection started from SITES closes SITES, returns to the
WORKSPACE, and activates the remote view; when the user has already left SITES,
the view switches but the current screen and focus stay put. A failed connection
stays on SITES with its reason.

A local operation reloads any other local view whose folder is the exact parent
of a changed path, restores the cursor by name, and prunes marks for names that
are gone; a view showing an unaffected folder stays as it is.

**Consequences:** A user copies between two local folders without losing either
place, and one FTP session stays singular. Three row arenas total about 5.04 MB
against the named 6 MB listing-arena budget, asserted by a host test; a fourth
view is not planned. The ACTIONS panel is 8 px taller than before and still
clears the dock by 4 px. A hidden view can lose its folder; the fallback happens
when the view is next listed or shown, not by polling.

## D98. A same-folder paste asks duplicate or cancel

**Date:** 2026-11-17
**Status:** accepted; user-directed. It supersedes D60's silent same-folder copy
and its "cross-pane movement remains UPLOAD/DOWNLOAD, not COPY/MOVE" clause.

**Context:** Copying a file into the folder it came from is the sharpest paste
case: the destination already exists and is the source, so overwrite cannot mean
anything. The build silently created `A copy.TXT` without telling the user,
which looks like the copy did nothing or that the original changed. A mixed
batch made it worse: a prompt that counted already-here items as replacements
would ask about destinations that can never be replaced.

**Decision:** A paste whose items all already live in the destination asks
`ITEMS ALREADY HERE` with CANCEL and SAVE DUPLICATES. CANCEL changes nothing.
SAVE DUPLICATES names each item with the copy rule (`A copy.TXT`, then
`A copy 2.TXT`) and never overwrites the source; a folder duplicates
recursively. The duplicate prompt is its own confirm: the destination folder is
the context line, the item list is the sources, `SAVE DUPLICATES KEEPS THE
ORIGINAL.` is the warning, CANCEL is focused first, and SAVE DUPLICATES is the
yellow slab.

A same-folder MOVE stays refused with `ALREADY HERE` before a plan
exists. Any other paste with at least one existing destination asks the existing
OVERWRITE ALL, SKIP EXISTING, and CANCEL prompt once. That prompt counts only
the destinations that would be replaced; an already-here item is named in the
list as `STAYS`, counts as skipped, and offers no duplicate choice. In a mixed
MOVE an already-here item stays in place instead of failing the batch.

The question is decided by the destination path, so it fires in either local
pane, including a paste in the other pane at the folder the items came from. One
exported copy-name function is the only owner of the `A copy` rule, so the
prompt, the preflight, and the plan cannot disagree. A self-copy step is never
planned.

**Consequences:** A same-folder copy is a visible choice, and a cancel is truly
a no-op. A batch asks one question and its terminal counts the already-here
items as skipped. The preflight must see a same-folder item as a destination,
not skip it: the parent comparison uses the canonical path identity rather than
`strcmp`, so a case variant or the `app0:` alias is recognised. The current
build has no overwrite-onto-itself path.

## D99. `app0:` is a comparison-only alias for the install tree

**Date:** 2026-11-17
**Status:** accepted; the platform equality is a `[C]` claim until the console
confirms it.

**Context:** `app0:` is how the console names the running application's install
tree, and `ux0:app/VFTPSHELL` is the same tree through the memory card. Copying
between the two spellings would otherwise look like a copy between two folders,
and a same-folder check would miss it. The install tree must stay writable
through the memory-card spelling so a user can repair a file or replace the
app's assets, but `app0:` is a read-only mount by platform contract (D96).

**Decision:** `app0:` and `ux0:app/VFTPSHELL` compare as one location for the
same-folder, ancestry, and reservation checks. Canonical and display paths keep
the spelling the user typed, so navigation, error text, and the log never
rewrite a path. The identity is comparison-only: it is not a second name for
the mount and it creates no writable surface. A write through `app0:` is
refused as `READ-ONLY MOUNT`; the same path written through its `ux0:` spelling
is writable. A copy or move between the two spellings is treated as the same
folder, so a copy duplicates instead of replacing and a move is refused.

**Consequences:** The alias prevents a self-copy or self-move that spelling
alone would hide, and a reservation on one spelling blocks the other. The
equality is `[C]`: `app0:` is assumed to resolve to `ux0:app/VFTPSHELL` on 3.65
Enso. The target value is owned by the filesystem adapter (`psp2_fs.c`) and
supplied once by the composition root, so the unverified claim stays out of
policy (AGENTS.md); until it is supplied, the alias comparison fails closed.
Matrix M24 confirms the mount and its read-only refusal; if the console reports
a different target, the adapter value moves and the decision is superseded.

## D100. A folder transfer is one job, and the remote ledger grows to eight rows

**Date:** 2026-11-17
**Status:** accepted; supersedes D65's six-row maximum and its folder refusal.
The console rows are PENDING.

**Context:** The v1 plan's R22 and R23 were the last unbuilt requirements. The
remote ledger was six action rows plus the switch (D65 item 1), and a folder
source was refused with `FOLDER NOT SUPPORTED`. A tree cannot be one queue
item per file: the queue holds 64 descriptors, and a second transfer would
interleave between a walk's listings while the client is briefly `READY`. The
copy and move model from the local panes did not reach the network boundary.

**Decision:**

1. **One heap job owns the transfer.** A `FolderJob` holds a 4,096-entry plan
   with a 512 KiB path arena and an iterative directory worklist. Its scan,
   conflict question, move confirmation, copy, verification, and delete run on
   the network lane; `pump_queue` and `pump_remote_actions` return while it
   exists, and every exit reaches one terminal.
2. **The remote ledger has eight rows.** DOWNLOAD, MOVE, NEW FOLDER, RENAME,
   DELETE, REFRESH, DISCONNECT, SWITCH. PASTE takes DOWNLOAD's slot while a
   local clipboard is armed. The panel keeps its 44 px rows and scrolls in
   whole rows with an indicator.
3. **Folder sources are valid.** A folder DOWNLOAD or UPLOAD, and a marked set
   that mixes files and folders, become one plan. The conflict question names
   up to 16 destinations and counts the rest; an existing destination folder
   merges, and a folder planned over a file is skipped with
   `DESTINATION IS A FILE`.
4. **D65's six-row maximum and its folder refusal are superseded.** The rest of
   D65 (REFRESH, unsafe names, the upload preflight, download totals, the
   batch CANCEL) stands.

**Consequences:** The ledger scrolls; `docs/DESIGN.md` and the matrix row that
says "seven-row ACTIONS" move to eight. The job is one lane owner, so a second
folder transfer is refused or dimmed while it runs, and a file transfer queues.
A tree beyond the plan capacity is refused before the first byte.

## D101. A cross-pane move verifies, keeps the copy, and leaves a report

**Date:** 2026-11-17
**Status:** accepted; supersedes R42's cross-volume rollback clause for
cross-pane moves. The console rows are PENDING.

**Context:** R42 says a cross-volume move is all-or-nothing: if the source
delete fails, the destination is removed so the volumes never disagree. A
cross-pane move is not a rename. Its copy is a network transfer, and removing a
verified copy after a failed delete discards the only copy the user has while
the source may already be gone. The plan also needed a durable record of what a
partial transfer did not move.

**Decision:**

1. **The copy verifies before any delete.** Each destination folder is
   re-listed and every planned entry must match by exact name and by the bytes
   the transfer completed for it. An unknown size, a case-only name, a short
   file, or a failed listing leaves the source in place.
2. **The delete is bottom-up and all-or-nothing per phase.** `path_check_delete`
   runs on every source root before the first remove. The delete stops at the
   first failure or a cancel between commands; that entry's verified copy
   stays, the undeleted sources are named, and the job ends `MOVED` partial.
3. **A failed delete never removes a verified copy.** This supersedes R42's
   cross-volume rollback clause for cross-pane moves; R42's temporary-write and
   partial rules still govern the copy itself.
4. **`skipped.txt` records the transfer.** One report goes in the folder above
   the transferred root: a header naming the direction and both roots, every
   entry the transfer did not complete, and the reason. It is replaced on every
   transfer that reaches its copy phase; a cancel before the first byte writes
   none. A directory is never replaced, and a file the transfer itself wrote
   under that name takes the free name. A failed report write changes nothing
   about the transfer and is named in the dock.

**Consequences:** A partial move deliberately leaves duplicates; the report and
the dock name them. A move on a server that reports no sizes stays a copy. The
report is a record, not an import; retrying means selecting the entries by
hand.

## D102. Read-only is per folder, and free space is a preflight

**Date:** 2026-11-17
**Status:** accepted. The console rows are PENDING.

**Context:** A server can be writable in one folder and not another, and FTP
answers `550` for a missing path as readily as a denied one. A session-wide
read-only latch would dim every write after one refused file, and the app's own
server answers `550` for protected paths. Free space is only proven on `ux0:`
today; the platform reports unknown for the other volumes.

**Decision:**

1. **Write capability belongs to the current remote folder.** It is read from
   the folder's own `perm` facts, in a listing's cdir/pdir row or an MLST after
   connect and after each navigation. A folder that grants no write, create,
   delete, or mkdir is read-only; UPLOAD, PASTE, MOVE, NEW FOLDER, RENAME, and
   DELETE dim with `READ ONLY`. A refusal fails only its entry and never marks
   the folder, and a server that reports no `perm` stays enabled.
2. **A read-only destination subtree is skipped at the walk.** It is recorded
   with its reason, so the conflict question and the report stay truthful.
3. **Every download sums its known bytes before the first byte.** A known
   shortfall refuses with `NOT ENOUGH SPACE`; an unknown free space skips the
   check and renders `--`; an unknown size is a lower bound. The per-volume
   cache is invalidated when a transfer writes to that volume.

**Consequences:** Only `ux0:` reports free space today; the code-freeze pass
measures whether `uma0:` and `ur0:` can. An unknown folder stays enabled, so a
server that reports nothing is never dimmed by a guess.

## D103. The launcher mark is a stack of files

**Date:** 2026-11-17
**Status:** accepted. The console rows are PENDING.

**Context:** D84's two-arrow mark reads as a transfer glyph — the symbol any
FTP or sync app uses — and says nothing about files. The launcher is where a
person decides whether the app is a file tool, so the mark has to show files.
Six directions were drawn and reviewed: a two-lane sync loop, two plain
chamfered panes, a split exchange bar, a split folder, a ledger window, and a
folder carrying a file window. The user chose the file stack.

**Decision:** `sce_sys/icon0.png` is a 128×128 true-black tile carrying a stack
of three sheets. The back sheet is `C_DIM`, the middle `C_YELLOW`, the front
`C_CYAN`, each 60×60 with a 12 px top-right chamfer and a 3 px field keyline,
offset 10 px diagonally. The front sheet is the document: `VTPS` set vertically
down its left margin in the app's 5×7 face, knocked out in `C_FIELD`, with four
ruled lines on the right. The mark is 83×83, centred on (64, 64), inside the
bubble's corner cut. It is drawn at 3× and box-filtered, then written as an
indexed 8-bit PNG by `tools/art/build_launcher_art.py`. Geometry and the mask
check are in `docs/design/2026-11-17-launcher-art.md`.

**Consequences:** The icon reads as files at launcher size and survives the
32 px row, while `VTPS` stays a signature detail that only resolves from about
96 px up. D84's geometry note is superseded but kept. Red and green stay out of
the icon. The art is reproducible: the script reads the palette from
`src/gfx/theme.h` by hand and the glyphs from `src/gfx/font.c`, and
`tools/check_assets.sh` now guards `pic0.png` too.

## D104. The LiveArea shows the workspace, and pic0 is the boot frame

**Date:** 2026-11-17
**Status:** accepted. The console rows are PENDING.

**Context:** `bg.png` and `startup.png` were two dark rectangles with a yellow
rule, and `pic0.png` did not exist. A LiveArea is the only place a person sees
the app before it draws: the page behind the gate, the gate thumbnail, and the
loading frame all ship empty-handed today.

**Decision:**

1. **The LiveArea background is the workspace, composition A.** 840×500: the
   wordmark at 48, 30 at px 7 with the settled halo and chroma; the descriptor
   `FTP CLIENT / FILE MANAGER / ANONYMOUS SERVER` at px 2.5; two 360×212
   chamfered panes at 48 and 432; the local pane's third row a cyan cursor slab;
   `UX0:  UR0:  UMA0:` and `READ-WRITE` at y 370. The lowest element ends at
   y 382, because the shell covers the bottom of the page with its gate.
2. **The gate thumbnail is the mark, the wordmark, and one line.** 280×158, no
   data and no state.
3. **`pic0.png` is the boot frame.** 960×544: the mark at 200 px, the wordmark
   centred at px 6, the descriptor at px 2.5. It reports no progress and no
   version, because the platform reports neither during load.
4. **`template.xml` stays `style="a1"`.** No `psmobile` frames, no extra
   LiveArea pages, until a decision earns them.
5. All three are indexed 8-bit, from `tools/art/build_launcher_art.py`, and
   `pic0.png` joins the VPK and the asset guard.

**Consequences:** The page shows the product instead of a brand abstraction and
still reads at a glance. The bottom fifth is deliberately empty. The rejected
poster and ledger-field compositions are recorded in the design sheet and can
return as additional LiveArea pages if the gate ever gets a second page.

# Architecture

**Status:** settled by the v1 plan. Phases 1-5 have built the project shape, the
shared foundation, the protocol engines, the operations and state layer, the
screens, the adapters, and the runtime: the build and packaging, the six port
contracts and host fakes, the gfx base and layout table, the reply/listing/path/
format policy, the site/settings/store/log policy, the passive FTP client and
server, the transfer queue, the local planner and executor, the input reducer
and UI state, the handoff, the seven screen surfaces, the six Vita adapters, and
the app/worker composition. The first console launch is U14. The plan
[`plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md)
owns sequencing and the unit-level detail; this document owns the shape.

## The dependency rule

Source dependencies point inward, toward policy. Policy is what the app decides:
protocol state, path rules, queue order, conflict policy, screen transitions.
Details are how it is carried out: SceNet, sceIo, SceCtrl, the IME, vita2d, the
filesystem.

`src/core/` compiles and runs on the host with no `psp2/` or `vita2d.h` header. If
a policy test needs a real socket or a memory card, the boundary is in the wrong
place.

## Layers

| Layer | Path | Responsibility | Constructs | May include |
|---|---|---|---|---|
| Policy | `src/core/` | Protocol state machines, replies, listings, paths and risk, site book, store, log events, transfer queue, file-operation planner, input reducer, UI state | no | C standard library |
| Ports | `src/platform/*.h` | Plain-C contracts for net, filesystem, input, text entry, clock, log | no | C standard library |
| Adapters | `src/platform/psp2_*.c` | SceNet, sceIo, SceCtrl, IME, clock, session log | no | `psp2/*` |
| Renderer | `src/gfx/` | Theme, 5x7 face, rectangles, lines, clipping; `vita2d` only in `draw.c` | no | `vita2d.h` in `draw.c` only |
| Presentation | `src/ui/` | Draws `UiState` and maps input to commands; returns nothing | no | core, gfx |
| Runtime | `src/app/` | Frame loop, worker thread, command and event rings, snapshot handoff | no | core, ports, ui, gfx |
| Composition | `src/main.c` | Constructs the adapters, starts the runtime, owns shutdown | yes | everything below |

`main()` is the only composition root. `src/app/` runs what `main()` built and
never constructs an adapter, which is what lets the host integration test link it
against fakes.

## Ports

The core declares these in its own types. No `Sce*` type, socket handle, or file
handle crosses inward.

- **`NetPort`** — resolve a host, connect, send, receive, close, listen, accept
  (returning the peer address), an aggregate wait over a handle set with a
  timeout, and a link query returning up or down with an address that is
  explicitly unknown. All by opaque handle; a handle the caller did not create is
  invalid. Errors are plain enums, not `SceNet` codes. The aggregate wait exists so
  no file outside `psp2_net.c` names `select`.
- **`FsPort`** — list one directory entry per call against an open handle, stat,
  open, read, write, sync, close, create a directory, remove a directory, rename,
  remove a file, read free space, and report whether a rename replaces an existing
  target. Free space is "known" or "unknown", never zero for unknown.
- **`InputPort`** — the pressed-edge set and the stick position.
- **`TextInputPort`** — open the system IME with a title, initial text, password
  flag, and maximum length; report open, cancelled, unavailable, or a UTF-8
  result.
- **`ClockPort`** — monotonic milliseconds and a sleep.
- **`LogPort`** — append one bounded record.

Adapters return a plain error; the core maps it to a user-facing reason and a
failure class. A platform value that cannot be read renders `--`.

## Protocol engine

One engine in `src/core/ftp/` serves both roles and shares the reply parser, the
listing formatter, the path table, and the transfer planner. Nothing from
VitaShell or libftpvita is vendored (D40).

- **Client:** resolve, connect, read the greeting, log in (anonymous or
  user/password), set binary mode, read the working directory, list, browse,
  download, upload, create, rename, delete, and quit. Passive only.
- **Server:** listen on the control port and the data port, accept one client, log
  in anonymously, list, change directory, download, upload, create, rename,
  delete, and quit. Passive only. Every path resolves through the shared table and
  is re-checked after normalization.
- **Shared:** reply parsing, multiline replies, PASV parsing, path joining and
  parent rules, the risk table, the denial table, the conflict gate, and the
  temp-and-rename write.

The engine is an event-driven state machine: command sent, reply line received,
data readable, data writable, deadline expired, cancel, peer closed. Each stage
carries a deadline. It never blocks on a file body; the worker pumps a bounded
amount of work per tick.

**Engine shape (U6/U7).** `src/core/ftp/ftp.c` holds the pieces both roles need:
the incremental control-line reader (4096-byte cap), the remote path normalizer
(`..` clamps at `/`; no local `path.c` use), the cause and stage names, and the
templed write (`<target>.vftp.part`, a stale `<target>.vftp.old` recovered or
cleared, file sync before rename, and the aside fallback when the platform cannot
replace). The client (`client.c`) and server (`server.c`) own their state
machines and expose one event callback each; neither owns a UI or a listing
arena.

- **Client:** `FtpClient` reports `STATE`, `LISTING_BEGIN/ROW/END`, `PROGRESS`,
  and `OPERATION_DONE`. The machine is `IDLE`, `RESOLVING`, `CONNECTING`,
  `GREETING`, `LOGGING_IN`, `READY`, `LISTING`, `TRANSFERRING`, `STALLED`,
  `LOST`, or `FAILED`; the data side is `NONE`, `ARMED`, `ACCEPTED`, `ACTIVE`,
  or `DRAINING`. The connect budget is 10 s with caps of 3 s resolve, 3 s
  connect, 2 s greeting/login, and 2 s first listing; a control command has 5 s;
  a data operation has a 15 s idle deadline. A data-only failure sends `ABOR`,
  records `cls=network` or `storage`, and keeps the control session; a control
  drop records `LOST`. The client tolerates either ABOR reply order with a
  250 ms drain grace, during which a new command is refused rather than allowed
  to consume a trailing `426`.
- **Server:** `FtpServer` reports `STATE`, `CLIENT`, `PROGRESS`,
  `TRANSFER_DONE`, and `ERROR`. The machine is `OFF`, `BINDING`, `ON`,
  `SESSION`, `REFUSING`, or `STOPPING`; the data side uses the same five
  sub-states. It binds control and control+1 on the LAN address, accepts one
  client, refuses a second with 450, drains the data backlog at every PASV,
  accepts a data peer only when its address equals the control peer's, and
  disarms on every non-data command and every refusal. A transfer holds the one
  slot; a data or mutating command during it answers 450. `STOR` writes a temp
  and renames on close; a reset or timeout aborts the temp and leaves the
  previous destination byte-identical. Network loss closes both listeners and
  returns `OFF`.

**Operations and state (U8/U16/U17/U9/U15).**

- `src/core/xfer.c` is the one slot: an ordered queue of descriptors, one active
  item, one busy fact, the R41 decision recorded once per batch, and a latched
  terminal outcome. It never touches a file or a peer.
- `src/core/fsop_plan.c` is pure planning. It takes the action, the sources, the
  destination, and the directory facts, and returns ordered steps, a rollback
  list, the conflict gate, and per-step risk. It never calls `FsPort`; a
  volume-root source, a copy into itself, or a protected delete is refused
  before any step exists.
- `src/core/fsop_run.c` walks the plan through `FsPort` with the templed write.
  A copy rollback removes only a destination the plan created; an overwritten
  destination that reached commit is left new. A cross-volume move is atomic per
  file: the source is deleted only after the rename, and a batch that fails
  after some moves leaves the completed moves and reports `n OF m MOVED` rather
  than deleting the only copy (D45).
- `src/core/input.c` turns edges and the stick into semantic events with repeat.
  `src/core/uistate.c` is the only writer of `UiState`; `src/core/uiact.c` is the
  command vocabulary. The reducer emits values, never calls the engine.
- `src/core/handoff.c` carries commands and events across fixed rings. A server
  stop and a cancel have dedicated slots and are never lost to a full command
  ring; progress is a coalesced seqlock latch, so a terminal state cannot be
  overwritten; each pane has three listing generations; the site book uses a
  16 KiB bulk lane (D49); the server state and a worker confirmation are
  latched; the query result is a fixed record.

## Screens and runtime (U10-U13)

- `src/ui/` is a pure presentation layer. `render.c` dispatches the current
  screen, draws the shared header, and never includes a platform header.
  `workspace.c` and `dock.c` own the two panes and the five dock states; the
  other five files own SITES, SITE EDITOR, ACTIONS, SERVER, CONFIRM, and the
  text-entry notice. `parts.c` holds the shared frame, keycap, wash, and slab
  primitives.
- `font_set_trace` is a host-only trace hook: the render suites assert the
  string, position, and colour, because glyph rectangles alone cannot name a
  label. The app leaves the hook NULL.
- `src/app/app.c` owns the frame loop and `UiState`; `src/app/worker.c` owns the
  client, server, queue, local executor, site book, settings, and log. The app
  polls input, decodes repeat, feeds the reducer, pushes commands, drains
  events, acquires one listing generation per pane per frame, draws, and
  releases. It never opens a socket or touches a file.
- In the product the worker runs on a POSIX thread. The host integration test
  passes `worker_thread = 0` and calls `worker_step` inline through `app_tick`;
  the same worker code runs either way (D49).
- The worker marks the server external-busy while an app-side transfer or queue
  owns the one slot, so a PC data command answers 450 (D51). A local action
  refuses while a transfer or server data command is active, and refuses a plan
  whose facts overflow the scan cap rather than planning a subset.
- The app treats a failed site save as `SAVE UNAVAILABLE`, keeps the book in
  memory, and sends the save result and the bulk view on separate events. The
  editor receives a full site (including the password) through the edit-site
  event, never through the site list.
- A high-risk local write is checked twice: the reducer checks the destination
  and every marked source, and the worker checks the built plan and asks for the
  high-risk confirmation if the reducer could not know the final name (D51).
- The server transfer start is its own event and its own start timestamp; a
  server upload with an unknown total still shows direction, name, and progress.

## Threading and handoff

Two threads (D38).

| Thread | Owns | Never does |
|---|---|---|
| Main | `UiState`, input, drawing | Open a socket, touch a file, wait on the worker |
| Worker | Client session, server state and slot, transfer queue, local batch, all file and network I/O | Draw, read UI state |

Handoff (`src/core/handoff.c`, policy with no platform call, so it is host-tested
on its own):

- **Commands** cross a fixed-capacity ring of plain records: open, parent,
  transfer, refresh, site connect/edit/new/delete/save, run action, confirm, IME,
  server start/stop/port, reconnect, dismiss, cancel, clear queue. No record
  holds a pointer into either thread's state. A server stop and a cancel use
  dedicated slots, so a full ring cannot drop them.
- **Events** cross a second fixed ring: session state, listing published,
  transfer terminal state, server state, operation progress, save result.
  Indices are release/acquire. Progress is a seqlock latch: the newest sample
  wins, a torn record is retried, and an older sample is dropped when a newer
  terminal event is delivered. The server state is latched separately and stays
  readable after the ring drains.
- **Listings** do not cross the rings. Each pane has three generations; the
  worker reserves a slot the reader does not hold, fills it, and publishes it
  with a release store. The UI keeps one generation until it releases it at a
  frame boundary, so a draw cannot see rows change under it.
- **The site book** is larger than one record and crosses on a 16 KiB bulk lane
  carrying a generation number, so a stale read is detectable. The payload is a
  view without passwords (D49).

Nothing is read by one thread while the other writes it. A second writer is a
defect, not a shortcut.

### Adapter claims (U12)

Every unverified Vita behaviour is a `[C]` comment in the adapter that owns it:

- `psp2_net.c`: `sceNetCtlInit` may already be up; `fcntl(O_NONBLOCK)` maps to
  the net non-blocking option; `select` over sceNet sockets; the resolver
  timeout unit and retry count; `SceNetCtlInfo.ip_address` is a dotted quad.
- `psp2_fs.c`: the `0x3001` devctl free-space call is proven only on `ux0:`;
  `ur0:`/`uma0:` are unverified; `mtime` is reported unknown; `sceIoRename`
  replace-on-existing is unverified, so `rename_replaces()` returns 0 and the
  aside fallback is always used (D48).
- `psp2_input.c`: the plain peek reports shoulders as L1/R1; the wide analog
  mode reports 0..255 centred at 128.
- `psp2_ime.c`: the common-dialog config must precede the IME dialog; the
  init order is an open decision; password masking and the first status frame
  are unverified.
- `psp2_log.c`: an append handle plus `SCE_SEEK_END` reports the existing size;
  if it fails, writes are refused rather than bypassing the cap.

These are hardware items for U14; none may move into policy.

## Data phase

LIST, NLST, MLSD, RETR, and STOR share one data connection per session, and it has
four sub-states: armed after PASV, accepted, active, draining. Any non-data command
or any refusal disarms it, so a 450 leaves nothing for the next command to accept.
A data connection is accepted only from the control peer's address; anything else
is closed without arming, and the backlog is drained when PASV arms. After an abort
the control channel drains the 426/226 pair before the next reply is parsed, so
later replies stay aligned. The client tolerates either abort order.

The transfer slot is not the data connection: a listing is short-lived, but it
still takes the slot in v1, because R38 and D9 give the slot to one server data
command at a time. A local batch therefore 450s a PC's listing; that cost is
recorded in the plan's risk table. A server command that mutates a path also takes
the slot, so a PC cannot delete a file the Vita is writing.

## State ownership

| State | Writer | Readers |
|---|---|---|
| Client session | FTP client machine, worker | UI snapshot, log |
| Server state and client slot | FTP server machine, worker | UI snapshot, log |
| Transfer queue and active slot | Transfer controller, worker | UI snapshot, log |
| Local operation and its plan | Operation planner and executor, worker | UI snapshot, log |
| Site book and settings | Worker, on a save command | UI snapshot |
| Session log | Worker | The user and the release checklist |
| `UiState` | Input reducer, main | Draw code, command emitter |

The UI reads snapshots and emits commands. It never mutates engine state.

## Paths, risk, and denial

One module owns this and all three roles call it.

- Canonicalization: volume detection, join, parent, removal of `.` and empty
  segments, collapsed separators, `..` clamping at a root, and byte-for-byte
  segments below a volume. A backslash is refused for every role, because one
  spelling that the platform might fold to `/` must not reach a denied file.
  The volume and every segment are compared case-insensitively against the
  denial and risk tables, while the original bytes go to the filesystem,
  because the exposed volumes are case-insensitive and a case variant must not
  slip past a denial.
- **Denied:** `ur0:tai/boot_config.txt` for every role, and every write, create,
  rename, or delete under `ur0:tai`, `ux0:tai`, and the app's own data folder for
  the server. The server also denies reads of `boot_config.txt` and its own data
  folder; the `tai` folders stay readable. A rename checks both operands.
- **Protected:** volume roots, `ur0:data`, `ux0:data`, and the `tai` directories
  cannot be renamed, moved, or deleted, by any role.
- **High risk for the local manager:** the `tai` folders and their contents,
  volume roots, `ur0:data` and `ux0:data` themselves, and the files that control
  loading (`config.txt`, `plugins.txt`, `*.skprx`, `*.suprx`). Risk belongs to a
  write step, source and destination, so a download or a copy into a `tai` folder
  carries it. A high-risk action confirms first (D30, D31).
- A remote-supplied name containing `/`, `\`, `:`, NUL, or a `..` segment is
  refused before any local path is built.

The server's virtual root is `/`, holding `UX0:`, `UR0:`, and `UMA0:`. There is no
path from an FTP client to any other mount.

## Persistence and log

Under `ux0:data/vitaFTPShell/`:

| File | Contents | Write rule |
|---|---|---|
| `sites.cfg` | Version header, one site per line, percent-encoded fields | Temp, sync, rename |
| `settings.cfg` | Last local path, last site, server port | Temp, sync, rename |
| `session.log` | One tab-separated record per line, monotonic ms plus fields | Append; rotate at 64 KiB |
| `session.old.log` | The one previous generation | Replaced on rotation |

A server's reply text is never a log field: a hostile server can echo a credential
back in a 530. One log line is bounded at 4096 bytes; an overlong record is
dropped whole, never split. A file that fails to parse yields whatever lines are
valid and is never overwritten until the user saves. A stale `.old` aside is
restored when the target is missing and cleared when both exist, so an
interrupted fallback cannot wedge the next save. When `ux0:` is absent, the book
lives in memory and the UI says saving is unavailable.

Passwords appear in `sites.cfg` by design and never in the log.

## Local file operations

The planner produces a plan before anything is touched: the ordered creates,
copies, renames, and deletes, the conflict decision, and the rollback list. The
executor walks the plan and reports progress; it makes no policy decisions.

- Recursion creates parents before children.
- A copy or move into itself or a descendant is refused.
- Every file is written under a reserved temporary name in the destination
  directory, synced, and renamed into place after a successful close (KTD12).
  Replace-on-rename is a port capability; when it is absent the core uses the
  aside fallback, and before a destination is written it restores or clears a
  stale aside left by an interrupted run. The destination file is synced after
  the rename; directory sync is the documented deferred gap.
- A copy rollback removes only a destination the plan created. A destination
  that existed and reached commit stays new; an old file is never deleted to
  satisfy a rollback.
- A cross-volume move is atomic per file: copy, rename, delete source. If the
  source delete fails, a new destination is removed; an overwritten destination
  is kept and the failure is named. A batch that fails after some moves leaves
  the completed moves and reports `n OF m MOVED`, never deleting a moved file
  (D45).
- Cancel or failure removes the temp; the previous destination stays
  byte-identical. A recursive delete cannot be rolled back and reports
  `DELETED n OF m`.
- The planner uses a 256 KiB arena, 2048 steps, and 2048 rollbacks; a plan that
  would overflow is refused, never truncated (D46).

## Error model

- Every adapter call returns a result the caller checks.
- The core maps a failure to a short reason, a stage, and one of five classes:
  `app`, `server`, `network`, `storage`, `user` (D33).
- An `app` class is a defect; the other four describe the world.
- A failed operation leaves the previous state intact or reports a recovery
  failure. It never reports success early.
- A value the platform cannot report is `--`, never a guess.

## Rendering

`src/ui/` draws from a `const UiState` and returns nothing. Geometry and copy come
from [`DESIGN.md`](DESIGN.md); the colour and panel vocabulary comes from
`src/gfx/theme`, adapted from COCKPIT under its MIT notice. `vita2d.h` appears only
in `src/gfx/draw.c`.

The host preview links the same `src/ui/` code against a software `vita2d` shim,
so a rendered state is the shipped composition, not a copy.

## Build shape

- CMake plus `build.sh`, following the pattern established by COCKPIT.
- `VITA_TITLEID` `VFTPSHELL`, version `01.00`, one `UNSAFE` self.
- `BUILD_NUMBER` in the project root; the source hash decides whether it
  increments. The hash stamp is local to the checkout, so a clean checkout
  increments once (it has no stamp) rather than reusing a number it cannot
  verify. Incrementing is always safe; a number is never reused for different
  sources.
- Artifact `vitaftpshell-v<version>-b<build>.vpk`; `build/` keeps exactly one.
- The unstripped ELF is kept.
- Host tests are a separate script and never enter the VPK.
- `src/main.c` is the composition root: it constructs each adapter once, calls
  `draw_init`, starts the app, and owns shutdown. It includes no platform
  header; the six `psp2_*.c` adapters own the SDK calls and `draw.c` owns
  `vita2d`.
- The app target compiles the real core, gfx, ui, app, and adapter sources and
  links `vita2d`, the SDK stubs the adapters call, `m`, and `pthread` (D48).
- `tools/test/run.sh` scans every `.c` and `.h` under `src/` against the
  seven-file allowlist and fails on a `psp2/`, `psp2common/`, `psp2kern/`, or
  `vita2d.h` include elsewhere. `tools/` is host-only and outside the guard:
  its `vita2d.h` is the software shim, not the SDK.
- `send.sh` refuses an artifact whose build number does not match
  `BUILD_NUMBER`, so a failed build cannot upload its predecessor.

## Host tests

`tools/test/run.sh` compiles the real core and graphics against fakes and
asserts:

- every refusal of the dependency rule (a `psp2/` or `vita2d.h` include
  anywhere under `src/` outside the seven-file allowlist fails the run);
- replies, multiline replies, PASV, and the three listing formats;
- path joining, parent, volume matching, the risk table, and the denial table;
- site round-trip, corrupt-file handling, atomic save, log formatting, and
  rotation;
- the client and the server against a scripted peer in each direction;
- queue order, the single slot, the conflict decision, terminal latching, and
  the fake-job walk;
- planner order, merge, the conflict gate, the copy-into-itself refusal, the
  volume-root refusal, protected deletes, risk flags, and the plan caps;
- executor overwrite with and without replace-on-rename, stale-aside recovery,
  cancel mid-copy, a kill at each write step, the destination sync, per-file
  move semantics, rollback, partial delete reporting, and `MOVE INCOMPLETE`;
- the input repeat decoder, every input-table row, marks across CWD/L/R, volume
  and connection clearing, dock focus, confirm scrolling, high-risk focus, IME
  cancel, and the volume-root delete refusal;
- the handoff: a producer faster than the consumer, a full ring, a coalesced
  progress stream, a latched server state, and three listing generations;
- `tools/test/tsan.sh` builds the handoff with plain stores and requires a
  ThreadSanitizer race, so the release/acquire on the publish index is proven
  necessary rather than assumed;
- the reducer's transitions, marks, dock focus, confirm scrolling, and the
  high-risk confirmation;
- the recorded draw calls and strings for every render state:
  WORKSPACE, SITES, SITE EDITOR, SERVER, ACTIONS, CONFIRM, and TEXT ENTRY;
- the app/worker integration: connect, list, mark, download, cancel, a failed
  site save, shutdown with an active transfer, and quit with the server on;
- `tools/test/tsan.sh` also remains a required negative ordering check.

A test that only proves a constant is not a test. A hardware run is the only
evidence that promotes the project to `verified`.

## Allowed platform includes

Only these files may include a `psp2/` or `vita2d.h` header:

- `src/platform/psp2_net.c`
- `src/platform/psp2_fs.c`
- `src/platform/psp2_input.c`
- `src/platform/psp2_ime.c`
- `src/platform/psp2_clock.c`
- `src/platform/psp2_log.c`
- `src/gfx/draw.c`

A new file that needs a platform call extends an adapter or adds one with a
decision in [`DECISIONS.md`](DECISIONS.md). It does not include the header
directly.

## Platform claims

Any Vita behavior that has not run on hardware is tagged `[C]` in the adapter that
depends on it, naming the claim and the reason. The open ones:

- [C] `select` over `sceNet` sockets for a mixed control, data, and listener set
  under FTP timing, with `sceNetEpollWait` as the fallback.
- [C] whether `sceIoRename` replaces an existing target, and the fallback in
  KTD12 if it does not.
- [C] `sceIoDevctl` free space on `uma0:` under the shipped eboot.
- [C] IME dialog init order and masked text behavior.

## GPLv3 boundary

The project is GPLv3 by choice (D40). COCKPIT's MIT graphics layer is adapted with
its notice in [`LICENSES/COCKPIT-MIT.txt`](../LICENSES/COCKPIT-MIT.txt); nothing
from VitaShell or libftpvita is vendored. Reused code keeps its notices and stays
under GPLv3 (D44).

## Open architecture questions

None. The plan resolved authid, passive ports, threading, listing caps,
persistence format, log format, MLSD, and the server foundation. The remaining
unknowns are execution-time measurements listed under Deferred to Implementation
in the plan.

# Architecture

**Status:** settled by the v1 plan. Phases 1 and 2 have built the project shape
and the shared foundation: the build and packaging, the six port contracts and
host fakes, the gfx base and layout table, the reply/listing/path/format
policy, and the site/settings/store/log policy. The protocol engines, screens,
adapters, and runtime remain contracts until their units land. The plan
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

## Threading and handoff

Two threads (D38).

| Thread | Owns | Never does |
|---|---|---|
| Main | `UiState`, input, drawing | Open a socket, touch a file, wait on the worker |
| Worker | Client session, server state and slot, transfer queue, local batch, all file and network I/O | Draw, read UI state |

Handoff (`src/core/handoff.c`, policy with no platform call, so it is host-tested
on its own):

- **Commands** cross a fixed-capacity ring of plain records: connect, disconnect,
  list, cwd, mkdir, rename, delete, copy, move, upload, download, cancel, retry,
  server start, server stop, set port, save sites, save settings. No record holds
  a pointer into either thread's state.
- **Events** cross a second fixed ring: session state, listing published, transfer
  progress, transfer terminal state, server state, operation progress, save
  result. Indices are release/acquire. Progress coalesces to the newest value;
  a terminal transfer state, a cancel acknowledgement, and a server state change
  are latched flags, so none can be dropped by a full ring.
- **Listings** do not cross the rings. Each pane has three generations; the
  worker fills one the UI has released and publishes it with a release store. The
  UI copies the index once at a frame boundary. The worker never fills the two
  most recently published generations, so a draw cannot see rows change under it.
- **The site book** is larger than one record and crosses on a bulk lane carrying
  a generation number, so a stale read is detectable.

Nothing is read by one thread while the other writes it. A second writer is a
defect, not a shortcut.

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
  segments below a volume. The volume and every segment are compared
  case-insensitively against the denial and risk tables, while the original bytes
  go to the filesystem, because the exposed volumes are case-insensitive and a
  case variant must not slip past a denial.
- **Denied:** `ur0:tai/boot_config.txt` for every role, and every write, create,
  rename, or delete under `ur0:tai`, `ux0:tai`, and the app's own data folder for
  the server. A rename checks both operands.
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
back in a 530. A file that fails to parse yields whatever lines are valid and is
never overwritten until the user saves. When `ux0:` is absent, the book lives in
memory and the UI says saving is unavailable.

Passwords appear in `sites.cfg` by design and never in the log.

## Local file operations

The planner produces a plan before anything is touched: the ordered creates,
copies, renames, and deletes, the conflict decision, and the rollback list. The
executor walks the plan and reports progress; it makes no policy decisions.

- Recursion creates parents before children.
- A copy or move into itself or a descendant is refused.
- Every file is written under a reserved temporary name in the destination
  directory and renamed into place after a successful close (KTD12). Replace-on-
  rename is a port capability; when it is absent the core plans the aside
  fallback, and before a destination is written it restores or clears a stale
  aside left by an interrupted run.
- A cross-volume move deletes the source only after the rename, and removes the
  destination if that delete fails (D36).
- Cancel or failure removes the temp; the previous destination stays
  byte-identical.

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

- CMake plus `build.sh`, as in `vita-cockpit/`.
- `VITA_TITLEID` `VFTPSHELL`, version `01.00`, one `UNSAFE` self.
- `BUILD_NUMBER` in the project root; the source hash decides whether it
  increments. The hash stamp is local to the checkout, so a clean checkout
  increments once (it has no stamp) rather than reusing a number it cannot
  verify. Incrementing is always safe; a number is never reused for different
  sources.
- Artifact `vitaftpshell-v<version>-b<build>.vpk`; `build/` keeps exactly one.
- The unstripped ELF is kept.
- Host tests are a separate script and never enter the VPK.
- `src/main.c` is a stub until U13: it includes no platform header, because the
  renderer owns `vita2d` and each adapter owns its SDK calls. The libvita2d link
  and the SDK stubs join when U13 adds the gfx and adapter sources to the app
  target; linking an uncalled library is an unused link.
- `tools/test/run.sh` scans every `.c` and `.h` under `src/` against the
  seven-file allowlist and fails on a `psp2/`, `psp2common/`, `psp2kern/`, or
  `vita2d.h` include elsewhere. `tools/` is host-only and outside the guard:
  its `vita2d.h` is the software shim, not the SDK.
- `send.sh` refuses an artifact whose build number does not match
  `BUILD_NUMBER`, so a failed build cannot upload its predecessor.

## Host tests

`tools/test/run.sh` compiles the real policy, presentation, and runtime against
fakes and asserts:

- every refusal of the dependency rule (a `psp2/` or `vita2d.h` include
  anywhere under `src/` outside the seven-file allowlist fails the run);
- replies, multiline replies, PASV, and the three listing formats;
- path joining, parent, volume matching, the risk table, and the denial table;
- site round-trip, corrupt-file handling, atomic save, log formatting, and
  rotation;
- the client and the server against a scripted peer in each direction;
- queue order, the conflict decision, recursion, the copy-into-itself refusal,
  cancellation, rollback, partial cleanup, and the rename-replace capability;
- the handoff: a producer faster than the consumer, a full ring, a coalesced
  progress stream, a latched terminal state, and three listing generations;
- the reducer's transitions, marks, dock focus, confirm scrolling, and the
  high-risk confirmation;
- the recorded draw calls for each screen state.

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
its notice; nothing from VitaShell or libftpvita is vendored. Reused code keeps its
notices and stays inside `vitaFTPShell/`; it must not be copied into the MIT
projects in the workspace.

## Open architecture questions

None. The plan resolved authid, passive ports, threading, listing caps,
persistence format, log format, MLSD, and the server foundation. The remaining
unknowns are execution-time measurements listed under Deferred to Implementation
in the plan.

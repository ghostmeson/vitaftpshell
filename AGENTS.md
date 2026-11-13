# AGENTS.md — vitaFTPShell project contract

This is the entry point for agents and contributors working in this repository.
It records the project rules and what is already settled. Read it before changing
anything.

## Status

`building`. Phases 1-5 are complete: the build shape, the six port contracts
and host fakes, the COCKPIT gfx base with the seven-surface layout table, the
reply/listing/path/format policy, the site book/settings/store/log policy, the
passive FTP client and server state machines, the transfer queue and slot
arbitration, the local operation planner and executor, the input reducer and UI
state, the cross-thread handoff, the seven screen surfaces, the six Vita
adapters, and the app/worker runtime all exist. `tools/test/run.sh` passes
nineteen host suites, and `tools/preview/run.sh` renders 36 settled states.
`build.sh` emits `vitaftpshell-v01.00-b38.vpk` with the real app linked. b16,
b18, b20, b21, b22, and b23 have run on the console: b18 proved the drive
browser; b20 opened the IME but crashed in the worker; b21 fixed the worker
stack and rename completed; b22 made "no selection" disable every action except
new folder; b23 fixed the server IP byte order and free-space queries, and FTP
works. b24 added the clipboard/PASTE copy/move model and hardened DELETE; b25
defers worker confirmations while a local dialog owns the screen. b26 widened
the FTP client connect window and logged the resolved IP and raw network result;
b27 clears the CONNECTING state on remote errors; b28 fixed the PASV data
connection by using the control peer's address and the reply port; b32 contains
the remote-pane fix (stuck READING, remote actions routed to the FTP client, DISCONNECT,
site teardown, idle NOOP liveness, remote mark pruning); b36 adds REFRESH,
unsafe-name refusal, download totals, upload MLST preflight, and folder-transfer
refusal (R22/R23 deferred); b37 closes the review pass with an iterated queue
pump, one session teardown that clears the overwrite latch, a truncated-success
listing cap, the ALL DRIVES download fallback, the high-risk delete warning, and
MLST send-failure handling. b37 has run on the console (operator report,
2026-11-13); b38 fixes a regression where the unsafe-name guard refused to open
the server's own `UX0:`/`UR0:`/`UMA0:` roots, and is built but not yet tested.
No M1–M8 row result has been transcribed, so the project is `building`, not
`verified`. Do not start coding from this file alone: the plan
is the source of truth for behavior, sequencing, and verification, and
`docs/DESIGN.md` is the layout authority.

## Read order

1. `STRATEGY.md` — why the product exists, who it serves, boundaries, metrics.
2. `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` — the plan: R1-R73, KTD1-KTD18, U1-U17 in six phases, the verification contract.
3. `PROJECTS.md` — current phase and the single next action.
4. `docs/PROJECT.md` — scope, non-goals, roadmap, failure policy.
5. `docs/ARCHITECTURE.md` — planned layers, ports, and allowed platform files.
6. `docs/DESIGN.md` — the settled layout, states, and input; the sheets in `docs/design/`.
7. `docs/DECISIONS.md` — numbered decisions with context and consequences.
8. `docs/FIXES.md` — chronological fix index with hardware status; start here
   before re-debugging a known failure.
9. `docs/THIRD_PARTY.md` — reused code, licenses, and protocol references.
10. `docs/README.md` — the documentation map.

## What this is

One GPLv3 app for a PS Vita on 3.65 Enso: an FTP client, a local file manager,
and an anonymous FTP server in the COCKPIT Cyberpunk HUD. The console is one
side of the transfer, not a remote endpoint. The two live panes are the product
shape; the transfer dock is where work is visible.

## Settled scope — do not reopen without new evidence

- v1 ships all three roles: client, local file manager, and FTP server.
- The server is anonymous read-write on the mounted `ux0:`, `ur0:`, and `uma0:`
  volumes, OFF at launch, and accepts one client at a time.
- The client is passive FTP only, tries MLSD then LIST, and parses Unix and DOS
  listings.
- Passwords are always remembered and stored unencrypted; they are never logged.
- Multi-select and recursive folder operations are v1.
- One transfer is active at a time; additional items queue, and a server-side
  transfer uses the same slot.
- Plain FTP only in v1. No FTPS, SFTP, HTTP, or WebDAV.
- No background daemon, taiHEN system plugin, kernel plugin, or boot component.
- No PC-first UI and no VitaShell feature-parity race.
- The visual layout is settled in `docs/DESIGN.md`. Do not invent pane geometry,
  dock height, or screen composition in code.
- GPLv3. Keep the license and the third-party notices intact.
- One `UNSAFE` eboot. The server and the local pane need all three volumes, and
  the safe authid refuses them (D32).
- The local manager may write inside `ur0:tai` and `ux0:tai` behind the
  high-risk confirmation; the server never may; `boot_config.txt` is denied for
  every role (D30, D31).
- Every started transfer records one cause class, and the release gate is zero
  `app`-class failures plus 95% over attempts that were neither cancelled nor
  network-interrupted (D33).

If evidence contradicts a settled decision, add a superseding entry to
`docs/DECISIONS.md` with the reason. Do not silently rewrite history.

## Architecture rules

The dependency rule applies in full: source dependencies point inward, toward
policy. Policy is compilable and testable on the host without a Vita header.

### Planned layers

| Layer | Path | Holds | May include |
|---|---|---|---|
| Policy | `src/core/` | FTP client/server state machines, listing parsing, path and risk rules, site book, store, log events, transfer queue, file-operation policy, UI state | C standard library only |
| Platform ports | `src/platform/*.h` | Plain-C contracts for net, filesystem, input, text entry, clock, log | no `psp2/` types |
| Platform adapters | `src/platform/psp2_*.c` | SceNet, sceIo, SceCtrl, IME, clock, log | `psp2/` headers |
| Renderer | `src/gfx/` | theme, font, draw primitives | `vita2d.h` only in `draw.c` |
| Presentation | `src/ui/` | draws `UiState` and maps input to commands | core and gfx headers only |
| Runtime | `src/app/` | frame loop, worker thread, rings, snapshot handoff; constructs nothing | core, ports, ui, gfx headers |
| Composition | `src/main.c` | constructs adapters, starts the runtime, owns the app lifetime | platform and ui headers |

### Allowed platform includes

Only these files may include a `psp2/` or `vita2d.h` header:

- `src/platform/psp2_net.c` — sockets, resolver, network state.
- `src/platform/psp2_fs.c` — directory and file calls, free space.
- `src/platform/psp2_input.c` — controller.
- `src/platform/psp2_ime.c` — system text entry.
- `src/platform/psp2_clock.c` — monotonic time and sleep.
- `src/platform/psp2_log.c` — session log writes and rotation.
- `src/gfx/draw.c` — `vita2d` only.

A new file that reaches for a platform header is a layering bug. Split the
concern or extend the relevant adapter; do not widen this list without a
decision in `docs/DECISIONS.md`.

### Ports

- `NetPort` — resolve, connect, send, receive, close, listen, accept, and
  non-blocking state. The FTP engine takes this contract, not a socket number.
- `FsPort` — list, stat, open, read, write, close, mkdir, rmdir, rename, remove,
  and free space.
- `InputPort` — button edges and stick position.
- `TextInputPort` — open the IME, return UTF-8, report cancellation.
- `ClockPort` — monotonic milliseconds and sleep.
- `LogPort` — append a bounded line to the session log.

The core declares these contracts in its own types. No `Sce*` type, socket
handle, or file handle crosses inward.

### State ownership

One writer per shared state. The eventual owners are expected to be:

- the client session: the FTP client state machine;
- the server state and client slot: the server controller;
- the transfer queue: the transfer controller;
- the site book and settings: the settings store;
- the UI state: the event reducer.

The UI reads state and emits commands. It does not write another owner's state.
A second writer is a design bug, not a shortcut.

### Host tests

Policy and UI state must run on the host against fake `NetPort`, `FsPort`,
`InputPort`, and `ClockPort` implementations. At minimum, test:

- reply parsing, multiline replies, and passive-mode parsing;
- Unix, DOS, and MLSD listing parsing;
- path joining, parent, and the protected-path rules;
- batch conflict decisions and queue order;
- recursive folder planning and the copy-into-itself refusal;
- cancellation and partial-file cleanup decisions;
- server command handling and path denial;
- the input reducer and screen transitions;
- the transfer slot, the local plan and rollback, and the handoff rings.

A test that only proves a function returns a constant is not a test. A hardware
run is the only evidence that promotes the project to `verified`.

### Errors and unverified platform behaviour

- Check every return value. Degrade, never crash.
- A value the platform cannot report renders `--`, never a guess or a zero.
- An adapter returns a plain error and the core maps it to a user message.
- Any claim about Vita behaviour that has not run on hardware carries a `[C]`
  comment naming the claim and the reason. Keep it in the adapter.

## Safety

1. **Never edit `ur0:tai/boot_config.txt`.** The app must deny writes, renames,
   and deletes to it, including through server mode.
2. **No kernel plugin.** Do not add a `.skprx`, a `*KERNEL` entry, or a boot
   component. v1 has no system-level surface.
3. **Server OFF by default.** Starting it is an explicit action. The ON state and
   client count stay visible on every screen. The warning says anonymous clients
   can read and write the exposed volumes and that the app data folder is
   excluded.
4. **Protected paths.** Volume roots, `ur0:data`, `ux0:data`, `ur0:tai`, and
   `ux0:tai` cannot be renamed, moved, or deleted. The server denies all access to
   `ur0:tai/boot_config.txt` and the app's own data folder under `ux0:data`, and
   denies every write, create, rename, and delete under `ur0:tai` and `ux0:tai`.
   The local manager may write inside the `tai` folders only after the high-risk
   confirmation (D30); nothing in the app writes, renames, or deletes
   `boot_config.txt`. Every check uses the canonical path from D42, and a rename
   checks both operands.
5. **Never delete or recreate a system directory.** `ur0:`, `ux0:`, `uma0:`,
   `ur0:data`, `ux0:data`, `ur0:tai`, and `ux0:tai` belong to the console. The
   app creates only its own data folder.
6. **Destructive actions confirm.** The confirmation names the items and says
   delete cannot be undone. Cancel changes nothing.
7. **No secrets in logs.** Passwords never enter the session log or an error
   message.
8. **GPLv3 boundary.** Reused VitaShell code keeps its notices and stays in this
   project. The COCKPIT-derived graphics files keep their MIT notice in
   `LICENSES/COCKPIT-MIT.txt`.

## Build and release

- Target: PS Vita 3.65 Enso, VitaSDK 2026.08, libvita2d.
- `./build.sh` builds one UNSAFE eboot and one VPK; `./tools/test/run.sh` runs
  the layering guard and the host tests; `./tools/preview/run.sh` renders the
  screens on the host; `./send.sh <vita-ip>` uploads the current VPK. The app
  target links the real core, gfx, ui, app, and adapters with libvita2d and the
  SDK stubs the adapters call.
- Every build emits `vitaftpshell-v<version>-b<build>.vpk`.
- `send.sh` refuses an artifact whose build number does not match
  `BUILD_NUMBER`, so a failed build cannot upload the previous VPK.
- `BUILD_NUMBER` lives in the project root, never in `build/`, and every build
  increments it unless the source hash is unchanged.
- `build.sh` prunes superseded VPKs; `build/` keeps exactly one artifact.
- Keep the unstripped ELF for any build handed to hardware.
- A fixed filename is an exception recorded in `docs/DECISIONS.md`, never the
  default.
- Upload form: `ftp://<vita-ip>:1337/ux0:/data/` with VitaShell's server running.

## Documentation duties

- Update `PROJECTS.md` after every job. One row, one next action.
- Update the plan when behavior changes; do not leave the truth only in chat.
- Record every fixed defect in `docs/FIXES.md` with its build, evidence, and
  `HW`/`HOST`/`PENDING` status. The detailed evidence stays in
  `docs/VERIFICATION.md`.
- Put feature additions under `docs/features/` with a dated document.
- Put screen mockups and design notes under `docs/design/`.
- Add a numbered entry to `docs/DECISIONS.md` for every product or architecture
  decision that would surprise a future reader.
- Record Vita platform facts with their evidence and the `[V]`/`[C]`/`[L]` tag;
  do not present community lore as verified.
- Keep the doc map in `docs/README.md` current.

## Definition of done

A job is finished when:

- it matches the requirements plan, or the plan is updated with the change;
- host tests pass for policy and UI state;
- the build produces one uniquely named VPK;
- `PROJECTS.md` reflects the phase and next action;
- `docs/` reflects the change, including a decision when one was made;
- the fix is indexed in `docs/FIXES.md` with its hardware status;
- no new `[C]` claim escaped the adapter;
- the documentation link and code-fence check reports no issues;
- `verified` is not claimed without hardware evidence.

## What not to do

- Do not invent screen geometry outside `docs/DESIGN.md`.
- Do not add SFTP, FTPS, HTTP, WebDAV, or SMB in v1.
- Do not add a background transfer daemon or a boot-time server.
- Do not write to `boot_config.txt`, `ur0:tai`, or `ux0:tai`.
- Do not render a number the platform cannot report.
- Do not hide the server's ON state or its anonymous access.
- Do not remove the COCKPIT MIT notice from the derived graphics files.
- Do not promote the project to `verified` without a hardware run.
- Do not let a new finding live only in chat.

## Open decisions

None at the product or architecture level. The planning session resolved authid,
passive ports, threading, listing caps, the site and log formats, MLSD, the server
foundation, the local `tai` policy, and the five deferred product choices; the
answers are D30-D43 and the plan's KTDs; D44-D47 record the standalone
repository and the Phase 4 implementation shapes; D48-D52 record the Phase 5
composition, UI evidence, and safety backstops; D53-D63 record the Phase 6
all-drives root, always-on log, action feedback, IME sequence, worker stack,
the no-selection rule, the server address/free-space policy, the clipboard, the
destructive-confirmation rules, the FTP-client connect/PASV diagnostics, the
remote-pane session/action rules, and the remote refresh/name/upload/folder
rules.

These remain open by design and belong to implementation, not to an agent's
judgment call:

- the exact listing-arena size, after a memory measurement on hardware;
- the reply-line cap, nominally 4096 bytes, against real servers;
- whether `sceIoRename` replaces an existing target; the adapter reports 0, so
  the aside fallback is always used (D48);
- the `FsEntry.mtime` unit (the server assumes Unix seconds); the adapter
  reports unknown and the UI renders `--` (D48);
- directory sync after a rename: `FsPort` has no call for it, so power-loss
  durability needs a port extension or a confirmed platform guarantee;
- the mksfoex flags this SDK build needs;
- the icon and LiveArea art.

## Directory map

```text
vitaFTPShell/
  AGENTS.md            this contract
  STRATEGY.md          product direction
  PROJECTS.md          project tracker
  README.md            project overview
  CMakeLists.txt       Vita toolchain, UNSAFE self, VPK
  BUILD_NUMBER         committed build identifier
  build.sh             hash-stamped build and one-artifact pruning
  send.sh              uploads the current VPK, refusing a stale number
  LICENSES/            third-party license notices (COCKPIT MIT)
  .github/workflows/   host-test CI
  src/
    main.c             composition root; constructs adapters, starts the app
    core/              policy: parsers, path, store, log, queue, planner,
                       executor, input/UI state, handoff
    core/ftp/          passive FTP client and server state machines
    gfx/               theme, font, draw primitives; draw.c owns vita2d
    ui/                the seven screens, dock, legends, shared parts, render
    app/               app frame loop and the one worker
    platform/          port headers and the six psp2_* adapters
  sce_sys/             placeholder icon and LiveArea art (U14 replaces)
  tools/
    test/run.sh        layering guard and host test runner
    fake/              NetPort, FsPort, input, IME, clock, log, and FTP peers
    preview/           software vita2d shim and the 960x544 renderer
  build/               one VPK and the unstripped ELF; ignored
  docs/
    README.md          documentation map
    PROJECT.md         scope, non-goals, roadmap, failure policy
    ARCHITECTURE.md    layers, ports, allowed platform files
    DESIGN.md          the settled layout, states, and input
    DECISIONS.md       numbered decisions, D1-D67
    FIXES.md           chronological fix index with HW/HOST/PENDING status
    THIRD_PARTY.md     reused code, licenses, protocol references
    plans/             the v1 plan: requirements, decisions, units, verification
    features/          future feature documents
    design/            the settled screen sheets and renders
```

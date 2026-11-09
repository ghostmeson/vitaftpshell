# AGENTS.md — vitaFTPShell project contract

This is the entry point for agents working inside `vitaFTPShell/`. The workspace
contract at `../AGENTS.md` still applies; this file adds project rules and
records what is already settled. Read both before changing anything.

## Status

`building`. Phases 1, 2, and 3 are complete: the build shape, the six port
contracts and host fakes, the COCKPIT gfx base with the seven-surface layout
table, the reply/listing/path/format policy, the site book/settings/store/log
policy, and the passive FTP client and server state machines all exist.
`tools/test/run.sh` passes ten host suites and `tools/preview/run.sh` renders a
960x544 PNG. The VPK is still a stub (`vitaftpshell-v01.00-b7.vpk`): no screen
or platform adapter exists yet, and the engine is not linked into the app target
until U13. Do not start coding from this file alone: the plan is the source of
truth for behavior, sequencing, and verification, and `docs/DESIGN.md` is the
layout authority.

## Read order

1. `../AGENTS.md` — workspace code rules, documentation duties, hard safety rules.
2. `STRATEGY.md` — why the product exists, who it serves, boundaries, metrics.
3. `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` — the plan: R1-R73, KTD1-KTD18, U1-U17 in six phases, the verification contract.
4. `PROJECTS.md` — current phase and the single next action.
5. `docs/PROJECT.md` — scope, non-goals, roadmap, failure policy.
6. `docs/ARCHITECTURE.md` — planned layers, ports, and allowed platform files.
7. `docs/DESIGN.md` — the settled layout, states, and input; the sheets in `docs/design/`.
8. `docs/DECISIONS.md` — numbered decisions with context and consequences.
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
- GPLv3. Do not copy this project's code into the MIT projects in the workspace.
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

The workspace dependency rule applies in full: source dependencies point
inward, toward policy. Policy is compilable and testable on the host without a
Vita header.

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
- the input reducer and screen transitions.

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
   project. It must not be copied into the MIT projects.

## Build and release

- Target: PS Vita 3.65 Enso, VitaSDK 2026.08, libvita2d.
- `./build.sh` builds one UNSAFE eboot and one VPK; `./tools/test/run.sh` runs
  the layering guard and the host tests; `./tools/preview/run.sh` renders the
  gfx layer on the host; `./send.sh <vita-ip>` uploads the current VPK. The
  renderer's libvita2d link and the SDK stubs join when U13 adds the gfx and
  adapter sources to the app target.
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
- Put feature additions under `docs/features/` with a dated document.
- Put screen mockups and design notes under `docs/design/`.
- Add a numbered entry to `docs/DECISIONS.md` for every product or architecture
  decision that would surprise a future reader.
- Link to the workspace wiki (`../agent-wiki/`) for Vita facts; do not fork them
  here.
- Keep the doc map in `docs/README.md` current.

## Definition of done

A job is finished when:

- it matches the requirements plan, or the plan is updated with the change;
- host tests pass for policy and UI state;
- the build produces one uniquely named VPK;
- `PROJECTS.md` reflects the phase and next action;
- `docs/` reflects the change, including a decision when one was made;
- no new `[C]` claim escaped the adapter;
- the workspace link and code-fence check reports no issues;
- `verified` is not claimed without hardware evidence.

## What not to do

- Do not invent screen geometry outside `docs/DESIGN.md`.
- Do not add SFTP, FTPS, HTTP, WebDAV, or SMB in v1.
- Do not add a background transfer daemon or a boot-time server.
- Do not write to `boot_config.txt`, `ur0:tai`, or `ux0:tai`.
- Do not render a number the platform cannot report.
- Do not hide the server's ON state or its anonymous access.
- Do not copy GPLv3 code into the MIT projects.
- Do not promote the project to `verified` without a hardware run.
- Do not let a new finding live only in chat.

## Open decisions

None at the product or architecture level. The planning session resolved authid,
passive ports, threading, listing caps, the site and log formats, MLSD, the server
foundation, the local `tai` policy, and the five deferred product choices; the
answers are D30-D40 and the plan's KTDs.

These remain open by design and belong to implementation, not to an agent's
judgment call:

- the exact listing-arena size, after a memory measurement on hardware;
- the reply-line cap, nominally 4096 bytes, against real servers;
- whether `sceIoRename` replaces an existing target, and which fallback KTD12
  needs as a result;
- the `FsEntry.mtime` unit (the server assumes Unix seconds) and whether the
  adapter must convert or report unknown;
- directory sync after a rename: `FsPort` has no call for it, so power-loss
  durability needs a port extension or a confirmed platform guarantee;
- the IME initialization order;
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
  src/
    main.c             composition root; a Phase 1 stub until U13
    core/ftp/          passive FTP client and server state machines
  sce_sys/             placeholder icon and LiveArea art (U14 replaces)
  tools/
    test/run.sh        layering guard and host test runner
    fake/              NetPort, FsPort, input, IME, clock, log, and FTP peers
  build/               one VPK and the unstripped ELF; ignored
  docs/
    README.md          documentation map
    PROJECT.md         scope, non-goals, roadmap, failure policy
    ARCHITECTURE.md    layers, ports, allowed platform files
    DESIGN.md          the settled layout, states, and input
    DECISIONS.md       numbered decisions, D1-D43
    THIRD_PARTY.md     reused code, licenses, protocol references
    plans/             the v1 plan: requirements, decisions, units, verification
    features/          future feature documents
    design/            the settled screen sheets and renders
```

# Project

## What it is

vitaFTPShell is one PS Vita app that combines an FTP client, a local file
manager, and an anonymous FTP server. Local files and a saved site are two live
panes on a 960×544 Cyberpunk HUD; the transfer dock shows work without taking
over the screen.

The console is a peer in the transfer, not a remote endpoint. A user can pull a
file from `ftp.gnu.org`, push a build to a personal server, or serve the memory
card to a PC, without making the PC the workspace.

## Status

`building`. Strategy, requirements, the screen-by-screen design, and the
architecture plan are complete. Phases 1-5 are done: the build shape, six port
contracts and fakes, the gfx base and layout table, the reply/listing/path/
format policy, the site/settings/store/log policy, the passive FTP client and
server, the transfer queue, the local planner and executor, the input reducer
and UI state, the cross-thread handoff, the seven screen surfaces, the six Vita
adapters, and the app/worker runtime exist. `tools/test/run.sh` passes nineteen
host suites, `tools/preview/run.sh` renders 36 settled states, and `build.sh`
emits `vitaftpshell-v01.00-b36.vpk` with the real app linked. b16 and b18 have
run on the console: b18 proved the drive browser, pinned the IME as the
new-folder/rename blocker, showed the client failing at TCP connect, and showed
the server with no client. b20 opened the IME and reached a rename, then the
worker crashed on a stack overflow; b21 fixed the worker stack and rename
completed. b22 made "no selection" disable every file action except new folder,
and exposed the server advertising a byte-swapped IP. b23 fixed the IP, matched
ftpvitalib's net pool/backlog, and restricted free-space queries to `ux0:`; FTP
works. b24 adds clipboard/PASTE copy/move and hardens DELETE; b25 defers worker
confirmations while a local dialog owns the screen; b26 widens the FTP-client
connect window and logs the resolved IP and raw network result; b27 clears the
CONNECTING state on remote errors; b28 fixes PASV by using the control peer's
address and the reply port; b32 fixes the remote pane (READING cleared on a
failed CWD, remote actions over the FTP client, DISCONNECT, site teardown, an
idle NOOP for liveness, remote mark pruning); b36 adds REFRESH, unsafe-name
refusal, download totals, upload MLST preflight, and folder-transfer refusal
(R22/R23 deferred). No M1–M8 row has passed; the project stays `building` until
it does.

## Users

- **Primary:** the anonymous-first collector. They pull homebrew, patches, and
  media from saved public FTP sites on the Vita, without a PC, and keep logins
  for the sites that require them.
- **Secondary:** the Vita-as-a-drive user. They keep VitaShell's FTP-server habit
  and want the same app to serve the console's files to another machine.

## v1 scope

| Capability | In v1 |
|---|---|
| FTP client | Saved sites, anonymous and login, passive FTP, browse, download, upload, create, rename, delete, recursive folders |
| Local manager | Browse the mounted `ux0:`/`ur0:`/`uma0:` volumes, multi-select, create, rename, copy, move, delete, recursive folders, free space |
| FTP server | Anonymous read-write, mounted `ux0:`/`ur0:`/`uma0:`, OFF at launch, one client, visible ON state |
| Transfers | One active, queued batch, progress, cancel, overwrite/skip/cancel conflict prompt |
| Interface | Two live panes, site manager, server panel, actions, confirms, system text entry, inline errors |

The full behavior is in
[`plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md),
requirements R1-R70.

## Non-goals

- SFTP, FTPS, HTTP, WebDAV, or SMB in v1.
- Resume, sync, scheduled transfers, or multiple active transfers.
- Multiple simultaneous server clients.
- A background daemon, kernel plugin, `*KERNEL` entry, or boot-time component.
- A PC-first desktop workflow or a desktop-client port.
- VitaShell feature parity: archive extraction, USB mounting, media playback.
- An encrypted credential vault or a master password.
- Themes, plugins, or alternate HUD layouts.

## Roadmap

| Phase | Outcome |
|---|---|
| Strategy | Product direction and boundaries — done |
| Requirements | v1 contract with R-IDs, flows, and acceptance examples — done |
| Visual design | Screen-by-screen layout, states, and input; `docs/DESIGN.md` — done |
| Architecture and plan | Modules, ports, threading, authid, build shape; the plan is enriched in place — done |
| Phase 1: Build shape | Source tree, `build.sh`, one VPK, the layering guard — done |
| Phase 2: Contracts, graphics, and shared policy | Ports, fakes, gfx, parsers, path table, store, log — done |
| Phase 3: Protocol engines | Client and server machines against scripted peers — done |
| Phase 4: Operations and state | Queue, planner, executor, reducer, handoff — done |
| Phase 5: Screens and composition | Seven surfaces, adapters, runtime, host integration test — done |
| Phase 6: Hardware verification | M1–M8 evidence; project becomes `verified` — in progress (b36) |

## Failure policy

- A failed transfer reports its stage and leaves no destination that looks
  complete. Writes stage to a temporary name and rename into place, so a failed
  overwrite leaves the previous file unchanged.
- A remote-supplied filename containing a path separator, `:`, NUL, or a `..`
  segment is refused before any local path is built.
- A failed write leaves the original unchanged or recovers it; the app never
  claims success before the write completes.
- A dropped network marks the session disconnected, stops active transfers with
  an error, and offers reconnect. The local pane stays usable.
- A failed server start leaves the server OFF and names the reason.
- A value the platform cannot report renders `--`, never a guess.
- A protected path is refused by name; the app never retries with force.
- A crash is a defect, not a degraded mode. Keep the unstripped ELF and gather a
  console dump before changing behavior.

## Success

The v1 matrix in the requirements plan is the definition of done: anonymous
download from `ftp.gnu.org`, upload to a login site, local batch work, anonymous
server access from a PC, cancellation, and protected-path refusal, all on
hardware. The strategy metrics are transfer completion rate, crash-free
sessions, connect-to-first-listing, and server-matrix pass.

## Risks

| Risk | Mitigation |
|---|---|
| Anonymous read-write server exposes the three Vita volumes | OFF by default, visible ON state, client count, warning, denied app data folder and tai writes, protected-path denial |
| Passwords stored unencrypted | State it before the first save; never log them; no false encryption claim |
| WiFi path is built but not hardware-verified | First hardware session proves connect, PASV, transfer, and timeouts |
| Public FTP may decline | Personal servers keep the product useful; no protocol widening in v1 |
| Controller-only density | Design phase reviews each screen before implementation |
| Cross-volume batch move is not all-or-nothing | Each file is atomic; a batch failure leaves completed moves and reports `n OF m MOVED`, never deletes a moved file (D45) |
| Planner and handoff caps are finite | A plan that overflows the arena or step table is refused, never truncated; a lost command is counted (D46) |
| One transfer slot is shared by the client, the server, and local operations | The worker gates the client pump on the server and sets the server's external-busy flag while an app transfer owns the slot (D51) |
| GPLv3 boundary | Keep the license and third-party notices intact; document provenance |

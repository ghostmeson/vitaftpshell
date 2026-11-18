# Project

## What it is

vitaFTPShell is one PS Vita app that combines an FTP client, a local file
manager, and an anonymous FTP server. Local storage and a saved site share two
live panes on a 960×544 Cyberpunk HUD, and the right pane can become a second
local browser; the transfer dock shows work without taking over the screen.

The console is a peer in the transfer, not a remote endpoint. A user can pull a
file from `ftp.gnu.org`, push a build to a personal server, or serve the memory
card to a PC, without making the PC the workspace.

## Status

<!-- status:begin -->
`building`. vitaFTPShell is one PS Vita app with three roles: a passive FTP
client, a local file manager over every mounted user volume, and an anonymous
read-write FTP server on `ux0:`, `ur0:`, and `uma0:`. The seven screens, the
app/worker runtime, and the six Vita adapters are implemented, and the host
suites pass. The local workspace and the cross-pane folder transfer — recursive
download and upload, the verified move, the report, and the per-folder
read-only state — are host-complete, and the cross-pane code review's fixes are
applied on `main`. The console matrix runs once at v1 code freeze (D95), so the
project is `building`, not `verified`.

The live state and the single next action are on [the board](../PROJECTS.md); the
hardware evidence is in [the evidence record](VERIFICATION.md).
<!-- status:end -->

## Users

- **Primary:** the anonymous-first collector. They pull homebrew, patches, and
  media from saved public FTP sites on the Vita, without a PC, and keep logins
  for the sites that require them.
- **Secondary:** the Vita-as-a-drive user. They keep VitaShell's FTP-server habit
  and want the same app to serve the console's files to another machine.

## v1 scope

| Capability | In v1 |
|---|---|
| FTP client | Saved sites, anonymous and login, passive FTP, browse, download, upload, create, rename, delete; the folder transfer and verified move are built on the host (R22/R23, D100) |
| Local manager | Browse every mounted user volume (`ux0:`, `ur0:`, `uma0:`, `imc0:`, `gro0:`, `grw0:`, `app0:`), with `app0:` and `gro0:` read-only and save data excluded; two local panes over one session; multi-select, create, rename, copy, move, delete, local recursive folder operations, free space |
| FTP server | Anonymous read-write, mounted `ux0:`/`ur0:`/`uma0:`, OFF at launch, up to four clients with one data transfer at a time, visible ON state |
| Transfers | One network transfer and one local job may run together, each with its own queued batch, progress, cancel, and overwrite/skip/cancel conflict prompt |
| Interface | Two live panes, site manager, server panel, actions, confirms, system text entry, inline errors |

The full behavior is in
[`plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md),
requirements R1-R74.

## Non-goals

- SFTP, FTPS, HTTP, WebDAV, or SMB in v1.
- Resume, sync, scheduled transfers, or more than one transfer per lane.
- A background daemon, kernel plugin, `*KERNEL` entry, or boot-time component.
- A PC-first desktop workflow or a desktop-client port.
- VitaShell feature parity: archive extraction, USB mounting, media playback.
- An encrypted credential vault or a master password.
- Themes, plugins, or alternate HUD layouts.

## Feature traceability

The three primary features, the requirements that define them, and the matrix
rows that prove them on hardware.

| Feature | Requirements | Matrix |
|---|---|---|
| FTP client — saved sites, browse, download, upload, mutations, recursive folders | R11, R13, R22, R23, R41 | M1, M2, M3, M14, M17, M18, M20, M22, M26, M28, M29, M30, M31, M32 |
| Local file manager — browse, marks, copy/move, rename, delete, new folder, recursive folders | R30, R31, R39 | M11, M12, M13, M19, M23, M24, M25, M27, M28, M29 |
| Anonymous FTP server — four sessions, per-session data ports, path guard | R50, R51 | M5, M6, M7, M21 |

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
| Phase 6: v1 completion and verification | The code is host-complete: the local workspace, the cross-pane folder transfer, and its review remediation. The open work is the 34-row console matrix, which runs once on the code-freeze build (D95); M24's `imc0:` leg cannot be observed on a PCH-1000 |

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

The v1 matrix in [`VERIFICATION.md`](VERIFICATION.md) is the definition of
done: anonymous
download from `ftp.gnu.org`, upload to a login site, local batch work, anonymous
server access from a PC, cancellation, and protected-path refusal, all on
hardware. Every row must be PASS; M24's `imc0:` leg is the one exception that
needs a recorded decision or a PCH-2000 run before v1 can close. The strategy
metrics are transfer completion rate, crash-free sessions, connect-to-first-
listing, and server-matrix pass.

## Risks

| Risk | Mitigation |
|---|---|
| Anonymous read-write server exposes the three Vita volumes, including app data and `tai` | OFF by default, visible ON state, `ACCESS READ-WRITE`, client count; the warning panel is removed (D88); `boot_config.txt` and system roots stay protected |
| Passwords stored unencrypted | State it before the first save; never log them; no false encryption claim |
| WiFi path has a hardware report but no row-level evidence yet | Record connect, PASV, transfer, and timeout results in `VERIFICATION.md` |
| The v1 exit needs every matrix row PASS, but M24's `imc0:` leg is BLOCKED on a PCH-1000 | Run M24 on a PCH-2000, or record an exception that supersedes D95 for that leg |
| Public FTP may decline | Personal servers keep the product useful; no protocol widening in v1 |
| Controller-only density | Design phase reviews each screen before implementation |
| Cross-volume batch move is not all-or-nothing | Each file is atomic; a batch failure leaves completed moves and reports `n OF m MOVED`, never deletes a moved file (D45) |
| Planner and handoff caps are finite | A plan that overflows the arena or step table is refused, never truncated; a lost command is counted (D46) |
| One network transfer and one local job may run together; the reservation table owns mutual exclusion | The worker runs both lanes; a server command answers 450 only on a held path, never because another lane is merely active (D72, D85) |
| GPLv3 boundary | Keep the license and third-party notices intact; document provenance |

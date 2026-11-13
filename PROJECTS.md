# vitaFTPShell project tracker

The live state of this project. Update it after every job. A job is not finished
until the phase, the last job, and the next action reflect it.

Status values: `planning`, `building`, `built`, `verified`, `blocked`.

`built` means it compiles and packages. `verified` means it has run on the
target console with evidence. Host tests alone cannot promote it.

## Board

| Project | Status | Last job | Next | Notes |
|---|---|---|---|---|
| `vitaFTPShell` | `building` | 2026-11-13 — Phase 6: b32 fixed the remote pane (READING, remote actions, DISCONNECT, keepalive); b36 added REFRESH, unsafe-name refusal, download totals, and MLST upload preflight; b37 closes the review pass: an iterated queue pump (no 128 KiB stack overflow), one session teardown that clears the overwrite latch, a 2000-row cap that lists as a truncated success, the ALL DRIVES download fallback, the high-risk delete warning, MLST send-failure handling, and plan/M1 alignment. Recursive folders (R22/R23) remain deferred and refused. Nineteen host suites, 36 preview states, ASan/UBSan clean | Install b37; test the 2000+ entry folder, DOWNLOAD from ALL DRIVES, the high-risk delete warning, the upload preflight (SKIP/OVERWRITE/CANCEL), then the b32/b36 remote pass and the localhost/`FTP.GNU.ORG` data path. Pull the three logs | GPLv3. One UNSAFE eboot. VPK `vitaftpshell-v01.00-b37.vpk` links the real app. No M1–M8 row passed, so not `verified`. Full b20 core is in `logs/b20/`. Public repo: https://github.com/ghostmeson/vitaftpshell |

## Phases

| Phase | State | Output |
|---|---|---|
| Strategy | done | `STRATEGY.md` |
| Requirements | done | `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` |
| Visual design | done | `docs/design/` sheets and `docs/DESIGN.md` |
| Architecture and implementation plan | done | `docs/ARCHITECTURE.md`, the plan's Planning Contract and U1–U17 |
| 1. Build shape | done | Source tree, `build.sh`, one VPK, the layering guard |
| 2. Contracts, graphics, and shared policy | done | Ports, fakes, gfx, parsers, path table, store, log |
| 3. Protocol engines | done | Client and server machines against scripted peers |
| 4. Operations and state | done | Queue, planner, executor, reducer, handoff |
| 5. Screens and composition | done | Seven surfaces, adapters, runtime, host integration test |
| 6. Hardware verification | in progress | M1–M8 evidence; project becomes `verified` |

## Current state

- **Design:** complete. The seven surfaces and their states are settled in
  `docs/design/`; `docs/DESIGN.md` carries the geometry, the input map, and the
  state rules. Decisions D17–D31 cover the design.
- **Architecture:** settled. The plan holds KTD1–KTD18, the thread and handoff
  model, the command table, U1–U17, and the six phases. `docs/ARCHITECTURE.md` is
  the module contract.
- **Build:** Phases 1-5 are done. `./build.sh` emits one
  `vitaftpshell-v01.00-b<build>.vpk` with the real app linked and keeps the
  unstripped ELF. `./tools/test/run.sh` runs the layering guard over every
  `.c`/`.h` under `src/` plus the nineteen host suites; `./tools/preview/run.sh`
  renders 36 settled states. `./send.sh <vita-ip>` refuses an artifact whose
  build number does not match `BUILD_NUMBER`.
- **Next:** Phase 6 — U14 hardware bring-up: install b37 and exercise a 2000+
  entry folder, DOWNLOAD from ALL DRIVES, the high-risk delete warning, the
  upload preflight (SKIP/OVERWRITE/CANCEL), then the b32/b36 remote-pane pass
  (open/parent, a refused CWD, DISCONNECT, reconnect, site switch, remote NEW
  FOLDER/RENAME/DELETE, multi-mark DELETE, REFRESH, unsafe names, folder
  refusal, download totals), the localhost self-site (server ON, site
  `127.0.0.1:1337`), and the seeded `FTP.GNU.ORG`. Pull the three logs and
  record `connect ... path=<ip>` / `connect_fail ... value=`. The project stays
  `building` until the M1–M8 evidence exists.
- **Product shape:** two live panes on the Vita — local files and a saved FTP
  site — with a site manager, transfer dock, local file manager, and an
  anonymous FTP server.
- **Fix history:** `docs/FIXES.md` is the chronological index of every
  Phase 5–6 defect, its root cause, its evidence, and its `HW`/`HOST`/`PENDING`
  status. Read it before re-debugging a failure.
- **Primary user:** the anonymous-first collector pulling homebrew, patches, and
  media from public FTP sites without a PC.
- **Secondary user:** the Vita-as-a-drive user who serves the console's files to
  another machine.
- **v1 scope:** client, manager, and server; multi-select and recursive folders;
  saved sites with remembered passwords; plain passive FTP.
- **Not in v1:** SFTP, FTPS, HTTP, WebDAV, resume, sync, background transfers,
  multiple active transfers, multiple server clients, a credential vault.
- **License:** GPLv3 by choice, nothing vendored. COCKPIT-derived graphics keep
  their MIT notice.

## Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| Anonymous read-write server | Anyone on the LAN can change files while it is ON | Off by default, visible ON state, client count, warning, protected-path denial |
| Plaintext passwords | Saved credentials are readable on the card | Say so before the first save; never log them; no false claim of encryption |
| Unverified WiFi path | The repo's socket code is `built`, not hardware-tested | First hardware session proves connect, PASV, upload, download, and timeouts |
| Public FTP decline | The anonymous-first flow depends on public sites | Personal servers keep the product useful; no protocol widening in v1 |
| Controller-only UI | A dense two-pane manager can become unusable | Design phase goes screen by screen before implementation |
| GPLv3 boundary | Copyleft code and notices must stay intact | Keep the license and third-party notices; document provenance |

## Log

| Date | Job | Result |
|---|---|---|
| 2026-11-08 | Strategy | Wrote `STRATEGY.md`: console-as-peer FTP client and file manager, server as a compatibility surface, GPLv3 via VitaShell. |
| 2026-11-08 | Requirements | Wrote the v1 plan: 70 requirements, six flows, ten acceptance examples, success criteria, scope boundaries, and assumptions. |
| 2026-11-08 | Requirements review | Seven-lens `ce-doc-review`; applied 21 fixes across safety, transfer integrity, input, and verification; recorded four FYIs and the deferred product questions. |
| 2026-11-08 | Workspace directions | Drew three direction-only WORKSPACE compositions at 1:1 with the real 5×7 face: A two ledgers, B one instrument, C focus. The user chose A. |
| 2026-11-08 | Workspace states | Developed A through six states: idle, transferring, done, failed, disconnected, empty/reading. Approved; marks persist after a batch (D18), site identity stays in both places (D19), and the geometry is in `docs/DESIGN.md`. |
| 2026-11-08 | WORKSPACE settled | Recorded the geometry, the pane and dock state rules, and the input map in `docs/DESIGN.md`; added D17-D19. Next: SITES. |
| 2026-11-08 | SITES directions | Drew three compositions at 1:1: A ledger + detail + password note, B single ledger + summary strip, C hero card + full table. The user chose C (D20). |
| 2026-11-08 | SITES states | Developed C through five states: first run, populated, empty, login failed, save unavailable. Approved; login sites carry the unencrypted-password note (D21); recorded the geometry. Next: SITE EDITOR. |
| 2026-11-08 | SITE EDITOR directions | Drew three layouts at 1:1: A preview card + field table, B single tall form, C two-column identity/login. The user chose A (D22). |
| 2026-11-08 | SITE EDITOR states | Developed A through four states: new anonymous, clean edit, invalid field, first-save password warning. Approved; dirty cancel confirms (D23), empty password allowed (D24). Next: SERVER. |
| 2026-11-08 | SITE EDITOR settled | Recorded the card/field geometry and state rules in `docs/DESIGN.md`; added D22-D24. |
| 2026-11-08 | SERVER directions | Drew three compositions at 1:1: A console, B bands, C instrument. The user chose A (D25). |
| 2026-11-08 | SERVER states | Developed A through six states: OFF, starting, ON idle, browsing, transferring, bind failure. Approved; geometry and state rules recorded. Next: ACTIONS. |
| 2026-11-08 | SERVER settled | Recorded the status, warning, client, dock, and error rules in `docs/DESIGN.md`; D25. |
| 2026-11-08 | ACTIONS directions | Drew three compositions at 1:1: A ledger + target, B single table, C pane mode. The user chose A (D26) and the marks-as-source copy/move model (D27). |
| 2026-11-08 | ACTIONS states | Developed A through four states: local single, local multi, remote single, protected root. Approved; geometry and rules recorded. Next: CONFIRM. |
| 2026-11-08 | ACTIONS settled | Recorded the action/target geometry and the marks copy/move model in `docs/DESIGN.md`; D26-D27. |
| 2026-11-08 | CONFIRM directions | Drew three placements at 1:1: A centered interrupt, B bottom sheet, C full screen. Found and recorded that the 5×7 face has no `?`; titles are statements. The user chose A (D28). |
| 2026-11-08 | CONFIRM states | Developed A through six states: delete 3, delete 12 with a scrolling viewport, batch conflict, cancel, server stop, discard. Approved; the long-list rule recorded (D29) and the plan’s CONFIRM input updated. Next: text entry. |
| 2026-11-08 | CONFIRM settled | Recorded the panel geometry, the five-row viewport, the options, and the input in `docs/DESIGN.md`; added D28-D29. |
| 2026-11-08 | TEXT ENTRY | Drew the system-IME contract and three states: open, cancelled, unavailable. The dock-focus rule remains for the close. |
| 2026-11-08 | Documentation | Added `AGENTS.md`, `README.md`, `docs/README.md`, `docs/PROJECT.md`, `docs/ARCHITECTURE.md`, `docs/DESIGN.md`, `docs/DECISIONS.md`, `docs/THIRD_PARTY.md`, the GPLv3 `LICENSE`, and the feature/design/plan folders. |
| 2026-11-08 | Visual design — WORKSPACE | Three directions; chose two equal ledgers; six states; D17–D19. |
| 2026-11-08 | Visual design — SITES | Three directions; chose the hero card and full table; five states; D20–D21. |
| 2026-11-08 | Visual design — SITE EDITOR | Three layouts; chose the preview card and field table; four states; D22–D24. |
| 2026-11-08 | Visual design — SERVER | Three compositions; chose the console; six states; D25. |
| 2026-11-08 | Visual design — ACTIONS | Three compositions; chose the ledger and target; four states; D26–D27. |
| 2026-11-08 | Visual design — CONFIRM | Three placements; chose the centered interrupt and the five-row scrolling list; six states; D28–D29. |
| 2026-11-08 | Visual design — TEXT ENTRY | The system-IME contract, cancelled and unavailable states, the dock-focus rule; the screen inventory is closed. |
| 2026-11-08 | Architecture plan — platform | Settled UNSAFE (the safe authid refuses `uma0:` and unrestricted `ur0:`), the local `tai` policy (writable behind a high-risk confirmation), the risk mark, and the attributed transfer metric. D30–D33. |
| 2026-11-08 | Architecture plan — engine | Settled the server written in the core, one worker thread, control port plus one for data, the 2000-row cap, two text files under `ux0:data/vitaFTPShell/`, the log format and rotation, and MLSD plus LIST. D38–D40. |
| 2026-11-08 | Architecture plan — product choices | Settled every remote delete confirms, one-press retry of a failed item, all-or-nothing cross-volume move, and the server stopping on network loss. D34–D37. |
| 2026-11-08 | Architecture plan | Enriched the plan: R24 and R42 tightened, R71–R73 added, KTD1–KTD18, six diagrams, the server command table, U1–U14, the hardware matrix, and the verification contract. Rewrote `docs/ARCHITECTURE.md`; updated `THIRD_PARTY.md`, `DESIGN.md`, and `AGENTS.md`. No source. |
| 2026-11-08 | Plan review | Architecture, coherence, feasibility, and security review. Split the operation unit into queue, planner, and executor (U8, U16, U17) and added the handoff unit (U15). Fixed the handoff publication protocol, the data-phase sub-states and abort reply alignment, the canonical path form and both rename operands, the protected `data` folders, the data-peer check, templed-write sync and aside recovery, the shared connect budget, the directory-merge conflict rule, the partial-delete report, and the MLST row. Added D41–D42; U7 no longer cites R71. |
| 2026-11-08 | Plan review — open items | Confirmed D41 (server uploads overwrite, disclosed), the 450 for a PC's listing during a local batch, and the same-peer data connection. No plan change. |
| 2026-11-09 | Phase 1 — build shape (U1) | Added `CMakeLists.txt` (VFTPSHELL, 01.00, UNSAFE), `build.sh` with a source-hash build number and one-artifact pruning, `send.sh` with a stale-artifact refusal, `src/main.c`, placeholder `sce_sys/` art, `.gitignore`, and `tools/test/run.sh` with the whole-tree layering guard. Built `vitaftpshell-v01.00-b3.vpk`; the guard fails on `psp2/` and `psp2common/` includes outside the seven-file allowlist. Code review applied two fixes and rejected two findings with evidence. No hardware run. |
| 2026-11-09 | Phase 2 — ports, gfx, and policy (U2–U5) | Added the six port contracts and controllable fakes; adapted COCKPIT's MIT gfx with `font_fit` and the seven-surface layout table; added the software vita2d preview; added reply, listing, path, and format policy; added the site book, settings, atomic store, and typed session-log events. Eight host suites pass, the preview renders 960x544, and the build emits `vitaftpshell-v01.00-b5.vpk` (still a stub). No hardware run. |
| 2026-11-09 | Phase 3 — protocol engines (U6–U7) | Added the shared FTP line reader, remote-path normalizer, and templed write; the passive client machine (resolve, greeting, login, FEAT/MLSD with LIST fallback, browse, RETR/STOR, mutations, cancel, deadlines, cause classes); the passive server machine (control and control+1 bind, one client slot, the command table, MLST/SIZE/MDTM, data peer check, templed STOR, ABOR, 450 busy, network loss, stop); and the scripted fake server/client. Ten host suites pass under ASan/UBSan; the preview still renders 960x544; the build emits `vitaftpshell-v01.00-b7.vpk` (still a stub). Two independent reviews (client/shared and server) found 13 issues; all were fixed: the final-226 drain deadline, the ABOR grace for either reply order, browse MLSD fallback and the 450-listing guard, malformed-reply ABOR, the rename event path, the pending-accept disarm, control-write teardown, the no-slash virtual path, stronger denial tests, the fake send/overflow hooks, and long-name rejection. No hardware run. |
| 2026-11-09 | Standalone repository | Detached vitaFTPShell into its own GPLv3 repository with its commit history, a public README and `.gitignore`, the COCKPIT MIT notice under `LICENSES/`, host-test CI, SPDX headers on every source file, and D44. Build b8; no source behavior change. |
| 2026-11-10 | Phase 5 — screens and composition (U10, U11, U12, U13) | Added the seven screens (WORKSPACE, SITES, SITE EDITOR, SERVER, ACTIONS, CONFIRM, TEXT ENTRY), the shared parts/renderer, the six Vita adapters with plain-C constructors, and the app/worker runtime with the site book and settings wiring. Nineteen host suites pass under ASan/UBSan; the preview renders 36 settled states. Two independent reviews found 20 issues; all were fixed, including the UPLOAD-runs-DELETE P0, the missing high-risk write gate, the double queue release on cancel, the multi-mark confirm overwrite, the server/client slot overlap, the fact-scan truncation, the same-folder copy overwrite, the server transfer timestamp, the remote-root mark clearing, the sites scroll, the confirm item data, and the text-entry scrim. Build `vitaftpshell-v01.00-b15.vpk` with the real app linked. No hardware run. |
| 2026-11-10 | Phase 6 — hardware bring-up and the file-manager slice (U14) | b16 installed after the indexed-PNG fix. b18 ran: drives, listings, and traversal worked; file actions, the server, and the client did not. Added the always-on structured log (D54) and `tools/logs/pull.sh`. Found and fixed three file-manager defects with host tests: the volume-root action lock that disabled every child row, a run that stayed on ACTIONS with no visible result, and a listing apply race that could erase a populated folder. D55. Build `vitaftpshell-v01.00-b19.vpk`. No M1–M8 row passed. |
| 2026-11-11 | Phase 6 — b18 log and crash evidence; IME fix (U14) | Analyzed the 724-line b18 log: five `open_ime` calls all ended `unavailable`, eight client failures were `stage=connect cls=network`, the server ran 32.6 s with no `server_client`, and the delete confirm was cancelled rather than run. The truncated 2,560-byte core still carried `THREAD_INFO`: `VFTPSHELL`, stop `0x30004`, PC `0x810485dc` in `libvita2d`. Followed the SDK IME sample: apputil init, concrete common-dialog language/enter values, forced English, `vita2d_common_dialog_update` every frame, a NONE grace, and `TextInputPort.last_error`. Added the raw `NetResult` to `connect_fail` and the address/port to `server_start`. D56. Build `vitaftpshell-v01.00-b20.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — b20 core and worker stack fix (U14) | The b20 IME fix opened the dialog and a rename reached the executor; the full core (160,672 B gzip, 5,071,200 B ELF) showed the worker thread `pthread` stop `0x30004` at `fsop_run+0x16`, called from `run_plan`. `fsop_run` reserved `0x4c54` on a `0x4d2c` stack because `run_copy_file`'s 8 KiB buffer was inlined. b21 gives the worker 128 KiB, marks `run_copy_file` noinline, uses 16-bit rollback indices, and cuts `fsop_run` to 5,476 B. D57. Build `vitaftpshell-v01.00-b21.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — no-selection actions (U14) | b21 ran on hardware: rename completed and the worker did not crash. The same run showed that with the cursor on `..` the ledger still targeted the current folder. Added `UiActions.has_selection`: no selection disables copy/move/upload/download/rename/delete with `NO SELECTION`; NEW FOLDER remains and creates in the current folder. Ledger band, target panel, and dock show the state. D58. Host tests cover an empty folder and a no-selection new folder. Build `vitaftpshell-v01.00-b22.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — server address and free-space fix (U14) | The b22 server panel advertised `192.29.168.192` for a console at `192.168.29.192`; `nc` and FileZilla timed out. `psp2_net.c` stored `inet_pton` network order in a host-order `NetAddress`; b23 applies `ntohl`. Matched ftpvitalib's 4 MiB net pool and 128 backlog. Restricted free space to one cached query per volume on `ux0:`; `ur0:`/`uma0:` now render `--`, after repeated devctl on subfolders preceded a memory-card warning. D59. Build `vitaftpshell-v01.00-b23.vpk`. No M1–M8 row passed. FTP works on hardware after b23. |
| 2026-11-12 | Phase 6 — clipboard copy/move and destructive DELETE audit (U14) | COPY/MOVE no longer target the current folder; they freeze the marks on a 64-path clipboard and PASTE runs into the folder the user navigated to. CLEAR discards; same-folder MOVE is disabled. DELETE now prunes removed marks against the listing, shows full source paths, uses one high-risk delete confirmation with the item list, refuses `boot_config.txt` before the dialog, and renders `DELETE` in the dock. VitaShell's copy-list/paste and device-entry patterns were the reference. D60/D61. Build `vitaftpshell-v01.00-b24.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — worker-confirm deferral (U14) | A worker confirmation arriving while a local confirm or IME was open was taken and dropped as `CONFIRM BUSY`, leaving the worker pending and all later actions frozen. The app now leaves the handoff confirm latched until the screen is free and the IME is inactive, then feeds it. Build `vitaftpshell-v01.00-b25.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — FTP-client connect diagnostics (U14) | `ftp.gnu.org:21` works from the Mac (220/230); the b18 Vita failed at the old 3 s connect deadline with no IP or raw result. b26 uses 12 s connect / 5 s greeting / 25 s session, logs the resolved IPv4 and `NetResult`, falls back to `gethostbyname` on a zero resolver result, and shows `CONNECTING` in SITES and the remote pane. D62. Build `vitaftpshell-v01.00-b26.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — CONNECTING lifecycle (U14) | b27 clears the CONNECTING state on a remote error or a failed site event, not on an unrelated successful site-book save, so a failed connect cannot leave the UI saying CONNECTING forever. Build `vitaftpshell-v01.00-b27.vpk`. No M1–M8 row passed. |
| 2026-11-12 | Phase 6 — PASV data connection fix (U14) | The b27 localhost log showed control/login working, then `connect_fail stage=data cls=server`: the server advertised its LAN address, the client's control peer was `127.0.0.1`, and the PASV host mismatch was refused. b28 uses the control peer's address with the reply port, which is FTP-bounce safe and fixes localhost/NAT/round-robin servers; first-listing/command/data windows widened and the data `NetResult` logged. D63. Build `vitaftpshell-v01.00-b28.vpk`. No M1–M8 row passed. |
| 2026-11-13 | Phase 6 — remote pane (U14) | Fixed the remote pane end to end. A CWD completion fired `OPERATION_DONE` while the client slot was held, so the immediate list returned `BUSY` and the pane stuck on `READING`; the worker now defers the refresh to the next step and a remote error clears `READING`. Remote NEW FOLDER/RENAME/DELETE were routed into the local `FsPort` planner and failed as `INVALID PATH`; they now use `ftp_client_mkdir/rename/delete/rmdir` through a worker queue, and a directory delete uses RMD. The remote ledger adds DISCONNECT, a site selection tears down the old session, a 20 s NOOP with a 5 s deadline detects a dead server, the site identity clears on loss, remote marks prune with a remote parent helper, and multi-mark DOWNLOAD/UPLOAD covers every mark. D64. Nineteen host suites pass; preview 36 states. Build `vitaftpshell-v01.00-b32.vpk`. No M1–M8 row passed. |
| 2026-11-13 | Phase 6 — remote pane, second pass (U14) | Added the R21 REFRESH action; flagged unsafe remote names (`/`, `\`, `:`, empty, `.`/`..`) with the risk diamond and refused DOWNLOAD/RENAME/DELETE before a local path; carried the listing size through the transfer so the dock shows a download total before the first byte; added the client MLST op and a worker upload preflight with OVERWRITE ALL / SKIP EXISTING / CANCEL; fixed a latent client bug where a listing after a transfer sent the previous `STOR`/`RETR`; CANCEL at a batch conflict now clears the batch. R22/R23 recursion is deferred by the user's choice and a folder source is refused with `FOLDER NOT SUPPORTED`. D65. Nineteen host suites pass; preview 36 states. Build `vitaftpshell-v01.00-b36.vpk`. No M1–M8 row passed. |
| 2026-11-13 | Phase 6 — review pass (U14) | Closed the independent review: `pump_queue` iterates instead of recursing (the 64-item SKIP stack overflow); one `reset_remote_session` clears the queue, batch counters, overwrite latch, remote actions, and upload preflight on DISCONNECT, CONNECT, and loss; a 2000-row cap is a truncated success and the `FIRST 2000` flag is applied with its rows; DOWNLOAD at ALL DRIVES reaches the `ux0:data` fallback; a high-risk delete keeps the boot warning; `ftp_client_mlst` reports a lost control write. Added listing-cap, download-conflict, upload-preflight (unsupported/directory/failure), high-risk-warning, ALL DRIVES download, PASV target, MLST send-failure, and stronger CANCEL tests. Updated the plan (R2, R19, R21-R23, R26, R36, R41, M1) and added D66. Nineteen suites pass, zero warnings, ASan/UBSan clean. Build `vitaftpshell-v01.00-b37.vpk`. No M1–M8 row passed. |
| 2026-11-09 | Phase 4 — operations and state (U8, U16, U17, U9, U15) | Added the ordered transfer queue and single slot; the pure local planner (copy, move, delete, mkdir, rename, merge, conflict gate, risk flags, protected-path refusals); the executor with templed writes, destination sync, aside recovery, rollback, and partial-delete reporting; the input repeat decoder, one-writer UI reducer, and command vocabulary; and the SPSC handoff with critical stop/cancel slots, a coalesced progress seqlock, three listing generations per pane, a bulk lane, and a latched server state. Sixteen host suites pass under ASan/UBSan. Two independent reviews found 16 issues; all were fixed, including the move-batch rollback that could delete the only copy, the overwrite rollback that could leave neither, the volume-root recursion, stale progress after a terminal event, the ACTIONS source, the marked transfer, the high-risk action, the save-dirty state, and the dock focus split. Build `vitaftpshell-v01.00-b10.vpk`. No hardware run. |

## Links

- Product direction: `STRATEGY.md`
- v1 requirements: `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`
- Documentation map: `docs/README.md`
- Third-party code and licenses: `docs/THIRD_PARTY.md`
- Project contract: `AGENTS.md`
- Public repository: https://github.com/ghostmeson/vitaftpshell

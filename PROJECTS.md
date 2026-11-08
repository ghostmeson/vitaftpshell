# vitaFTPShell project tracker

The live state of this project. Update it after every job. A job is not finished
until the phase, the last job, and the next action reflect it.

Status values: `planning`, `building`, `built`, `verified`, `blocked`.

`built` means it compiles and packages. `verified` means it has run on the
target console with evidence. Host tests alone cannot promote it.

## Board

| Project | Status | Last job | Next | Notes |
|---|---|---|---|---|
| `vitaFTPShell` | `building` | 2026-11-09 — Phase 1 (U1): build shape, packaging, send script, placeholder LiveArea art, and the whole-tree layering guard | Start Phase 2: U2, U3, U4, U5 (ports and fakes, graphics base, parsers and paths, store and log) | GPLv3. One UNSAFE eboot. VPK `vitaftpshell-v01.00-b3.vpk`; it is a stub app, so the project is `building`, and no hardware run means not `verified`. `docs/ARCHITECTURE.md` is the module contract; the plan is the source of truth. |

## Phases

| Phase | State | Output |
|---|---|---|
| Strategy | done | `STRATEGY.md` |
| Requirements | done | `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` |
| Visual design | done | `docs/design/` sheets and `docs/DESIGN.md` |
| Architecture and implementation plan | done | `docs/ARCHITECTURE.md`, the plan's Planning Contract and U1–U17 |
| 1. Build shape | done | Source tree, `build.sh`, one VPK, the layering guard |
| 2. Contracts, graphics, and shared policy | next | Ports, fakes, gfx, parsers, path table, store, log |
| 3. Protocol engines | pending | Client and server machines against scripted peers |
| 4. Operations and state | pending | Queue, planner, executor, reducer, handoff |
| 5. Screens and composition | pending | Seven surfaces, adapters, runtime, host integration test |
| 6. Hardware verification | pending | M1–M8 evidence; project becomes `verified` |

## Current state

- **Design:** complete. The seven surfaces and their states are settled in
  `docs/design/`; `docs/DESIGN.md` carries the geometry, the input map, and the
  state rules. Decisions D17–D31 cover the design.
- **Architecture:** settled. The plan holds KTD1–KTD18, the thread and handoff
  model, the command table, U1–U17, and the six phases. `docs/ARCHITECTURE.md` is
  the module contract.
- **Build:** Phase 1 is done. `./build.sh` emits one
  `vitaftpshell-v01.00-b<build>.vpk` and keeps the unstripped ELF.
  `./tools/test/run.sh` runs the whole-tree layering guard; host tests arrive
  with Phase 2. `./send.sh <vita-ip>` refuses an artifact whose build number does
  not match `BUILD_NUMBER`.
- **Next:** Phase 2 — U2 (ports and fakes), U3 (graphics base), U4 (replies,
  listings, paths, formatting), and U5 (site book, settings, store, log). Each
  phase ends at its gate, then stops with a report and a commit.
- **Product shape:** two live panes on the Vita — local files and a saved FTP
  site — with a site manager, transfer dock, local file manager, and an
  anonymous FTP server.
- **Primary user:** the anonymous-first collector pulling homebrew, patches, and
  media from public FTP sites without a PC.
- **Secondary user:** the Vita-as-a-drive user who serves the console's files to
  another machine.
- **v1 scope:** client, manager, and server; multi-select and recursive folders;
  saved sites with remembered passwords; plain passive FTP.
- **Not in v1:** SFTP, FTPS, HTTP, WebDAV, resume, sync, background transfers,
  multiple active transfers, multiple server clients, a credential vault.
- **License:** GPLv3 by choice, nothing vendored. This is an island inside the
  MIT workspace.

## Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| Anonymous read-write server | Anyone on the LAN can change files while it is ON | Off by default, visible ON state, client count, warning, protected-path denial |
| Plaintext passwords | Saved credentials are readable on the card | Say so before the first save; never log them; no false claim of encryption |
| Unverified WiFi path | The repo's socket code is `built`, not hardware-tested | First hardware session proves connect, PASV, upload, download, and timeouts |
| Public FTP decline | The anonymous-first flow depends on public sites | Personal servers keep the product useful; no protocol widening in v1 |
| Controller-only UI | A dense two-pane manager can become unusable | Design phase goes screen by screen before implementation |
| GPLv3 boundary | Code cannot flow back into the MIT projects | Keep reused code inside `vitaFTPShell/`; document provenance |

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

## Links

- Product direction: `STRATEGY.md`
- v1 requirements: `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`
- Documentation map: `docs/README.md`
- Third-party code and licenses: `docs/THIRD_PARTY.md`
- Workspace board: `../PROJECTS.md`
- Workspace contract: `../AGENTS.md`

# Fix log

Chronological index of the defects fixed during Phase 5 and Phase 6. Future
sessions start here, then open the linked authoritative document.

This file is an **index**, not the full evidence record:

- [`VERIFICATION.md`](VERIFICATION.md) owns the hardware matrix, the logs, the
  core-dump analysis, and the host-test evidence.
- [`DECISIONS.md`](DECISIONS.md) owns the numbered product and architecture
  choices (D53–D67 for this period).
- [`ARCHITECTURE.md`](ARCHITECTURE.md) owns the layering and port rules.
- [`DESIGN.md`](DESIGN.md) owns the settled layout, states, and input.

## Status legend

| Tag | Meaning |
|---|---|
| `HW` | The result was observed on the console in the named build. |
| `HOST` | Host suites pass; the Vita result is not yet proven. |
| `PENDING` | Built but not yet run on hardware. |
| `PART` | One part is hardware-observed; the named remainder is not. |

Do not promote a `HOST` or `PENDING` row to `HW` without a console observation
recorded in `VERIFICATION.md`. The project stays `building` until the M1–M8
matrix passes.

## Current state

- **Current artifact:** `vitaftpshell-v01.00-b38.vpk`, 248,757 bytes,
  SHA-256 `00d798a67d913ff5c283b7612a3b20268ed360dd03ea4d2c789ca6954e823061`.
- **Last hardware-observed build:** b37 (operator report, 2026-11-13). The
  report did not enumerate the M1–M8 rows, so those stay `PENDING`. b38 is
  built but not hardware-tested. b28, b32, and b36 were not separately tested.
- **Latest hardware evidence:** b37 operator report (no log pulled yet);
  `logs/session-b27.log` (local actions, server start, localhost control/login,
  PASV data refusal); `logs/b20/` (complete worker-stack core); `logs/b18/`
  (first diagnostic log and truncated core). The `logs/` tree is local and
  git-ignored; copy it with `tools/logs/pull.sh <vita-ip>`.
- **Matrix:** no M1–M8 row has passed. The FTP server works on hardware; the
  local manager and FTP client still need the hardware pass.

## Build and install

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b15 → b16 | Install failed at 99 % with `0x8010113D`; the three `sce_sys` PNGs were 24-bit truecolor (color type 2) | Convert `icon0.png`, `bg.png`, and `startup.png` to indexed 8-bit (color type 3); add `tools/check_assets.sh` to the host gate and `build.sh` | Asset guard; b16 installed | `HW b16` |

## Local pane, listings, and input

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b17 | A local listing failure looked like an empty folder | Report the exact `FsResult`; log `list begin/open/end`; draw `LISTING FAILED` and the reason | `test_integration`, `test_uistate`; b18/b27 listings | `HW b18/b27` |
| b17 → b18 | Local pane started inside `ux0:data`; Triangle/Select were the wrong way for the user's hands | Start at virtual `ALL DRIVES` (`*`) listing mounted volumes; swap Triangle = ACTIONS, Select = SITES; Circle from a volume root returns to `ALL DRIVES` | `test_uistate`, `test_integration`; user confirmed drives/traversal | `HW b18` |
| b19 | A published listing could be applied before its `LISTING_BEGIN`, then the begin cleared the rows and showed `EMPTY` | Apply the published generation while draining `LISTING_END`, between begin and end (D55) | `test_local_file_actions`; b27 listings | `PART` race is host-tested; ordinary listings observed |

## UI actions, clipboard, and DELETE

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b19 | At `ux0:`, every selected child inherited `VOLUME ROOT`, so copy/move/rename/delete were dead | Classify the selected source, not the pane path; only the root itself is locked | `test_local_actions_inside_volume_root` | `HOST` |
| b19 | A run stayed on ACTIONS, which has no status line, so success and failure looked identical | Return to WORKSPACE after a run/confirm; show `state->status` in the idle and ACTIONS docks | `test_action_returns_to_workspace` | `HOST` |
| b22 | With the cursor on `..` or an empty folder, actions silently targeted the current folder from inside it | `UiActions.has_selection`; no selection disables every action except NEW FOLDER; ledger/target/dock say `NO SELECTION`; a guard refuses a manually enabled action | `test_actions_no_selection`, `test_local_new_folder_without_selection` | `HOST` |
| b24 | COPY/MOVE always used the current folder as destination; MOVE could not work and COPY offered to overwrite its own source | VitaShell-style clipboard: COPY/MOVE freeze the marks, the user navigates, PASTE runs into the new folder; CLEAR discards; same-folder MOVE is disabled | `test_clipboard_copy`, `test_clipboard_move` | `HOST` |
| b24 | Deleted marks stayed in the pane, so the next DELETE confirmation listed already-deleted files | `prune_marks` drops a mark whose folder is being listed and whose row is gone; marks in other folders survive | `test_delete_confirm_fresh`, `test_marks_pruned_after_delete` | `HOST` |
| b24 | DELETE showed bare names, ambiguous when marks came from different folders | Confirmation lists the full path of every source (`UiConfirm.local_paths`) | `test_delete_confirm_fresh`, `test_delete_high_risk_and_protected` | `HOST` |
| b24 | A high-risk delete skipped the item list and showed only one risk path | One DELETE dialog carries the full list, `high_risk`, and the risk path; the DELETE choice sets `value=1` for the worker | `test_delete_high_risk_and_protected` | `HOST` |
| b24 | `boot_config.txt` and protected paths needed a single gate before the dialog | Refuse before the confirmation; the worker repeats the plan-time check | `test_delete_high_risk_and_protected` | `HOST` |
| b24 | A local delete rendered as `MOVE` in the dock | Add `XFER_DIR_LOCAL_DELETE`; the dock says `DELETE` | host suites | `HOST` |
| b24 | Basic local rename/delete after the fix | Same actions, no stale state | b27 log: IME rename and DELETE confirm/run completed with `transfer_done` | `HW b27` basic path |
| b25 | A worker confirmation arriving while a local confirm or IME was open was taken and dropped as `CONFIRM BUSY`; the worker stayed pending and all later actions froze | Leave the handoff confirm latched until the screen is free and the IME is inactive; then feed it | host suites | `HOST` |

## IME

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b20 | Every `open_ime` logged `begin unavailable`; new folder and rename could not proceed | Follow the SDK IME sample: `sceAppUtilInit` before the common-dialog config, concrete English/CROSS values, `supportedLanguages` + `languagesForced`, `vita2d_common_dialog_update` every frame, a short NONE grace, raw platform code in `TextInputPort.last_error` | Fake IME path; b20 opened the dialog and reached a rename | `HW b20` |

## Runtime and worker

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b21 | Rename reached `fsop_run` and the worker died with `0x30004`; `fsop_run` reserved 19,540 bytes on a 19,756-byte pthread stack because the 8 KiB copy buffer was inlined | 128 KiB worker stack with fallback; `run_copy_file` noinline; 16-bit rollback indices; b21 `fsop_run` frame 5,476 bytes | Complete b20 core; b21 rename completed; `test_fsop_run` | `HW b21` |

## Network and FTP server

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b17 | START SERVER stuck at `STARTING` with no IP; `ftp_server_start` emitted `BINDING` then `ON`, and the worker emitted a second `STARTING` that overwrote it | Remove the worker's duplicate event | b23 FTP works | `HW b23` |
| b17 | Resolver was created with a `NULL` parameter block | Pass a zeroed `SceNetResolverParam`; add a `gethostbyname` fallback | b18 reached `stage=connect` (DNS worked); b27 localhost | `HW b18/b27` |
| b23 | SERVER advertised `192.29.168.192` for a console at `192.168.29.192`; every client timed out | `inet_pton` yields network order; convert with `ntohl` before storing in `NetAddress.addr` | b27 `server_start host=192.168.29.192`; FTP works | `HW b23/b27` |
| b23 | Free space ran `sceIoDevctl(0x3001)` on every subfolder, including `ur0:`/`uma0:`, before a memory-card warning | Cache free space per volume; query once at the volume root and only on `ux0:`; other volumes render `--` | host suites; b23 run | `PENDING` watch the card warning |
| b23 | 1 MiB net pool and backlog 4; VitaShell's `ftpvitalib` uses 4 MiB and 128 | Match the working library's pool and backlog | b23 FTP works | `HW b23` |

## FTP client

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b20 | `connect_fail` recorded only `stage=connect cls=network`, not why TCP failed | Add `FtpClientEvent.net` and log it as `value=` | `test_connect_refused` | `HOST` |
| b26 | External site failed at the old 3 s connect deadline; no resolved IP, raw result, or connecting UI | 12 s connect, 5 s greeting/login, session budget; log resolved IPv4/port and raw `NetResult`; resolver-zero fallback; `CONNECTING` in SITES and remote pane | b27 `connect path=127.0.0.1`; control/login worked | `HW b27` control path |
| b27 | `CONNECTING` could stick after a remote error or an unrelated site-book save | Clear on remote error/session loss/login failure; not on a successful site-book save | host suites | `HOST` |
| b28 | Localhost control/login worked, then `stage=data cls=server`: the server's PASV LAN address differed from the client's `127.0.0.1` control peer and was refused | Ignore the PASV host; connect to the control peer with the reply port; first listing 10 s, command 10 s, data idle 20 s, session 35 s; log the data `NetResult` | `test_reply` PASV peer/port, `test_client` `test_pasv_nat` | `PENDING b28` |

## Remote pane

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b32 | Opening a remote folder left the pane on `READING`; the status said `LIST FAILED` | The client's CWD completion fired `OPERATION_DONE` while its operation slot was still held; the worker's immediate `remote_list` returned `BUSY`, and no listing end cleared the eager `loading` flag | Defer the refresh to the next worker step; clear `loading` on a remote error; do not read the row arena while loading | `test_remote_navigation`, `test_remote_error_clears_reading` | `PENDING b32` |
| b32 | `REMOTE ACTIONS` offered NEW FOLDER, RENAME, and DELETE, but they failed or were refused as `INVALID PATH` | The worker routed remote actions into the local `FsPort` planner; a remote path has no volume token. Remote DELETE never reached its confirmation | Route remote NEW FOLDER/RENAME/DELETE through `ftp_client_mkdir/rename/delete/rmdir`; use RMD for a marked/selected directory; skip the local protection table for remote paths; disable row actions while disconnected or reading | `test_remote_mutations`, `test_remote_delete_confirm_and_type` | `PENDING b32` |
| b32 | No way to disconnect; selecting another site said `CONNECT REFUSED` | No action or command; `ftp_client_connect` refuses a `READY` session | Add DISCONNECT (closes both sockets immediately, clears the queue/pane/site, returns `DISCONNECTED`); a site selection tears the old session down first | `test_remote_disconnect_action`, `test_remote_disconnect_and_reconnect` | `PENDING b32` |
| b32 | The header stayed `CONNECTED` after the server died | Nothing probed an idle control channel; a half-open flow never produces a FIN; the site identity was never cleared | `NOOP` after 20 s idle with a 5 s reply deadline; clear the site identity on loss; the header reads `NOT CONNECTED` | `test_keepalive`, `test_keepalive_timeout`, `test_idle_control_drop` | `PENDING b32` |
| b32 | Remote marks survived a listing that no longer contained them | `prune_marks` used the local `path_parent`, which rejects `/`-rooted remote paths; the old mark was always kept | Remote parent helper; case-sensitive remote comparison | `test_remote_marks_pruned` | `PENDING b32` |
| b32 | Multi-mark DOWNLOAD/UPLOAD handled only the first mark | The source list expanded marks only for DELETE | Expand marks for DELETE, DOWNLOAD, and UPLOAD | `test_remote_multi_download` | `PENDING b32` |
| b32 | A pane-less error could write `panes[-1]` | `UI_EVT_ERROR` checked only the upper bound; the command-queue error passes `-1` | `UiStateEvent.pane` is an `int`; all pane accesses check both bounds | host suites | `PENDING b32` |
| b32 | A selected row could be read while the listing arena was being written | The reducer read `rows[selected]` even when `loading` | Row access returns `NULL` while loading; Cross/Square are refused | `test_remote_actions_availability` | `PENDING b32` |

## Remote volume-root regression (b38)

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b38 | The remote pane could not open `UX0:`, `UR0:`, or `UMA0:` on localhost; FileZilla could | The b36/b37 unsafe-name guard refused Cross on every row containing `:`, including directories; navigation builds no local path | Let a directory row open; files still refuse `UNSAFE NAME`; add a reducer test and an integration CWD test | `test_remote_volume_root_open` (UI + integration) | `HOST b38` |

## Review pass (b37)

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b37 | A 64-item SKIP batch could overflow the 128 KiB worker stack | `pump_queue` called itself once per released item; its frame is 2.4 KiB | Iterate in `pump_queue` and `continue` to the next item | host suites; `test_upload_conflict` SKIP/CANCEL | `HW b37` |
| b37 | OVERWRITE consent survived DISCONNECT and CONNECT | The teardown paths cleared the queue and counters but not the conflict latch; `batch_finish` could not clear it after the counters were zeroed | One `reset_remote_session` drops the queue, batch counters, conflict answer, remote actions, and upload preflight; `batch_reset` owns the fields | `test_upload_conflict` CANCEL; code path | `HW b37`; cross-session scenario untested |
| b37 | A 2000+ entry local folder was reported failed and wiped | The cap break left `next` at `FS_OK`, so `ok = next == FS_END` was false | A cap stop is a successful truncated listing; only a real read error fails | `test_listing_cap` | `HW b37` |
| b37 | The `FIRST 2000` badge belonged to the previous listing | `apply_listing` ran before the flag was stored from the same end event | Store the truncation flag before applying the rows | `test_listing_cap` | `HW b37` |
| b37 | ACTIONS DOWNLOAD did nothing at ALL DRIVES | The ledger joined the virtual root `*` with the file name and mapped the failure to `PATH TOO LONG`; the worker's `ux0:data` fallback never ran | Skip the join/risk pre-check for `*` and empty; let `default_local_dir` resolve it | `test_remote_download_from_all_drives` | `HW b37` |
| b37 | A high-risk DELETE confirmation lost the console-boot sentence | The confirm printed only `THIS CANNOT BE UNDONE.` | Use the full boot warning for any high-risk delete, keeping the risk path | `test_high_risk_delete_warning`, render check | `HW b37` |
| b37 | Upload preflight branches had no host coverage | The fake peer always advertised MLST and answered only 250/550; no download conflict test | Scripted MLST modes (unsupported, directory, failure) and a SKIP/OVERWRITE download-conflict test | `test_upload_preflight_*`, `test_download_conflict` | `HW b37` |
| b37 | A failed MLST control write left the upload check latched | `ftp_client_mlst` ignored `client_send` failure and returned OK | Return `NOT_CONNECTED` and clear the op when the write fails | `test_mlst_send_failure` | `HW b37` |
| b37 | The PASV data target and CANCEL outcome were not asserted | The fake exposed no connect target; the CANCEL test asserted a count SKIP also leaves | Expose the dialed peer and assert control peer:5004; assert `CANCELLED`, idle dock, and a fresh prompt | `test_pasv_nat`, `test_upload_conflict` | `HW b37` |
| b37 | The plan and M1 gate described superseded behavior | R2/R19/R21-R23/R26/R36/R41/M1 were not updated with D53/D59/D62/D63/D65 | Update the plan to the shipped ALL DRIVES root, control-peer PASV, 35 s budget, 20 s data idle, listing cap, free-space cache, MLST preflight, deferred folders, and the new M1 condition | plan review | `HW b37` |

## Remote pane, second pass (b36)

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b36 | R21 had `UI_CMD_REFRESH` but no action or key | No UI entry; the command was unreachable | Add REFRESH to the remote ledger; it emits `UI_CMD_REFRESH` and the existing worker path re-lists the current folder | `test_remote_refresh` (worker), `test_remote_refresh_action` (UI), render check | `PENDING b36` |
| b36 | A remote name with `/` or `:` was flattened by `last_name` into a local name | The listing row carried no safety fact; the worker used the local name splitter | Mark an unsafe row (`/`, `\`, `:`, empty, `.`/`..`), draw the risk diamond, dim DOWNLOAD/RENAME/DELETE with `UNSAFE NAME`, and refuse Cross/Square before any local path | `test_remote_unsafe_name`, `test_remote_unsafe_and_folder`, client worker name backstop | `PENDING b36` |
| b36 | A download showed no total until the transfer completed | The listing/mark size never reached `XferItem` or `FtpClientTransfer`; the client reset total to zero | Carry size on the command, queue item, and transfer; the client emits it in progress; the dock shows it before the first byte | `test_client` total hint, `test_cancel` dock total, `test_remote_multi_download` totals | `PENDING b36` |
| b36 | An upload silently overwrote an existing remote file | No remote existence check; uploads went straight to STOR | Add the client MLST op and a worker preflight; an existing file asks OVERWRITE ALL / SKIP EXISTING / CANCEL; a directory destination fails; CANCEL clears the batch | `test_mlst`, `test_upload_conflict`, `test_client` listing-after-upload | `PENDING b36` |
| b36 | A selected folder was queued as one RETR/STOR and failed | R22/R23 recursion was not implemented | Deferred by user choice: dim the folder transfer with `FOLDER NOT SUPPORTED`; a marked set with a folder refuses the whole transfer; the worker repeats the refusal | `test_remote_unsafe_and_folder`, `test_marks` folder refusal | `PENDING b36` (deferred R22/R23) |
| b36 | A listing after a transfer could send the previous `STOR`/`RETR` instead of `MLSD`/`LIST` | `client_begin_listing` never reset `data_kind`; the verb selector reused the last transfer | Set `data_kind = CDATA_LIST` when a listing begins | `test_client` upload-then-list | `PENDING b36` |
| b36 | CANCEL at a batch conflict advanced to the next item | The conflict CANCEL path reused the active-item cancel helper | CANCEL clears the batch queue and reports `CANCELLED` | `test_upload_conflict` third attempt | `PENDING b36` |

## Logging

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b18 | `session.log` had only 9 lines and could not explain a listing, connect, or server start | Always-on structured records: app/screen/key/command/IME(no value)/list/queue/transfer/connect/server/save/error; 256 KiB × 3 generations; app pushes to the worker log ring (D54) | b18/b27 logs; password sentinel scan | `HW b18/b27` |
| b20 | A local rename crash produced a complete core but not a full stack because the b20 ELF was overwritten | Keep the unstripped ELF for any hardware build; the b20 core is in `logs/b20/` | b20 core analysis | note for future builds |

## Hardware evidence trail

| Build | What ran | Evidence |
|---|---|---|
| b16 | Installed after indexed-PNG fix; app opened | `VERIFICATION.md` install section |
| b18 | Drives, folders, files, traversal; IME unavailable; client failed at connect; server no client; 724-line log | `logs/b18/session.log` |
| b20 | IME opened; rename reached the worker; worker stack crash; complete 5,071,200-byte core | `logs/b20/`, `VERIFICATION.md` b20 core |
| b21 | Rename completed; no worker crash | User report; `PROJECTS.md` |
| b23 | Correct server IP; FTP works | `PROJECTS.md`, `VERIFICATION.md` b22/b23 |
| b27 | Local rename/delete; server start; localhost control/login; PASV data refusal | `logs/session-b27.log` |
| b28 | **Not tested** | PASV fix |
| b32 | **Not tested** | Remote pane fixes; current artifact |

## Decisions from this period

| Decision | Subject |
|---|---|
| D53 | Local pane starts at `ALL DRIVES`; Triangle/Select swap |
| D54 | Always-on structured session log, 256 KiB × 3 |
| D55 | Listing applies inside begin/end; actions return to WORKSPACE |
| D56 | IME follows the SDK sample; raw failure code |
| D57 | 128 KiB worker stack; `fsop_run` frame budget |
| D58 | No selection is not the current folder |
| D59 | Host-order advertised address; free space is a device query |
| D60 | Local COPY/MOVE use clipboard + PASTE |
| D61 | Destructive confirmations name full paths; one gate |
| D62 | FTP client connect window and failure naming |
| D63 | PASV uses the control peer, not the advertised host |
| D64 | Remote sessions are explicit, refreshable, and self-healing |
| D65 | Remote refresh, unsafe names, upload preflight, and the folder-transfer limit |
| D66 | A session teardown owns the batch; `pump_queue` iterates; a capped listing is a truncated success |
| D67 | The unsafe-name rule guards transfers, not navigation |

## Still open

- **b38 remote volume-root test:** install b38 and open `UX0:`, `UR0:`, and
  `UMA0:` through the remote pane; the server-side CWD is already proven by
  FileZilla.
- **b37 row-level observations:** the operator reported b37 hardware-tested on
  2026-11-13, but the M1–M8 rows, the remote-pane pass, the 2000+ entry folder,
  the high-risk delete warning, and the localhost/`FTP.GNU.ORG` data path still
  need their observed results transcribed into `VERIFICATION.md`.
- **b28 PASV hardware test:** superseded by b37; the PASV fix is still unproven
  on the console.
- **Cross-session batch latch:** `reset_remote_session` clears it, but no host
  test drives OVERWRITE -> server busy -> DISCONNECT -> CONNECT -> new upload.
- **b24/b25 local-action hardware pass:** clipboard copy/move, no-selection,
  stale-mark pruning, full-path delete, high-risk delete, confirm deferral.
  b27 observed basic rename/delete only.
- **External passive data port:** if `connect_fail ... stage=data ... value=2`
  appears at the correct IP, the network is blocking the passive port; test a
  LAN FTP server before changing the client.
- **R22/R23 recursive transfers:** deferred by the user's choice. A folder is
  now refused with `FOLDER NOT SUPPORTED` instead of sent as one RETR/STOR.
  This remains a plan gap.
- **Upload conflict detection is MLST-only.** A server that does not advertise
  MLST is not probed and keeps the prior overwrite behavior. A server that
  advertises it but returns 502 is treated as unsupported.
- **Download totals trust the listing.** A server that reports a wrong size
  makes the dock total wrong; the bytes-done count remains correct.
- **M1–M8:** none passed.
- **Remaining `[C]` claims:** `select` over SceNet, IME masked text, the
  128 KiB pthread stack acceptance, `ur0:`/`uma0:` free space (now reported
  unknown), the fixed `control+1` PASV data port, and the reply-line cap.

## Method for the next session

1. Read this file, then `PROJECTS.md`, then the plan and the relevant decision.
2. Reproduce with a host test before touching the Vita. If the host stack is
   larger than the worker's, add a stack-budget note or a core-dump check.
3. Check the platform claim against `vitasdk/` headers or a working reference
   (`vita-cap-test`, `ftpvitalib`, `vita-ftp-client`) before wiring it in.
4. Add a new row here with the build, symptom, root cause, fix, evidence, and
   hardware status. Add a decision if the choice would surprise a reader.
5. Run `./tools/test/run.sh`, `./tools/preview/run.sh`, and `./build.sh`; keep
   the unstripped ELF for the build handed to the console.
6. Pull the three session logs after every hardware run; the evidence is worth
   more than a verbal report.

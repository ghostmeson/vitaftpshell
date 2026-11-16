# Fix log

Chronological index of the defects fixed during Phase 5 and Phase 6. Future
sessions start here, then open the linked authoritative document.

This file is an **index**, not the full evidence record:

- [`VERIFICATION.md`](VERIFICATION.md) owns the hardware matrix, the logs, the
  core-dump analysis, and the host-test evidence.
- [`DECISIONS.md`](DECISIONS.md) owns the numbered product and architecture
  choices (D53–D91 for this period).
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
recorded in `VERIFICATION.md`. The project stays `building` until the M1–M13
matrix passes.

## Current state

- **Current artifact:** `vitaftpshell-v01.00-b72.vpk`, 265,072 bytes,
  SHA-256 `9bb0ae6c7109be44c75a1fc89c7119aef4fdb8e211ff46ae9211702e4316888f`.
- **Latest hardware-observed build:** b60 (operator report, 2026-11-15):
  FileZilla upload and download work with two sessions; the app-local client
  transfers on all three volumes. The session log is `logs/session-b60.log`.
- **Previous hardware-observed build:** b43 (operator report, 2026-11-14):
  copy, paste, new folder, rename, and download work and the dock words read
  correctly. b49 added the parallel lanes; b40 crashed (four core dumps analysed
  in `VERIFICATION.md`). b28, b32, b36, b38, and b39 were not separately tested.
- **Latest hardware evidence:** `logs/session-b60.log` (FileZilla two sessions;
  STOR 265,539 and 786 bytes, RETR 786 bytes, all 226; app-local transfers on
  `ux0:`, `ur0:`, and `uma0:`); `logs/session-b57.log` (the old global-busy
  failure); `logs/b20/` (worker-stack core); `logs/b18/` (first diagnostic log).
  The `logs/` tree is local and git-ignored; copy it with
  `tools/logs/pull.sh <vita-ip>`.
- **Matrix:** the b58 self-connection upload and download pass on hardware.
  b60 accepts up to four clients with one data transfer at a time (D86);
  FileZilla's browse and transfer connections both connect, and its upload and
  download pass. b61–b63 are contained in b66, and b66–b72 are the current
  line; none of them has run on hardware since b60. M5 remains PART only for a
  FileZilla rename/delete and the full three-volume browse. M10 and M13 are
  partly observed; M1–M9, M11, and M12 await the b72 console run.

## Network closure (b73-b74, host)

The per-session data ports, the SIZE preflight refresh, and the threaded
transfer suite; the artifact is `vitaftpshell-v01.00-b73.vpk`.

| Build | Symptom | Cause and fix | Evidence | Status |
|---|---|---|---|---|
| b73 | **The server could not tell two sessions on one host apart.** | One passive listener on `control+1` with an address-only peer check. Each session slot now owns `control+1+slot`, bound on its first PASV and held until stop, so the arrival port identifies the owner (D92); a data-port bind failure answers 425 and leaves the server ON. | `test_slot_ports_two_sessions`, `test_data_port_bind_failure`, `test_slot_listener_binds_once`, `test_rearm_drains_stale_peer`, `test_control_port_ceiling`, `test_wait_set_stays_within_cap`; host run | `HOST b73` |
| b74 | **The SERVER port field could not open text entry, so the control port could not be changed from the screen.** | Pressing Cross re-sent the edit command with the current value. It now opens the IME seeded with the live port and a five-character bound, and the answer returns as an edit command while the SERVER screen owns the prompt. | `test_server_port_editor`, `test_server_actions`; host run | `HOST b74` |
## Residual open-issues sweep (b72)

| Build | Symptom | Cause and fix | Evidence | Status |
|---|---|---|---|---|
| b72 | **Nothing proved the app held the listing the UI rendered.** | The handoff adopt mechanism had a unit test but no app-level one. `test_app_listing_slot_held` drives the real App: an idle frame keeps the slot held, and a REFRESH publishes a newer generation that is adopted while the old slot is released. | `test_app_listing_slot_held`; host run | `HOST b72` |
| b72 | **The queue lane stopped at the lost-reply terminal.** | `test_download_final_reply_loses_queue` now reconnects the saved site after the kept terminal and downloads one more file, asserting the RETR, the arrival, and a done dock. | `test_download_final_reply_loses_queue`; host run | `HOST b72` |
| b72 | **The reply-backlog PASS mask rested on inspection.** | `fake_net_block_sends` wedges a control socket so the server's 16 KiB reply buffer fills. `test_trace_drops_password` drops `PASS`, `pass`, `PASS<TAB>`, and bare `PASS`: every spelling traces `DROPPED PASS ****`, no argument reaches the trace, and a dropped NOOP proves the branch ran. The multiline raw-append paths now enter the same backlog state. | `test_trace_drops_password`; host run | `HOST b72` |
| b72 | **The owner RETR exemption was proven only while the data accept was pending.** | The fake filesystem now holds 16 KiB, so a RETR stays ACTIVE with the data socket undrained. `test_owner_size_while_active` reads SIZE mid-transfer: the owner gets 213 with the real size, the other session 450. | `test_owner_size_while_active`; host run | `HOST b72` |
| b72 | **The cancel dialog called a complete download partial.** | The single-lane cancel says the transferred file is removed and that KEEP leaves it running; the two-lane cancel says cancelled transfers remove their files. An explicit cancel still discards (D89). | `test_render_screens`, `test_uistate`; host run | `HOST b72` |
| b72 | **`FEAT` advertised a `perm` fact MLST never emitted.** | The feature list is now `MLST type*;size*;modify*;`; `test_feat_matches_mlst` compares it with the MLST body. | `test_feat_matches_mlst`; host run | `HOST b72` |
| b72 | **A DHCP change left a stale `227` endpoint.** | The one-second link check adopts a known, non-zero address when it changes; an unknown address keeps the last usable one. The listener stays wildcard-bound and the data port stays fixed (D59). | `test_pasv_follows_link_address`; host run | `HOST b72` |
| b72 | **A second cancel could overwrite the first before the worker read it.** | Each lane has its own critical slot plus a single-lane slot; the oldest press pops first. The local executor consumes the press it peeked, and a lane never takes the single-lane answer while the other lane has its own cancel. | `test_cancel_slots`, `test_two_cancel_presses`; host run | `HOST b72` |
| b72 | **A kept `.vftp.old` was visible but unmarked.** | `UiRow.debris` is set from the sidecar kind in both panes and draws `ASIDE` in yellow in the value column; the `.part` stays unlisted (D81). | `test_aside_debris_mark`, `test_aside_debris_row`; host run | `HOST b72` |
| b72 | **A successful rename with a failed aside cleanup reported a failed commit.** | `FsOpProgress.notice` records `ASIDE CLEANUP FAILED - OLD COPY KEPT AS ...`; the step counts as done, the worker carries the notice on the done event, and the dock keeps the green word and shows the notice on line three. | `test_aside_cleanup_failure`, `test_local_rename_aside_notice`, `test_aside_notice_done`; host run | `HOST b72` |
| b72 | **The queue's terminal and conflict latches had no production reader.** | Remove `xfer_take_terminal`, `xfer_set_conflict`, `xfer_conflict`, their fields, and the dead outcome arguments. The worker owns the batch conflict and logs each terminal. | `test_xfer`; host run | `HOST b72` |
| b72 | **The confirm answer was resolved twice.** | One `resolve_confirm_answer` serves Cross and Circle, preserving the prompt-id echo, the KEEP close, and the PATH BUSY waiting effects. | `test_uistate`; host run | `HOST b72` |
| b72 | **TSan reported a race in the fake clock, not the app.** | The fake clock's shared time is atomic. `tools/test/tsan.sh` probes TSan, keeps the negative handoff ordering check, and runs the real app thread with the fakes included; a source failure now fails the gate. | `tools/test/tsan.sh`; host run | `HOST b72` |
| b72 | **An upload to a server without MLST kept the stale-overwrite behavior.** | The preflight prefers MLST and falls back to SIZE (213 exists, 550 missing, 5xx unsupported). A destination the current remote listing shows as a folder is still refused; a stale probe answer is discarded when its item is gone. D91 supersedes D65 section 4. | `test_size_stat`, `test_upload_preflight_size_fallback`, `test_upload_preflight_size_folder`; host run | `HOST b72` |


**Accepted residuals.** `path_canonicalize` still does not fold trailing dots
or spaces; the Vita filesystem's own folding remains `[C]` and needs the
console. A SIZE-only probe cannot report a type for a destination outside the
current remote listing; the server's STOR refusal is the remaining signal, and
the parent refresh closes the stale-listing half.

## Review remediation (b72)

The full review of the residual sweep (`ce-code-review`, base `dd7aad3`, run
`20261116-002414-b4c0cc07`) returned `Ready with fixes`. The validator confirmed
14 findings, rejected two, and left one unresolved; the confirmed items that
needed code are below.

| Build | Symptom | Cause and fix | Evidence | Status |
|---|---|---|---|---|
| b72 | **A cancelled upload preflight could apply its verdict to the next item.** | `upload_stat_ready` held one destination's answer while a cancel could drop that item. The result now carries its destination, a mismatch is discarded, and a cancel, clear, or batch end clears the latch. | Code path; host suite | `HOST b72` |
| b72 | **The TSan gate skipped itself when the app test failed to build.** | Any compile failure exited 0. The script now probes TSan once with a trivial program and treats a later source failure as a hard error; the app run must print its OK line. | `tools/test/tsan.sh`; host run | `HOST b72` |
| b72 | **A SIZE-only upload could not see a folder destination.** | SIZE reports no type. The worker now checks the current remote listing's row for the destination and keeps the `DESTINATION IS A FOLDER` refusal. | `test_upload_preflight_size_folder` | `HOST b72` |
| b72 | **A lane could peek the other lane's single-lane cancel; a notice survived a failure; the lane count and header contract were loose; the queue call still took dead outcome arguments.** | The peek and consume now share the other-lane guard; the local executor consumes the press it saw; a failure clears the notice; `HANDOFF_CANCEL_LANE_COUNT` names the two slots; the SIZE/MLST headers name 500/501/502/504; `xfer_release`/`xfer_cancel_active` take only the queue and next item. | `test_cancel_slots`, `test_uistate`, `test_client`, `test_xfer`; host run | `HOST b72` |

**Validator outcomes.** #14 (the adopt/writer claim race) was rejected as
pre-existing: the b67 change that introduced adopt is in the base and this pass
only tests it. #19 (`FEAT` `modify*`) was rejected because the server does emit
`modify=` when the filesystem reports an mtime; the Vita adapter's unknown mtime
is the recorded D48 limit, not a false capability. #9 (a stale single-lane
cancel replayed against another lane) was unresolved; the peek/consume guard and
consume-at-peek change above remove the mechanism, but no test drives the exact
cross-thread interleaving. The PASV review item was not a defect: the adapter
binds the listener to `INADDR_ANY` (the wildcard keeps it valid across a link
change), so only the advertised `227` address needed the refresh.

## Open-issues pass (b68)

| Build | Symptom | Cause and fix | Evidence | Status |
|---|---|---|---|---|
| b67 | **A download whose size changed after the listing was deleted as a partial when the final reply was lost.** | The keep rule trusts the caller's listing hint. Ask `SIZE` before PASV and use a 213 reply as the transfer's own size; 500/502/550 keep the listing hint (U1, R1, D89). | `test_download_size_preflight`; host run | `HOST b68` |
| b67 | **A rename could create a `.vftp.part`/`.vftp.old` name.** | `path_check_rename` did not apply the sidecar denial to its destination. Reject the destination in the shared path rule; the source stays allowed so an aside can be repaired (U2, R2). | `test_sidecar_names`, `test_rename_sidecar_refused`; host run | `HOST b68` |
| b67 | **The UI released its listing slot after every frame while it kept rendering the rows.** | The handoff held the slot only for the frame. `handoff_listing_adopt` keeps the held generation or releases it and acquires the newest; the app holds each pane until shutdown (U3, R3). | `test_listing_adopt`; host run | `HOST b68` |
| b67 | **The owner's own RETR metadata read and the two-item queue terminal had no tests.** | Add a pending-RETR owner test with a guard that refuses the foreign session, and mark both queue items so the terminal names the cancelled remainder (U5, R5). | `test_owner_size_while_download`, `test_download_final_reply_loses_queue`; host run | `HOST b68` |

**Data-connection identity (U4).** Resolved by one data port per session slot (D92): slot *n* owns the port one above the control port for its slot, bound on its first PASV, so a data connection is attributed by the port it arrives on. Two sessions from one host can no longer be confused. The earlier decision to keep one fixed port is history in D39 and D59.

## Review remediation (b66)

| Build | Symptom | Cause and fix | Evidence | Status |
|---|---|---|---|---|
| b66 | **A rename onto an existing name opened the conflict prompt, and the answer ran nothing.** | The single-item conflict producer survived while its answer branch was deleted; the prompt frees the plan, so the answer must re-plan. Restore the branch, count a refused re-plan as failed, and key the prompt kind on an enum (U1, R1). | `test_local_rename_conflict`; host run | `HOST b66` |
| b66 | **A download that received every byte then waited forever for the final 226.** | No deadline covered the draining state; the stage deadline had been cleared and the data deadline removed. Arm a final-reply deadline, keep a known-complete download, remove a short one, end the session, and name the remaining queue (U2, R2, D89). | `test_download_final_reply_missing`, `test_download_final_reply_short`, `test_upload_final_reply_missing`, `test_download_final_reply_loses_queue`, render kept case; host run | `HOST b66` |
| b66 | **A client that dropped between PASV and its data command kept the only data slot; every other client got 450.** | The close test counted only an active transfer or a pending accept. Release any non-idle slot the closing session owns, emit an event only for an active transfer, and delegate the control-failed path to the close path (U3, R3). | `test_close_while_armed`, `test_owner_close_active_one_event`; host run | `HOST b66` |
| b66 | **A foreign ABOR disarmed the owner's armed PASV.** | The third ABOR branch disarmed any session's slot. Only the owner may disarm it (U3, R4). | `test_foreign_abor_keeps_pasv`; host run | `HOST b66` |
| b66 | **The server answered 450 for the file the same session was uploading.** | The guard could not exclude one session. Let the owner read metadata for its own transfer path; an upload destination answers 550, never the old size; other sessions stay guarded (U4, R5, D90). | `test_owner_size_while_upload`; host run | `HOST b66` |
| b66 | **A dropped PASS command wrote its argument to the session log.** | The drop path traced the raw line before the mask. Mask by a case-insensitive first token before the backlog branch (U4, R6). | `test_trace_masks_password` (accepted path), `test_trace_drops_password` (all four spellings against a wedged reply buffer); host run | `HOST b66` |
| b66 | **The local terminal badge vanished while the network lane ran.** | The inline badge predicate accepted only active or waiting. Share one predicate and name the terminal words (U6, R8). | `test_render_workspace`; host run | `HOST b66` |
| b66 | **The CLIENT panel paired one peer with another session's command.** | The panel read server-global peer and command fields. Carry the session index, peer, masked verb, and path on the event; fall back to the live session on close (U6, R8). | `test_client_event_pairs_session`; host run | `HOST b66` |
| b66 | **Reservation logic duplicated the path module's case folding and ancestry, and the header exposed its 288 KiB layout.** | Export `path_ci_equal`/`path_is_ancestor`; make `Reserve` opaque with create/destroy; state the lane-blind contract (U5, R7). | `test_reserve`, `test_path`; host run | `HOST b66` |
| b66 | **The stack gate copied the worker recursion depth; the sanitizer covered 3 of 21 suites and nothing ran it.** | Read the depth from one header and fail closed; run every suite under ASan/UBSan and add a CI step (U8, R11). | `test_asan`; gate self-test; host run | `HOST b66` |

**Still open from the b65 review, recorded rather than built:**

- The data-connection accept cannot distinguish two sessions on one host; the fixed port is kept and the limitation is accepted for v1 (U4).

**Still open from the b63 review, recorded rather than built** (U7):

- The data-connection accept cannot distinguish two sessions on one host; the fixed port is kept and the limitation is accepted (U4).
- `path_canonicalize` does not fold trailing dots or spaces; the Vita filesystem's own folding is unverified `[C]` and needs a console test.

## Upload defect (b57 → b58)

| Build | Symptom | Evidence | Status |
|---|---|---|---|
| b57 | **FTP server uploads never start.** FileZilla queues a file until it fails; MKD, rename, delete, and listings work, and the app's own client to localhost fails the same way. | The server traces every command, reply, data accept, temp open, first read, commit, and close to `ux0:data/vitaFTPShell/session.log`; `PASS` is masked. The log showed `transfer_start` → `FTPC PASV` → `FTPR 450 Transfer in progress` on the app's own client. | `HW b57` |
| b58 | The server refused every data and mutating command while the app's network lane was active, because the worker set the legacy global `external_busy` flag. The app's client therefore deadlocked against its own server, and a PC was refused on unrelated paths. | Remove `ftp_server_set_external_busy` and the global gate; the path guard still answers 450 on a held path (D85). | `test_held_path`; b57 trace; b58 console: app-local upload of the 263,206-byte VPK and download both 226 | `HW b58` |
| b58 | **FileZilla `450 Busy`.** The server already had a client (the app's remote pane), and FileZilla's extra control connection was refused. | The server accepted one client at a time (D41). | b58 session log: app client connected while FileZilla got 450 Busy | `HW b58` |
| b63 | **The SERVER warning panel remained after the warning text was trimmed.** | Remove the warning region from the layout; the client panel takes the full right column at 582,76,360×352 (D88). The status panel keeps `ACCESS READ-WRITE`, ON/OFF, and the client count; the header repeats the server state. | `test_render_screens`; preview; host run | `PENDING b63` |
| b62 | **The SERVER warning named the app data folder and the writable `tai` folders.** | Remove those two dim lines; keep the general anonymous read-write line, the `boot_config.txt` denial, the overwrite note, and the lifecycle. | `test_render_screens`; host run | `PENDING b62` |
| b61 | **The server refused its own app data and `tai` writes, and the local high-risk prompt blocked routine edits.** | The path policy denied server reads/writes in `ux0:data/vitaFTPShell` and writes under `tai`; the reducer/worker opened a second confirmation for high-risk paths. b61 removes those gates (D87) and keeps `ur0:tai/boot_config.txt` plus the system roots. | `test_path`, `test_server`, `test_uistate`, `test_integration`, `test_client`, ASan/UBSan; b61 hardware retry pending | `PENDING b61` |
| b60 | **FileZilla cannot hold its browse and transfer control connections at the same time.** | Accept up to four sessions, each with its own login, cwd, line reader, reply buffer, idle deadline, and rename state; one data transfer owns the PASV/data port at a time; a fifth connection gets 450; closing a non-owner does not abort the transfer (D86). Also fix the guard order: a free path returned before the HOLD, so no server transfer ever held its path (D86). | `test_two_clients`, `test_client_limit`, ASan/UBSan; b60 console: FileZilla `clients=2`, STOR 265,539 and 786 bytes, RETR 786 bytes, all 226; app-local all volumes | `HW b60` |

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
| b19 | At `ux0:`, every selected child inherited `VOLUME ROOT`, so copy/move/rename/delete were dead | Classify the selected source, not the pane path; only the root itself is locked | `test_local_actions_inside_volume_root` | `HOST b66` |
| b19 | A run stayed on ACTIONS, which has no status line, so success and failure looked identical | Return to WORKSPACE after a run/confirm; show `state->status` in the idle and ACTIONS docks | `test_action_returns_to_workspace` | `HOST b66` |
| b22 | With the cursor on `..` or an empty folder, actions silently targeted the current folder from inside it | `UiActions.has_selection`; no selection disables every action except NEW FOLDER; ledger/target/dock say `NO SELECTION`; a guard refuses a manually enabled action | `test_actions_no_selection`, `test_local_new_folder_without_selection` | `HOST b66` |
| b24 | COPY/MOVE always used the current folder as destination; MOVE could not work and COPY offered to overwrite its own source | VitaShell-style clipboard: COPY/MOVE freeze the marks, the user navigates, PASTE runs into the new folder; CLEAR discards; same-folder MOVE is disabled | `test_clipboard_copy`, `test_clipboard_move` | `HOST b66` |
| b24 | Deleted marks stayed in the pane, so the next DELETE confirmation listed already-deleted files | `prune_marks` drops a mark whose folder is being listed and whose row is gone; marks in other folders survive | `test_delete_confirm_fresh`, `test_marks_pruned_after_delete` | `HOST b66` |
| b24 | DELETE showed bare names, ambiguous when marks came from different folders | Confirmation lists the full path of every source (`UiConfirm.local_paths`) | `test_delete_confirm_fresh`, `test_delete_high_risk_and_protected` | `HOST b66` |
| b24 | A high-risk delete skipped the item list and showed only one risk path | One DELETE dialog carries the full list, `high_risk`, and the risk path; the DELETE choice sets `value=1` for the worker | `test_delete_high_risk_and_protected` | `HOST b66` |
| b24 | `boot_config.txt` and protected paths needed a single gate before the dialog | Refuse before the confirmation; the worker repeats the plan-time check | `test_delete_high_risk_and_protected` | `HOST b66` |
| b24 | A local delete rendered as `MOVE` in the dock | Add `XFER_DIR_LOCAL_DELETE`; the dock says `DELETE` | host suites | `HOST b66` |
| b24 | Basic local rename/delete after the fix | Same actions, no stale state | b27 log: IME rename and DELETE confirm/run completed with `transfer_done` | `HW b27` basic path |
| b25 | A worker confirmation arriving while a local confirm or IME was open was taken and dropped as `CONFIRM BUSY`; the worker stayed pending and all later actions froze | Leave the handoff confirm latched until the screen is free and the IME is inactive; then feed it | host suites | `HOST b66` |

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
| b20 | `connect_fail` recorded only `stage=connect cls=network`, not why TCP failed | Add `FtpClientEvent.net` and log it as `value=` | `test_connect_refused` | `HOST b66` |
| b26 | External site failed at the old 3 s connect deadline; no resolved IP, raw result, or connecting UI | 12 s connect, 5 s greeting/login, session budget; log resolved IPv4/port and raw `NetResult`; resolver-zero fallback; `CONNECTING` in SITES and remote pane | b27 `connect path=127.0.0.1`; control/login worked | `HW b27` control path |
| b27 | `CONNECTING` could stick after a remote error or an unrelated site-book save | Clear on remote error/session loss/login failure; not on a successful site-book save | host suites | `HOST b66` |
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




## b49 review pass

| Build | Defect | Fix | Evidence | Status |
|---|---|---|---|---|
| b49 | A second local action overwrote the live run, leaking its temp and handle | Refuse a local action while a run or a WAIT item owns the lane; `run_plan_begin` refuses a non-empty lane | `test_local_batch_refused_item`, integration suite | `HOST b49` |
| b49 | Local progress and item terminals painted the network dock | Carry the direction on local progress and on the done/failed event; route by lane | `test_two_lane_dock` | `HOST b49` |
| b49 | CANCEL NETWORK could cancel the local run, and a peeked cancel could be swallowed | The cancel slot reports its command type; only a local cancel stops the local run; consume only a local one | reducer and worker suites | `HOST b49` |
| b49 | Cancelling a local batch stopped only the current item | A cancel request ends the batch once the current item rolls back | `test_cancel_copy`, `test_local_batch_terminal` | `HOST b49` |
| b49 | A held RNTO sent 450 then 550 | Return after the guard's 450 and clear RNFR | `test_held_rename` | `HOST b49` |
| b49 | The WAITING badge never rendered | Show it before the first byte and clear it on the next lane event | render suite, waiting case | `HOST b49` |
| b49 | QUIT freed an in-flight run without aborting its temp | `fsop_run_abort` closes the source and removes the temp before destroy | executor suite | `HOST b49` |
| b49 | The reservation arena was never reclaimed | Compact the surviving paths on release | `test_release_reclaims_arena` | `HOST b49` |


## b55 deferred review items

These are review findings that the b55 pass did not close. They are recorded
here so they do not live only in a review report.

| Item | Why it waited | Status |
|---|---|---|
| The `UiState` row pointer can outlive a released listing generation | Fixed in b67: the app holds the slot and adopts a newer generation in one step | `test_app_listing_slot_held`; `HOST b68` |
| A kept `.vftp.old` is visible but not marked as debris | Fixed in b72: `UiRow.debris` and the yellow `ASIDE` token (D81) | `test_aside_debris_mark`, `test_aside_debris_row`; `HOST b72` |
| A failed aside cleanup still reports a failed commit | Fixed in b72: `FsOpProgress.notice` and the yellow dock notice | `test_aside_cleanup_failure`, `test_local_rename_aside_notice`; `HOST b72` |
| The queue's terminal and conflict latches have no production reader | Fixed in b72: the latches and dead outcome arguments are removed | `test_xfer`; `HOST b72` |
| Confirm resolution is duplicated across the two key branches | Fixed in b72: one `resolve_confirm_answer` serves both keys | `test_uistate`; `HOST b72` |
| TSan over the threaded app test | Fixed in b72: the fake clock is atomic and the script runs the app with the fakes included | `tools/test/tsan.sh`; `HOST b72` |
| M1-M13 on b55 | Superseded by the b66 console run | `PENDING` |

## b55 review remediation

| Build | Change | Evidence | Status |
|---|---|---|---|
| b55 | A local open failure returns a distinct client result; the worker owns one failed terminal and the post-call latch guard | `test_download_destination_failure`, `test_client` | `HOST b55` |
| b55 | The local lane starts beside a network transfer; lane-scoped cancel; both lanes render WAITING | `test_two_lanes_progress`, `test_wait_resume_rechecks`, reducer and render cases | `HOST b55` |
| b55 | Worker prompts carry a sequence; a mismatched answer leaves the deferred prompt latched | reducer id echo; `test_wait_resume_rechecks` | `HOST b55` |
| b55 | The UI command buffer lives in App; the dispatch frame is 80 B and the gate checks it | gate reports 80 B; D79 | `HOST b55` |
| b55 | The client and server pumps consume a per-tick budget and the wait is bounded by the local lane | `test_download_budget`, server suite | `HOST b55` |
| b55 | CONNECT/REFRESH refuse or defer while the server is busy | code path; server suite | `HOST b55` |
| b55 | The server drains foreign data peers, reports a reply backlog, checks its 450, and closes a 30 s idle session | `test_peer_check`, `test_peer_check_recovers`, `test_control_idle` | `HOST b55` |
| b55 | The transfer sidecar has one owner; a failed restore names the kept copy; `.part` is hidden | `test_path`, `test_reserve`, `test_fsop_run` | `HOST b55` |
| b55 | Dead exports removed, sites seeded by events, protected-root flag, action context, rename SKIP and capacity wording | suites; `test_fsop_plan` | `HOST b55` |
| b55 | The real worker pthread is exercised on the host: listing through the handoff, quit, join, ASan/UBSan clean | `test_app_thread`, `tools/test/asan.sh` | `HOST b55` |
| b55 | TSan reports a race in the singleton fake clock, not the app | `test_app_thread` under TSan after the atomic clock; `tools/test/tsan.sh` | `HOST b72` |

## b41 parallel lanes (b49)

| Build | Change | Evidence | Status |
|---|---|---|---|
| b49 | The local executor is resumable: a heap-owned run state and `fsop_run_step` advance by a byte budget, the worker interleaves a local slice between client/server/queue ticks, and `fsop_run` remains the blocking wrapper (U25) | `test_stepped_copy`, `test_stepped_cancel`, the existing executor suite | `HOST b49` |
| b49 | Canonical-path reservations: a local plan reserves every step, a download its destination, an upload its source; equal paths conflict unless both read, tree operations conflict with ancestors/descendants, sidecars are covered, and the containing directory is not locked (U26, D72) | `test_reserve` | `HOST b49` |
| b49 | A collision asks WAIT / SKIP / CANCEL; WAIT is head-of-line and retries. A server data or mutating command on a held path answers 450, disarms PASV, and holds the path until the transfer ends (U26) | `test_held_path`, `test_path_busy_prompt` | `HOST b49` |
| b49 | A paste with duplicate destination names is refused and a move source inside another is skipped before any command (U26) | `test_paste_guards` | `HOST b49` |
| b49 | The dock has a network and a local lane: the network stays full, the local lane is a badge, the local lane alone takes the dock, and the focused cancel offers KEEP / CANCEL NETWORK / CANCEL LOCAL (U27, D78) | `test_two_lane_dock`, the two-lane cases in `test_render_workspace`, preview 39 states | `HOST b49` |

## b40 crash pass (b41/b43)

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b41 | Local folder copy, a clipboard batch, NEW FOLDER, and RENAME crashed the app | `local_batch_preflight`'s `Path[64]` made `handle_action` reserve 71,680 bytes; `local_batch_execute` called `handle_action` recursively; the worker used 158,472 of 128 KiB (stop `0x10006`) | Big arrays moved to the `Worker`; batches advance one item per `worker_step`; stack raised to 192 KiB; `tools/check_worker_stack.py` fails a frame over budget in `build.sh` | core analysis in `VERIFICATION.md`, `test_local_batch_terminal`, `test_local_batch_preflight`; b43 operator report: copy/paste/new folder/rename all complete | `HW b43` |
| b43 | Every local copy ended `DESTINATION COMMIT FAILED` after all bytes moved; the dock said `COPIED` in red | `ftp_temp_finish` treated both syncs, close, and rename as one commit condition | Sync is a recorded durability hint; close and rename decide the commit; the message names `CLOSE` or `RENAME`; `sync skipped` is logged | `test_sync_is_best_effort`, `test_rename_failures`; b43 operator report: copies and downloads complete | `HW b43` |
| b43 | A batch with a refused item could end green | Refused items never reached `run_plan`, so they were never counted | `local_batch_advance` counts an item that neither ran nor raised a prompt as failed | `test_local_batch_refused_item` | `HOST b43` |
| b43 | A total failure read `COPIED` in red | `done_dock` always used the past-tense operation word | Nothing succeeded reads `FAILED` in red; a partial batch keeps the word in yellow | `test_render_workspace` (done/failed dock) | `HOST b43` |

## b40 correction pass

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b40 | With nothing marked, COPY and MOVE were unavailable | The local action gate required `mark_count > 0`; DELETE, UPLOAD, and DOWNLOAD already fell back to the cursor | Use the marked set when present, otherwise the cursor row; keep the root and protected refusals | `test_cursor_copy_folder`, `test_clipboard_copy`, reducer suite | `HOST b40` |
| b40 | The dock always said `DONE`; RENAME and NEW FOLDER reported as COPY | `done_dock` hard-coded the word and `run_plan` defaulted to the copy direction | Past-tense word per operation, one terminal per batch, skipped and cancelled counts, partial yellow, failure red | `test_render_workspace`, `test_local_batch_terminal`, `test_local_batch_preflight` | `HOST b40` |
| b40 | A local batch showed `1 OF 1` per item and could end green after a failed item | The worker had one shared network counter and no local batch | Batch id, index, total, and last flag on every command; a worker-owned local batch with one terminal; one pane refresh | `test_local_batch_terminal`, `test_local_batch_preflight` | `HOST b40` |
| b40 | The local conflict prompt named blank rows and its CANCEL continued the batch | The planner cleared its step table before the prompt read it, and the confirm had no batch branch | Store the existing destinations on the plan; prompt once for the whole batch with the real paths; CANCEL ends the batch | `test_local_conflict_prompt`, `test_local_batch_preflight` | `HOST b40` |
| b40 | CANCEL could not stop a local copy | The worker passed `NULL` as the executor callback; the confirm emitted a normal command the blocked worker could not read | Dedicated cancel slot, a callback that peeks it at every chunk, a one-shot consume, and a between-item cancel | `test_cancel_copy`, reducer cancel test | `HOST b40` |
| b40 | The cursor row was a thin bar over a faint wash | The selection was a 2 px bar and 6 % wash | Cyan slab with near-black text in the workspace, ACTIONS, and SITES; yellow glyphs behind a dark keyline; inactive bar unchanged | `test_active_pane`, render suites, preview | `HOST b40` |
| b40 | The active pane was only a 2 px yellow left edge | The design allowed a glow only on the wordmark | Full yellow outline following the chamfer plus a static additive bloom; the lost pane loses the bloom | `test_active_pane`, render suite, preview | `HOST b40` |
| b40 | Failures and refusals were yellow, like selection and risk | `C_RED` was defined but unused | Red for the pane band, listing failure, dock failed word, editor, SERVER, SITES, and text entry; risk warnings stay yellow | `test_pane_error`, render suites, preview | `HOST b40` |
| b40 | A one-item transfer ended as `2 OF 1` | The terminal index was `batch_done + 1` after the increment | Use the incremented 1-based count; omit the count for a single item | integration suite | `HOST b40` |

## Visible action errors (b39)

| Build | Symptom → cause | Fix | Evidence | Status |
|---|---|---|---|---|
| b39 | A refused action looked like a software bug: the pane kept its rows and only the small dock line changed | A failed action set `pane->error` but the pane only renders it when empty; the unsafe refusal set only the dock status | Draw the error in the pane band in place of the path; set it for refusals and worker errors; clear it on the next key or a successful listing | `test_pane_error`, `test_remote_unsafe_and_folder` | `HOST b39` |

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
| D68 | A refused action reports inside the pane, not only in the dock |

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
- **Upload conflict detection falls back to SIZE.** A server that does not
  advertise MLST is probed with SIZE (D91); a server that offers neither is not
  probed and keeps the prior overwrite behavior. A destination outside the
  current remote listing cannot be typed by SIZE alone.
- **Download totals trust the listing.** A server that reports a wrong size
  makes the dock total wrong; the bytes-done count remains correct.
- **M1–M13:** none is fully passed. M5 is PART (FileZilla rename/delete and
  the three-volume browse unobserved); M10 and M13 are partly observed; the
  rest await the b72 console run.
- **Remaining `[C]` claims:** `select` over SceNet, IME masked text, the
  128 KiB pthread stack acceptance, `ur0:`/`uma0:` free space (now reported
  unknown), the per-session data ports (D92), and the reply-line cap.

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

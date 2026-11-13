# Verification

**Build:** vitaftpshell-v01.00-b39.vpk
**SHA-256:** acc6457ce2fc171ac3ce602b2473c12d53e880594eb4e2eb6ff86e3a71b08fe3
**ELF:** build/vitaftpshell, 509,264 bytes
**Console:** PS Vita 3.65 Enso; the system version string may read 3.74 (spoofed)
**Date:** 2026-11-13
**Status:** b37 hardware-tested (operator report); b39 built, not tested; M1–M8 row transcription pending

This file is the U14 evidence record. A row is PASS only when the observed
result matches its pass condition and the evidence is recorded here. A row that
cannot be run is BLOCKED, never PASS. `verified` requires every row PASS.

## b37 hardware run

**Date:** 2026-11-13
**Build:** `vitaftpshell-v01.00-b37.vpk`
**Report:** The operator reported the b37 VPK installed and exercised on the
console.

This establishes that b37 ran on the target hardware and moves the b37 fix rows
from `HOST` to `HW`. The report did not supply row-level observations for the
M1–M8 matrix, so each row below stays `PENDING` until its observed result and
evidence are recorded here. The project therefore stays `building`.

## Setup

| Item | Value |
|---|---|
| Vita LAN IP | pending |
| Wi-Fi / network | pending |
| VitaShell FTP on 1337 | pending |
| Unsafe Homebrew enabled | pending |
| Mounted volumes | pending |
| PC-side MLSD server | pending |
| PC-side LIST-only server | pending |
| DOS-listing server | pending |
| FileZilla | pending |
| Scratch folder | pending |

## Matrix

| Row | Setup | Expected | Observed | Evidence | Result |
|---|---|---|---|---|---|
| M1 | `ftp.gnu.org`, anonymous, passive | Connect to first listing under 10 s; a 1 MB+ download completes with matching size | pending | pending | PENDING |
| M2 | LAN server with MLSD | MLSD is used, browse and download complete | pending | pending | PENDING |
| M3 | LAN server with MLSD disabled | LIST fallback parses, browse and download complete | pending | pending | PENDING |
| M4 | DOS-listing server | Names, sizes, and folders parse; a download completes | pending | pending | PENDING |
| M5 | FileZilla against the Vita server | Browse all three volumes, upload, rename, delete | pending | pending | PENDING |
| M6 | Denials through the server | `boot_config.txt`, `ux0:tai` upload, volume-root delete all refuse, and nothing is created | pending | pending | PENDING |
| M7 | Second client and stop-during-write | 450 to the second client; stop during a write confirms and leaves no partial | pending | pending | PENDING |
| M8 | Network loss and cancel | Server stops and reports; cancel takes effect within 5 s; no crash dump | pending | pending | PENDING |

## Install and launch

- Upload: `./send.sh <vita-ip>` with VitaShell's FTP server on port 1337.
- Install path: `ftp://<vita-ip>:1337/ux0:/data/`; install the VPK with Unsafe
  Homebrew enabled.
- First attempt: b15 failed at 99% with `0x8010113D`.
- Diagnosis: the three sce_sys PNGs were 24-bit truecolor (bit depth 8, color
  type 2). The Vita promoter requires indexed 8-bit PNGs (bit depth 8, color
  type 3). See the defect table.
- Fix: converted `icon0.png`, `bg.png`, and `startup.png` to indexed 8-bit
  without changing their colors; added `tools/check_assets.sh` to the host gate
  and `build.sh`.
- Install result: b16 installed successfully; the app launches.
- b17 added the server event-order fix, resolver fix, virtual root, button remap, and listing error reporting.
- b18 added the always-on structured session log (D54).
- b18 ran on hardware: drives, folders, files, and traversal work; file actions,
  the server, and the client did not. See the b18 observations below.
- b19 fixed the file-manager action path and the listing apply order (D55).
- b20 followed the SDK's IME sample (apputil init, valid common-dialog config,
  forced English, a common-dialog update every frame) and logged the raw IME
  and connect failure codes.
- b20 ran on hardware: the IME opened and a rename reached the worker, then the
  worker thread crashed in `fsop_run` on a stack overflow. The complete core is
  in `logs/b20/`.
- b21 fixed the worker stack and the `fsop_run` frame (D57). On hardware, the
  rename completed and the worker no longer crashed. The same run showed that
  with the cursor on `..` the action ledger still targeted the current folder.
- b22 made "no selection" explicit: every file action is dead except NEW
  FOLDER, which creates in the current folder (D58).
- b22 ran on hardware. The SERVER panel advertised `192.29.168.192`, but the
  system TTY and the actual console were at `192.168.29.192`; every client
  timed out. The warning about the memory card followed repeated
  `sceIoDevctl(0x3001)` calls on subfolders of `ur0:`/`uma0:`.
- b23 fixed the address formatting (`inet_pton` network order → host order),
  raised the net pool to 4 MiB and the backlog to 128 as VitaShell's ftpvitalib
  does, and made free space a per-volume query on `ux0:` only. On hardware, FTP
  now works.
- b24 replaced same-folder COPY/MOVE with a VitaShell-style clipboard: COPY or
  MOVE freezes the marks, the user navigates, and PASTE runs into the current
  folder. DELETE prunes removed marks, names full paths, and uses one
  high-risk delete confirmation with the item list.
- b25 defers a worker confirmation while a local confirm or the IME owns the
  screen, instead of taking it and dropping it as `CONFIRM BUSY` while the
  worker stays pending.
- b26 is the FTP-client diagnostic build: ftp.gnu.org:21 answers from the Mac,
  so the site is healthy. The Vita timed out at the 3 s connect deadline. b26
  raises it to 12 s (5 s greeting/login, 25 s session), logs the resolved IPv4
  and raw `NetResult`, falls back to `gethostbyname` if the resolver returns
  zero, and shows `CONNECTING` in SITES and the remote pane.
- b27 clears the `CONNECTING` state on a remote error or a failed site event,
  not on an unrelated successful site-book save.
- b28 is the PASV fix. The b27 localhost log shows the control connection and
  login succeeding, then `connect_fail stage=data cls=server`: the server
  advertised its LAN address in PASV, the client's control peer was
  `127.0.0.1`, and `reply_parse_pasv` refused the different host. b28 ignores
  the PASV host and uses the control peer with the advertised port; it also
  widens the first-listing/command windows. It is the current build. Launch
  pending.

## Row detail

### b18 hardware observations (2026-11-10)

The b18 build opened and the local pane worked as a browser:

- `ALL DRIVES` listed `ur0:`, `ux0:`, and `uma0:`.
- Folders and files were visible and forward/back traversal worked.
- File actions did not work: copy, move, rename, delete, and new folder
  produced no visible change.
- The server panel reached `STARTED` with an IP and port, but no FTP client on
  the LAN could connect.
- The default site failed immediately with `LOGIN FAILED`; no `CONNECTING`
  state was shown.

The b18 session log was not pulled before b19 was built. `tools/logs/pull.sh
<vita-ip>` reads `session.log`, `session.old.log`, and `session.old2.log` from
VitaShell's FTP server and writes them to `logs/`; it never writes to the
console.

### b18 session-log analysis (2026-11-10)

The supplied log has 724 lines across three launches. It confirms the browser
work and pinpoints the three failures:

- **File manager:** `ux0:`, `ux0:data/GBS`, `ur0:`, `ur0:tai`, and `uma0:` all
  list and traverse. The ACTIONS ledger opens and the DELETE confirm opens.
  There is **no** `run_action` line: the recorded delete ended with
  LEFT/RIGHT/LEFT/CROSS, which leaves the confirm on CANCEL. The user cancelled
  the operation rather than running it.
- **New folder / rename:** five attempts log
  `command cmd=open_ime detail=NEW FOLDER NAME` immediately followed by
  `ime cmd=begin detail=unavailable`. The IME never opens, so those two actions
  cannot complete. This is the file-manager blocker that b20 addresses.
- **Client:** eight lines of
  `connect_fail site=ftp.gnu.org stage=connect cls=network`. The stage is
  `connect`, not `resolve` or `login`: DNS succeeded and TCP connect failed.
  The log did not carry the raw `NetResult`; b20 adds it.
- **Server:** `server_start` at 100172 ms, no `server_client` line, and a
  user-called `server_stop` at 132776 ms. The listener was on for 32.6 s and no
  TCP connection reached the app. b20 logs the advertised address and control
  port at start.

The control address shown by the SERVER panel came from the same link query
that b20 logs, so the next run can compare the displayed address with the PC's
route and with a direct `nc <vita-ip> 1337` test.

### b18 crash dump

The supplied `.psp2dmp.tmp` is 2,560 bytes and `gzip -t` fails with `unexpected
end of file`; it was copied before the console finished writing it. The partial
gzip decompresses to 7,870 bytes of an ELF32 core. The first `THREAD_INFO` note
is intact:

- thread `VFTPSHELL`, status 1, stop reason `0x30004` (data abort)
- PC `0x810485dc`
- sibling threads `SceCommonDialogWorker`, `SceGxmDisplayQueue`, `pthread`

The PC maps into `libvita2d` in the current unstripped ELF; the b18 ELF was not
retained, so the exact b18 function is not assigned. The b18 log has no
`app_stop`, so the process ended outside the in-app quit path. Re-copy the dump
after the console finishes writing, and keep the matching unstripped ELF.

### b20 crash dump (complete)

The b20 dump is complete: 160,672 bytes of gzip, `gzip -t` passes, and it
decompresses to a 5,071,200-byte ELF32 core. The `THREAD_INFO` note shows:

- `VFTPSHELL` (main): waiting in `sceKernelDelayThread`
- `SceCommonDialogWorker`: waiting
- `SceGxmDisplayQueue`: waiting
- `pthread` (the app worker): **stop `0x30004`, PC `0x8105706e`, LR `0x8107388d`**

The core records the app at runtime base `0x81055000`; subtracting that base
maps PC/LR to:

- PC `fsop_run+0x16`
- LR `run_plan` in `worker.c`

The fault is a worker stack overflow, not a bad path. The b20 `fsop_run` frame
reserved `0x4c54` (19,540) bytes; `STACK_INFO` gives the worker stack
`0x4d2c` (19,756) bytes. The rename path never copies a byte, but the compiler
had inlined `run_copy_file`'s 8 KiB buffer into `fsop_run`, so the full frame
was reserved anyway.

The IME fix worked: the user reached the rename and pressed save. The next call
was the local rename executor, and that is where the worker died.

b21:

- gives the worker a 128 KiB stack (`pthread_attr_setstacksize`; falls back to
  the default if the platform refuses the request);
- marks `run_copy_file` noinline so a rename does not reserve its buffer;
- stores rollback indices as `uint16_t` (2048 fits) instead of `int`.

The b21 ELF has `fsop_run` at a 5,476-byte frame and `run_copy_file` separate at
11,324 bytes, so a rename uses roughly 5.5 KiB and a copy roughly 16.8 KiB.

### b22 server address and memory-card finding

The b22 SERVER panel advertised `192.29.168.192`; the console's system TTY in the
same boot recorded `network ready. myaddr: 192.168.29.192`. A PC `nc` to the
panel address timed out, and FileZilla could not connect either.

Root cause: `psp2_net.c` stores `inet_pton`'s network-order result directly in
`NetAddress.addr`, but the rest of the port uses host order (`net_connect`
applies `htonl`). The worker formats the address with shifts, so the two middle
octets came out swapped. b23 applies `ntohl`.

The same run produced a Vita warning about the memory card after the server
started. The app was calling `sceIoDevctl(0x3001)` for free space on every
subfolder listing, including `ur0:` and `uma0:`. VitaShell's `file.c` queries
free space on the **device entry**, not on each folder, and its FTP library
(`ftpvitalib`) uses `INADDR_ANY`, a 128-connection backlog, and a 4 MiB net
pool. b23 matches the pool/backlog, caches free space per volume, and queries
only `ux0:`; `ur0:`/`uma0:` render `--`.

The server control path still uses the non-blocking `select` on the worker. If
it does not accept a client even at the corrected address, the next step is
ftpvitalib's blocking accept on a dedicated server thread, not another socket
guess.

### b24/b25 file-manager changes

VitaShell's file manager uses a copy/cut list and a paste action in the
destination folder. b24 adopts that model for local COPY/MOVE:

- COPY/MOVE freezes the marked paths into a clipboard and returns to WORKSPACE.
  No file is touched. The dock shows `COPY n - TRIANGLE TO PASTE`.
- The destination folder's ACTIONS screen shows PASTE first and CLEAR second;
  COPY/MOVE are hidden while a clipboard is armed.
- PASTE runs one copy/move command per clipboard path into the current folder.
  A same-folder MOVE is disabled (`ALREADY HERE`); a same-folder COPY still
  creates a copy name.
- The clipboard is independent of the marks, so navigating across volumes does
  not lose it. The 64-path cap names the limit instead of truncating silently.

DELETE audit:

- Marks are pruned against the new listing, so a deleted file cannot reappear in
  the next confirmation.
- The confirmation always lists the full paths of every source, including marks
  from another folder.
- A high-risk delete uses the same delete list with a risk path and the DELETE
  choice; it no longer replaces the list with a path-only high-risk dialog.
- `boot_config.txt` is refused before the confirmation; the worker repeats the
  check.
- The dock direction for a delete is DELETE, not MOVE.

Host evidence: `test_clipboard_copy`, `test_clipboard_move`,
`test_delete_confirm_fresh`, `test_delete_high_risk_and_protected`, and
`test_marks_pruned_after_delete`; all nineteen suites pass.

### b26/b27 FTP client connection

The default site is healthy. From the Mac:

- `dig +short ftp.gnu.org` → `209.51.188.20`
- `nc -vz ftp.gnu.org 21` → succeeded
- `curl ftp://ftp.gnu.org/` → `220 GNU FTP server ready.`, then anonymous
  `230 ...` and a directory listing

The b18 Vita log failed at `stage=connect` after about 3.4 s, which is the old
3 s connect deadline. The log carried neither the resolved address nor the raw
`NetResult`, and the UI showed `NOT CONNECTED` while the worker was trying.

b26 changes:

- connect deadline 12 s, greeting/login 5 s, session budget 25 s;
- logs `connect ... path=<resolved IPv4> port=21 stage=begin` before the
  connect and `connect_fail ... path=<ip> value=<NetResult>` on failure;
- falls back to `gethostbyname` if the resolver returns `0.0.0.0`;
- shows `CONNECTING` in the SITES footer and the remote pane until the session
  succeeds or fails.

`value` is the `NetResult`: `2` timeout, `4` refused, `5` unreachable,
`9` socket/select error. If the log says `path=209.51.188.20 value=2` after
12 s, the Vita's network path is dropping port 21 while the Mac's path works;
that is a network block, not an FTP parser bug. Test a LAN FTP server on the
PC (the site editor accepts its IP and port) before changing the client again.
If the value is `4` or `9`, the failure is at the server or the socket layer and
we can act on it.

### b28 PASV data connection

The b27 localhost log shows the control path working:

- `connect site=localhost path=127.0.0.1 port=1337`
- `server_client clients=1`
- `list path=/ begin`
- `connect_fail stage=data cls=server value=0`
- `server_client clients=0`, `CLIENT CLOSED`

The server advertised its LAN address in the PASV reply. The client's control
peer was `127.0.0.1`, and `reply_parse_pasv` refused the different host as a
foreign address. That is the `PASSIVE REFUSED` failure, not a network timeout.

b28 ignores the PASV host and connects to the **control peer's address with the
advertised data port**. This is the safe NAT behavior: the untrusted PASV host
cannot redirect the client, while localhost, NATed servers, and servers with
multiple addresses work. The first-listing window is 10 s, command 10 s, data
idle 20 s, and the session 35 s; a data connect failure records its raw
`NetResult` in the existing `connect_fail ... value=` line.

The older external `ftp.gnu.org` run failed at `first_listing` after the old
2 s window. The PASV-host change and the wider window must both be tested on the
console; the Mac still receives `220`/`230` from the site. b37 supersedes b28
as the build to install and keeps the same PASV behavior.

### b32 remote pane

The b27/b28 logs proved the control/login and PASV paths. The remote pane then
failed in four independent ways, all reproduced on the host:

1. **Stuck on READING.** The host navigation test opened `/SUB`: the client
   received CWD 250, then the worker's immediate `remote_list` returned `BUSY`
   because `OPERATION_DONE` fires before the client releases its operation slot.
   The status became `LIST FAILED`, but no listing begin/end followed, so
   `UiState.panes[REMOTE].loading` stayed 1. The pane rendered `READING` forever.
   b32 sets `refresh_remote` and lists on the next worker step; a remote error
   also clears `loading`.
2. **Remote actions were local.** NEW FOLDER, RENAME, and DELETE were routed to
   `FsOp`/`FsPort`; `path_canonicalize` requires a volume token, so a remote
   `/NAME` failed. Remote DELETE was refused as `INVALID PATH` before its
   confirmation. b32 routes them through the FTP client and skips the local
   protection table for remote paths.
3. **No disconnect and no session switch.** There was no action and no command;
   selecting another site while `READY` returned `CONNECT REFUSED`. b32 adds
   DISCONNECT and tears down the old session before a new connect.
4. **CONNECTED after death.** Nothing probed an idle control channel. b32 sends
   `NOOP` after 20 s of inactivity and goes `LOST` after 5 s without a reply;
   a drop clears the site identity and the header reads `NOT CONNECTED`.

Host evidence: `test_integration` (`test_remote_navigation`,
`test_remote_mutations`, `test_remote_disconnect_and_reconnect`),
`test_uistate` (`test_remote_actions_availability`,
`test_remote_disconnect_action`, `test_remote_delete_confirm_and_type`,
`test_remote_error_clears_reading`, `test_session_lost_clears_identity`,
`test_remote_marks_pruned`, `test_remote_multi_download`), and `test_client`
(`test_keepalive`, `test_keepalive_timeout`, `test_idle_control_drop`,
`test_disconnect`). The console test is pending.

### b36 remote pane, second pass

The b32 host tests exposed a latent client bug while adding the upload
preflight: `client_begin_listing` never reset `data_kind`, so a listing after a
transfer sent the previous `STOR`/`RETR` verb. In the b36 harness, the refresh
after an upload sent a second `STOR` and the peer saw the same file twice. b36
sets `data_kind = CDATA_LIST` whenever a listing begins; `test_client` now
uploads, then lists, and asserts `MLSD /`.

b36 also closes the four b32 gaps:

- **R21 REFRESH:** the remote ledger has a REFRESH row (six remote actions);
  it emits `UI_CMD_REFRESH` and the worker re-lists the current folder.
- **R44 unsafe names:** a listed name with `/`, `\`, `:`, empty, or `.`/`..`
  is flagged; the row keeps the yellow diamond, DOWNLOAD/RENAME/DELETE are dim
  with `UNSAFE NAME`, and Cross/Square refuse before a local path is built.
- **Download totals:** the listing/mark size rides on the command, `XferItem`,
  and `FtpClientTransfer`; the dock shows the total before the first byte.
- **Upload conflicts:** when FEAT advertises MLST, the client sends `MLST` before
  a STOR. An existing file asks OVERWRITE ALL / SKIP EXISTING / CANCEL once per
  batch; a directory destination fails; a server without MLST keeps the prior
  overwrite behavior. The Vita server advertises MLST, so this is the normal
  path against the console's own server.
- **Folder transfers:** R22/R23 are deferred by the user's choice. A folder
  source is refused with `FOLDER NOT SUPPORTED` in the UI and the worker; a
  file transfer still works. This is a plan gap, not a mis-queued RETR/STOR.

Host evidence: `test_client` (`test_mlst`, upload-then-list, download total
hint), `test_integration` (`test_remote_refresh`, `test_remote_unsafe_name`,
`test_upload_conflict` with SKIP/OVERWRITE/CANCEL, `test_cancel` dock total),
and `test_uistate` (`test_remote_refresh_action`,
`test_remote_unsafe_and_folder`, folder refusal in `test_marks`). The console
test is pending.

### b37 review pass

An independent review of the uncommitted Phase 6 work found five P1 and ten
P2/P3 issues. b37 fixes them and adds the missing host coverage:

- `pump_queue` no longer recurses; a 64-item SKIP batch used ~150 KiB against
  the 128 KiB worker stack.
- One `reset_remote_session` clears the queue, batch counters, conflict latch,
  remote actions, and upload preflight on DISCONNECT, CONNECT, and loss. A
  stale OVERWRITE ALL answer can no longer cross a login.
- A folder that stops at the 2000-row cap is a successful truncated listing;
  the `FIRST 2000` badge is applied with the rows it describes.
- ACTIONS DOWNLOAD at the ALL DRIVES root uses the worker's `ux0:data`
  fallback instead of failing with `PATH TOO LONG`.
- A high-risk DELETE confirmation shows the console-boot warning.
- `ftp_client_mlst` reports a lost control write instead of leaving the upload
  check latched.
- The fake peer can script MLST as unsupported, a directory, or a failure;
  the PASV test asserts the data socket dialed the control peer and reply
  port; the CANCEL test asserts `CANCELLED`, an idle dock, and a fresh prompt.

Host evidence: `test_listing_cap`, `test_remote_download_from_all_drives`,
`test_high_risk_delete_warning`, `test_upload_preflight_unsupported`,
`test_upload_preflight_directory`, `test_upload_preflight_failure`,
`test_download_conflict`, `test_mlst_send_failure`, `test_pasv_nat`, and the
strengthened `test_upload_conflict`. Nineteen suites pass with zero warnings;
ASan/UBSan are clean for integration, client, and UI state. The console test is
pending.

### M1 - ftp.gnu.org anonymous

- Steps:
- Expected: connect to first listing under 10 s; a 1 MB+ download completes
  with matching size.
- Connect to first listing: pending
- Download: pending
- Local free space: pending
- Session log excerpt: pending
- Password check: pending
- Failure class: pending
- Result: PENDING

### M2 - LAN server with MLSD

- Steps:
- Expected: MLSD is used; browse and download complete.
- Server / version: pending
- MLSD or LIST: pending
- Download: pending
- Session log excerpt: pending
- Failure class: pending
- Result: PENDING

### M3 - LAN server, MLSD disabled

- Steps:
- Expected: LIST fallback parses; browse and download complete.
- Server / version: pending
- MLSD or LIST: pending
- Download: pending
- Session log excerpt: pending
- Failure class: pending
- Result: PENDING

### M4 - DOS-listing server

- Steps:
- Expected: names, sizes, and folders parse; a download completes.
- Server / version: pending
- Observed listing: pending
- Download: pending
- Failure class: pending
- Result: PENDING

### M5 - FileZilla against the Vita server

- Steps:
- Expected: browse all three volumes; upload, rename, delete.
- FileZilla version: pending
- Volumes visible: pending
- Upload: pending
- Rename: pending
- Delete: pending
- Failure class: pending
- Result: PENDING

### M6 - Denials through the server

- Steps:
- Expected: `boot_config.txt`, `ux0:tai` upload, and volume-root delete all
  refuse; nothing is created.
- `boot_config.txt` read/write/rename/delete: pending
- `ux0:tai` upload: pending
- Volume-root delete: pending
- Post-attempt directory state: pending
- Failure class: pending
- Result: PENDING

### M7 - Second client and stop-during-write

- Steps:
- Expected: 450 to the second client; stop during a write confirms and leaves
  no partial.
- Second client reply: pending
- Stop-during-write confirm: pending
- Partial file after stop: pending
- Failure class: pending
- Result: PENDING

### M8 - Network loss and cancel

- Steps:
- Expected: server stops and reports; cancel takes effect within 5 s; no crash
  dump.
- Network loss result: pending
- Cancel latency: pending
- Crash dump: pending
- Failure class: pending
- Result: PENDING

## Adapter claims under test

| Claim | Observed | Result |
|---|---|---|
| Net init, resolve, connect, passive data, link query | pending | PENDING |
| No-network start stays OFF with a clear error | pending | PENDING |
| Free space on ux0: (and ur0:/uma0: if mounted) | pending | PENDING |
| Overwrite leaves the new file and no sidecar | pending | PENDING |
| IME opens and cancel keeps the value/focus | pending | PENDING |
| Session log contains no password | pending | PENDING |
| Listing memory footprint (2000 rows, 3 generations, ~3.4 MB) | pending | PENDING |
| Reply-line cap and mtime unit | pending | PENDING |

## Hardware findings from the first launch

The user reported these before any matrix row completed:

1. The local pane showed folders in `ux0:`, but entering a folder showed an
   empty list, and going back showed the volume empty as well.
   - b17 added an exact listing failure message, reported a mid-listing read
     error, and started at a virtual `ALL DRIVES` root. b18 showed the volumes
     and folder contents, so the remaining empty state was intermittent.
   - Root cause found in the host integration test: the app could apply a
     published listing before draining its `LISTING_BEGIN`; the later begin then
     cleared the rows. b19 applies the published generation while draining the
     end event, between begin and end (D55). Evidence: `test_integration` enters
     `ux0:`, enters `ux0:data`, and completes delete, new folder, and rename.
   - The b18 session log is still needed to confirm the exact ordering on
     hardware.
2. `FTP.GNU.ORG` failed instantly with `LOGIN FAILED - CONTROL CONNECTION LOST`.
   - b17 passes a resolver parameter block instead of `NULL`, matching the
     known-working `vita-cap-test` path, and logs `connect_fail` with the
     stage and cause. b18 writes the full UI/command/list/error trail to
     `session.log`. Evidence pending.
3. `START SERVER` stayed at `STARTING` with no IP.
   - Root cause confirmed in code: `ftp_server_start()` emits `BINDING` then
     `ON`, and the worker emitted a second `STARTING` after it returned. The
     second event overwrote `ON` and cleared the address. Fixed in b17 by
     removing the duplicate event. Evidence pending.

User-directed input and layout changes in b17:

- The local pane always starts at a virtual `ALL DRIVES` root listing `ux0:`,
  `ur0:`, and `uma0:` when mounted. Entering a volume shows its root; Circle
  from a volume root returns to `ALL DRIVES`.
- Triangle opens ACTIONS; Select opens SITES (swapped from the settled design).
  The workspace legend matches.

## Defects found

The chronological index, including each fix's hardware status and the build it
landed in, is [`FIXES.md`](FIXES.md). This table keeps the detailed test
evidence.

| Unit | Symptom | Fix | Host test | Re-run result |
|---|---|---|---|---|
| build/assets | b15 install failed at 99% with `0x8010113D` | Convert the three sce_sys PNGs to indexed 8-bit (color type 3); add `tools/check_assets.sh` to the host gate and `build.sh` | asset guard (`tools/check_assets.sh`), run by `tools/test/run.sh` and `build.sh` | b16 installed successfully |
| server/worker | START SERVER stayed at STARTING with no IP | Removed the worker's duplicate STARTING event that overwrote the server's ON event | server/worker host suites still pass; hardware pending | b17 built |
| net | DNS resolver created with a NULL parameter block | Pass a zeroed `SceNetResolverParam`; add a `gethostbyname` fallback | client suite still passes; hardware pending | b17 built |
| worker/ui | A local listing failure was published as an empty list with no message | Report the exact `FsResult`; log `list begin/open/end`; draw `LISTING FAILED` and the reason | `test_integration`, `test_uistate` pass | b17 built |
| ui/worker | Local pane needed a virtual all-drives root; Triangle/Select were the old mapping | Added `UI_ROW_VOLUME`, `worker_list_root`, root-aware parent, and swapped the two workspace keys | `test_uistate`, `test_integration`, render suites pass | b18 |
| logging | The session log missed UI, commands, listings, and errors | Added always-on structured logging, a UI log ring, 256 KiB × 3 generations, and D54 | `test_logev`, `test_handoff`, `test_integration`; password sentinel scan | b18 built |
| ui/actions | At `ux0:` every selected child was locked as `VOLUME ROOT`, so copy, move, rename, and delete were unavailable | Classify the selected source, not the pane path; keep only new folder tied to the virtual root | `test_local_actions_inside_volume_root` | b19 built |
| ui/actions | A run stayed on ACTIONS, which has no status line, so success and failure looked the same | Return to WORKSPACE after a run or confirmation; show `state->status` in the idle and actions docks | `test_action_returns_to_workspace` | b19 built |
| app/listings | A published listing could be applied before its begin event; the begin then cleared the rows and showed EMPTY | Apply the generation while draining the listing-end event, between begin and end (D55) | `test_local_file_actions` | b19 built |
| ime | `open_ime` reached the adapter, but every `begin` logged `unavailable`; new folder and rename could not proceed | Followed the SDK IME sample: `sceAppUtilInit` before the common-dialog config, concrete language/enter-button values, `supportedLanguages` + `languagesForced`, `vita2d_common_dialog_update` every frame, a short NONE grace, and the raw platform code in `last_error` | fake IME path, `test_integration` still passes; hardware pending | b20 built |
| client/log | `connect_fail` recorded only `stage=connect cls=network`, not why TCP failed | Added `FtpClientEvent.net`, set on resolve/connect failure, and logged it as `value=` | `test_connect_refused` asserts `NET_REFUSED` propagation | b20 built |
| server/log | `server_start` recorded no address or port, so the next run cannot compare the panel with the network | Log the LAN address and control port at start | server/worker suites pass | b20 built |
| worker/fsop | Rename reached the executor and the worker died with `0x30004`; `fsop_run` reserved 19,540 bytes on a 19,756-byte pthread stack, with the 8 KiB copy buffer inlined even for a rename | 128 KiB worker stack with fallback; `run_copy_file` noinline; 16-bit rollback indices; b21 `fsop_run` frame 5,476 bytes | `test_fsop_run`, `test_integration` pass | b21: rename completed on hardware; no worker crash |
| ui/actions | With the cursor on `..` (or an empty folder), copy/move/upload/rename/delete silently targeted the current folder from inside it | `UiActions.has_selection`; no selection disables every action except NEW FOLDER; the ledger/target/dock say `NO SELECTION`; a guard refuses a manually enabled action | `test_actions_no_selection`, `test_local_new_folder_without_selection` | b22 built |
| net/link | SERVER advertised `192.29.168.192` for a console at `192.168.29.192`; PC `nc` and FileZilla timed out | `inet_pton` yields network order; convert with `ntohl` before storing in `NetAddress.addr` | `test_quit_with_server` asserts the displayed `192.168.29.192` | b23 built |
| worker/fs | Free space ran `sceIoDevctl(0x3001)` on every subfolder, including `ur0:`/`uma0:`, before a memory-card warning | Cache free space per volume; query once at the volume root and only on `ux0:`; other volumes render `--` | host suites; fake free-space path unchanged | b23 built |
| net/server | 1 MiB net pool and backlog 4; VitaShell's ftpvitalib uses 4 MiB and 128 | Match the working library's pool and backlog | server/worker suites pass | b23 built |
| ui/actions | COPY/MOVE always targeted the current folder, so MOVE could not work and COPY offered to overwrite its own source | Clipboard + PASTE: COPY/MOVE freeze marks, the user navigates, PASTE runs into the destination; CLEAR cancels. Same-folder MOVE disabled | `test_clipboard_copy`, `test_clipboard_move` | b24 built |
| ui/delete | Deleted marks stayed in the pane, so the next DELETE confirmation listed already-deleted files | `prune_marks` drops a mark whose folder is being listed and whose row is gone; marks in other folders survive | `test_delete_confirm_fresh`, `test_marks_pruned_after_delete` | b24 built |
| ui/delete | The confirmation showed bare names, ambiguous when marks came from different folders | The confirmation uses full source paths in `UiConfirm.local_paths` | `test_delete_confirm_fresh`, `test_delete_high_risk_and_protected` | b24 built |
| ui/delete | A high-risk delete skipped the item list and showed only one risk path | One DELETE confirmation carries the full list, `high_risk`, and the risk path; DELETE choice sets `value=1` for the worker | `test_delete_high_risk_and_protected` | b24 built |
| ui/dock | A local delete rendered as MOVE | Added `XFER_DIR_LOCAL_DELETE`; the dock says DELETE | existing host suites | b24 built |
| ui/confirm | A worker confirmation arriving while a local confirm or IME was open was taken and dropped as `CONFIRM BUSY`; the worker stayed pending and later actions froze | Leave the handoff confirm latched until the screen is free and the IME is inactive; then feed it | host suites; the app-level deferral needs hardware | b25 built |
| client/connect | ftp.gnu.org failed at `stage=connect` after ~3.4 s; no resolved IP, raw result, or connecting state | 12 s connect / 5 s greeting / 25 s session; log the resolved IPv4 and `NetResult`; resolver-zero fallback; `CONNECTING` UI | `test_client` timeout tests, `test_integration` resolved-IP log | b26–b28 |
| client/pasv | Localhost control/login worked, then `stage=data cls=server`: the server's PASV LAN address differed from the client's `127.0.0.1` control peer and was refused | Ignore the PASV host; connect to the control peer with the reply port; first-listing 10 s, command 10 s, data idle 20 s, session 35 s; log data-connect `NetResult` | `test_reply` PASV peer/port, `test_client` `test_pasv_nat` | b28 built |
| remote/read | Opening a remote folder stayed on `READING`; status `LIST FAILED` | `OPERATION_DONE` fired while the client slot was held, so the immediate list returned `BUSY`; no end cleared `loading` | Defer refresh to the next worker step; clear `loading` on a remote error; no row reads while loading | `test_remote_navigation`, `test_remote_error_clears_reading` | b32 built |
| remote/actions | NEW FOLDER/RENAME/DELETE failed or were refused `INVALID PATH` | Remote actions went to the local `FsPort` planner; remote paths have no volume token | Route through `ftp_client_mkdir/rename/delete/rmdir`; RMD for a directory; skip the local protection table for remote | `test_remote_mutations`, `test_remote_delete_confirm_and_type` | b32 built |
| remote/disconnect | No disconnect; another site said `CONNECT REFUSED` | No action/command; `ftp_client_connect` refuses a `READY` session | DISCONNECT action; connect tears down first; clear the pane and site | `test_remote_disconnect_action`, `test_remote_disconnect_and_reconnect` | b32 built |
| remote/liveness | Header stayed `CONNECTED` after the server died | No idle probe; a half-open control channel produces no FIN | 20 s `NOOP`, 5 s reply deadline; clear site identity on loss | `test_keepalive`, `test_keepalive_timeout`, `test_idle_control_drop` | b32 built |
| remote/marks | Remote marks never pruned after a listing | `prune_marks` used the local `path_parent` on `/`-rooted paths | Remote parent helper; case-sensitive remote compare | `test_remote_marks_pruned` | b32 built |
| remote/multi | Multi-mark DOWNLOAD/UPLOAD used only the first mark | Marks expanded only for DELETE | Expand marks for DELETE, DOWNLOAD, UPLOAD | `test_remote_multi_download` | b32 built |
| ui/error-pane | A pane-less error could write `panes[-1]` | Only the upper bound was checked; the command-queue error passes `-1` | `pane` is an `int`; both bounds checked | host suites | b32 built |
| remote/refresh | `UI_CMD_REFRESH` had no action | No UI entry | REFRESH in the remote ledger; emits the existing refresh command | `test_remote_refresh`, `test_remote_refresh_action` | b36 built |
| remote/names | A remote name with `/` or `:` was flattened into a local name | No safety flag; local `last_name` splitter | Unsafe row flag, risk diamond, `UNSAFE NAME` dim/refusal; worker backstop | `test_remote_unsafe_name`, `test_remote_unsafe_and_folder` | b36 built |
| transfer/total | A download total was unknown until completion | Size never reached the transfer | Size on command → item → client; dock total from the listing | `test_client`, `test_cancel` | b36 built |
| transfer/upload | Uploads overwrote remote files silently | No remote existence check | MLST preflight + conflict prompt; CANCEL clears the batch | `test_mlst`, `test_upload_conflict` | b36 built |
| transfer/folder | A folder was queued as one RETR/STOR | R22/R23 deferred | `FOLDER NOT SUPPORTED` in the UI and worker | `test_marks`, `test_remote_unsafe_and_folder` | b36 built (deferred R22/R23) |
| client/listing-verb | A listing after a transfer sent the previous data verb | `data_kind` was not reset when a listing began | `client_begin_listing` sets `CDATA_LIST` | `test_client` upload-then-list | b36 built |
| worker/stack | A 64-item SKIP batch could overflow the worker stack | `pump_queue` recursed once per released item | Iterate instead of recursing | host suites, `test_upload_conflict` | b37 HW (operator report) |
| worker/session | OVERWRITE consent survived DISCONNECT/CONNECT | Teardown did not clear the conflict latch | `reset_remote_session` + `batch_reset` | `test_upload_conflict` CANCEL; cross-session untested | b37 HW (operator report) |
| listing/cap | A 2000+ entry folder was reported failed and wiped | A cap stop left `next` at `FS_OK` | Cap stop reports `ok=1, truncated=1` | `test_listing_cap` | b37 HW (operator report) |
| listing/flag | The truncation badge belonged to the previous listing | Flag stored after `apply_listing` | Store before applying rows | `test_listing_cap` | b37 HW (operator report) |
| actions/download | DOWNLOAD did nothing at ALL DRIVES | The ledger joined `*` with the name | Skip the pre-check for `*`/empty | `test_remote_download_from_all_drives` | b37 HW (operator report) |
| confirm/risk | A high-risk delete lost the boot warning | Only the plain delete warning was drawn | Full boot warning for high-risk deletes | `test_high_risk_delete_warning`, render | b37 HW (operator report) |
| client/mlst | A failed MLST write left the upload check latched | `client_send` failure ignored | Return `NOT_CONNECTED` | `test_mlst_send_failure` | b37 HW (operator report) |
| remote/root | The client could not open its own server's `UX0:`/`UR0:`/`UMA0:` roots | The unsafe-name guard refused Cross on every `:` row, including directories | Unsafe blocks files only; a directory still opens | `test_remote_volume_root_open` (UI + integration) | b38 built |
| ui/action-error | A refused action looked like nothing happened | The pane rendered its error only when empty; refusals set only the dock status | Error in the pane band; clears on the next key or a good listing | `test_pane_error`, `test_remote_unsafe_and_folder` | b39 built |

## File-manager slice (b19–b25)

Host evidence, driven through the real input reducer, worker, queue, planner,
executor, and fake filesystem:

- `make_app` starts at `ALL DRIVES`; `ux0:` opens from the volume row.
- `ux0:data` opens with `..`, the app data folder, and the seeded file.
- Delete goes through ACTIONS → DELETE → CONFIRM and removes the file.
- New folder goes through the IME and creates `ux0:data/NEWDIR`.
- Rename goes through the IME and turns `NEWDIR` into `RENAMED`.
- The screen returns to WORKSPACE after every run; `state->status` is the
  dock's third line, so a refusal or failure is visible.

Hardware pass steps:

1. Install `build/vitaftpshell-v01.00-b39.vpk`.
2. Launch; confirm `ALL DRIVES` lists `ux0:`, `ur0:`, and `uma0:` when mounted.
3. Enter `ux0:`, then `ux0:data`. Confirm the rows and that Circle returns to
   `ALL DRIVES` from the volume root.
4. Create a folder, rename it, and delete it. Confirm each result in the dock
   and that a failed action shows its reason in yellow. If the IME still fails,
   the log now carries the raw Sce code as `ime ... value=0x...`.
5. Mark two files. Triangle → COPY; the dock shows `COPY 2 - TRIANGLE TO
   PASTE`. Navigate to another folder; Triangle → PASTE; both copies appear
   there and the source still exists. Repeat with MOVE; the source disappears.
   A MOVE pasted in the same folder must be dim (`ALREADY HERE`).
6. Delete a marked file, then open DELETE for a different file. The confirmation
   must show the new file's full path, not the deleted one. A delete inside
   `ur0:tai` must show the full path and the risk path in one dialog; cancelling
   leaves the file.
7. Enter an empty folder. With the cursor on `..`, Triangle must show
   `NO SELECTION`; only NEW FOLDER may run, and it must create inside that
   folder.
8. Start the server. The panel address must match the Vita's WiFi address
   (b23 fixes the octet swap; b22 showed `192.29.168.192` for
   `192.168.29.192`). From the PC run `nc -v <vita-ip> 1337`. A
   `220 vitaFTPShell ready.` banner proves the listener; a refusal or timeout
   is a network/address result, not an FTP one.
9. Retry the default site and record `connect_fail ... value=...`.
10. Pull the logs with `tools/logs/pull.sh <vita-ip>` and record the exact lines
   in this file.

The server and client remain blocked on the same run: the server reached
`STARTED` in b18 without accepting a LAN client, and the default site failed
immediately. Their logs are still required before those defects can be
classified.

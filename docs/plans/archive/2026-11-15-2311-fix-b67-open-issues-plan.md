---
title: vitaFTPShell b67 Open Issues - Plan
type: fix
date: 2026-11-15
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-plan-bootstrap
execution: code
---

# vitaFTPShell b67 Open Issues - Plan

## Goal Capsule

- **Objective:** Close the review items that can still lose a file, mislabel a transfer, or read a released listing buffer, so the next console run measures the product rather than a known open defect.
- **Means:** Four bounded fixes — a transfer-time size preflight, the sidecar rename denial, the listing-lifetime hold, and the two missing host tests — plus a decision on the per-PASV data-port identity.
- **Authority:** `AGENTS.md` and `docs/DECISIONS.md` govern; `docs/FIXES.md` is the open-issue record; `docs/DESIGN.md` governs words; D86-D90 stay settled.
- **Stop conditions:** every requirement is met or recorded as a decided residual; the host suite, sanitizer, and stack gate are green; one uniquely named VPK exists; `FIXES.md` no longer lists a settled item as open.
- **Who finishes:** the implementer lands U1-U5 and builds b67; the operator runs the M1-M13 matrix as the next job. The project stays `building`.

---

## Product Contract

### Summary

Finish the review remediation's remaining code risks: make the kept-download size describe the transfer rather than a stale listing, refuse sidecar names as rename destinations, keep the UI's listing rows alive while they are on screen, and prove the two test scenarios the plan claimed. The per-PASV data-port identity is evaluated against D59 and either built or recorded with evidence.

### Problem Frame

b66 fixed the review's P1s, but four items still sit between the code and a trustworthy console run. A download whose size changed after the listing is deleted as a partial when the final 226 is lost — the one unremediated data-loss path. A `.vftp.part`/`.vftp.old` name is refused for write and delete but accepted as an RNTO destination, so the denial set has a hole. The UI releases its held listing slot at the end of every frame while it keeps rendering the same row pointer, so the worker can reuse that slot before the next listing arrives. And two plan-mandated tests (owner RETR metadata, the two-item queue terminal) are still missing, so a regression in either would pass the suite.

### Requirements

**Data integrity**

- R1. A download's expected size comes from a SIZE/MLST query for that transfer, falling back to the listing hint only when the server cannot report a size.
- R2. A sidecar name (`.vftp.part`, `.vftp.old`) is refused as a rename destination, matching the write and delete denial.

**Listing lifetime**

- R3. The UI keeps its current listing slot held while its rows are on screen; the worker never reuses a slot the UI still renders, and a newer listing is adopted without a torn read.

**Server identity**

- R4. The data-connection accept cannot hand the armed slot to a different session on the same host, or the limitation is recorded with the evidence that a fixed port cannot distinguish them.

**Evidence**

- R5. A host test drives the owner's own RETR metadata read with a busy guard, and a host test drives a two-item queue whose first item loses the final reply and whose terminal names the cancelled remainder.
- R6. The open-issue record in `docs/FIXES.md` matches the shipped state: no settled item listed as open, no stale build or matrix line.

### Scope Boundaries

**In scope:** R1-R6.

**Deferred to Follow-Up Work**

- A per-PASV ephemeral data listener, if U4's evaluation shows the fixed port cannot be identity-checked without breaking D59/D63.
- The remaining b63 residuals not selected here: FEAT's `perm` fact, the stale PASV address after a DHCP change, the second-cancel overwrite, and the cancel-during-wait wording.
- The console matrix (the next job).

**Considered and not built**

- Comparing the data connection's source port to the control port: FTP data connections use an ephemeral source port, so this would reject every real client.

**Outside this product's identity:** unchanged (no secure transport, daemon, boot component, or PC-first workflow).

---

## Planning Contract

### Key Technical Decisions

- KTD1. **The size preflight runs inside `ftp_client_download`, before PASV.** The client already owns the transfer state; a worker-side stat would add a second operation and a queue state. A server that answers 500/502/550 leaves the listing hint in place, preserving today's behavior for servers without SIZE.
- KTD2. **A sidecar rename destination is denied in `path_check_rename`.** The denial belongs with the other path rules, not in the RNTO handler, so every caller gets it.
- KTD3. **The UI holds its listing slot until a newer generation is adopted.** The worker allocator already skips held slots; the missing piece is the app holding across frames instead of releasing after each draw. A new handoff call adopts the newest published generation only when it is newer, releasing the old slot in the same step.
- KTD4. **The per-PASV listener is evaluated, not assumed.** The fixed control+1 port (D59) is a settled product choice; changing it needs NetPort support for an ephemeral port and a reply that carries it. If that is not available or would break the settled client PASV behavior, record the same-host limitation with its evidence.

### Risks and Mitigations

| Risk | Mitigation |
|---|---|
| The SIZE preflight adds a round trip and rewrites every download test | The fake peer answers 502 by default so existing tests keep the hint; one new test sets a size |
| Holding a listing slot starves the worker's three slots | One held slot per pane leaves two for the writer; a test publishes three generations while holding |
| A per-PASV port breaks FileZilla or the app's own client | U4 is a decision gate; the fixed port stays unless an ephemeral port is proven with the fake and the real client path |

---

## Implementation Units

### U1. Query the size before a download

- **Goal:** A download's kept-file decision uses a size measured for this transfer, not the listing hint alone.
- **Requirements:** R1.
- **Files:** `src/core/ftp/client.c`, `src/core/ftp/client.h`, `tools/test/test_client.c`, `tools/test/test_integration.c`, `tools/fake/fake_ftpd.h`, `tools/fake/fake_ftpd.c`.
- **Approach:** Add a step before PASV that sends `SIZE <remote path>`; a 213 sets `total`/`total_known`, and any 4xx/5xx reply falls back to the listing hint and proceeds. The temp is already open, so a failure to query does not fail the transfer. The fake peer gains a size mode (default "unsupported" keeps existing tests unchanged).
- **Patterns to follow:** the existing MLST/STAT reply parsing and the command-timeout handling.
- **Test scenarios:**
  - A listing hint of 100, SIZE 213 = 5, five bytes delivered, final reply withheld: the file is kept and the terminal total is 5.
  - A listing hint of 5, SIZE 502, five bytes delivered, final reply withheld: the hint is used and the file is kept.
  - A SIZE 550 for a missing file: the transfer proceeds with the hint, as today.
- **Verification:** the kept decision follows the preflight size; every existing download test still passes with the 502 default.

### U2. Refuse a sidecar rename destination

- **Goal:** `.vftp.part` and `.vftp.old` cannot be created by a rename, matching the write and delete denial.
- **Requirements:** R2.
- **Files:** `src/core/path.c`, `tools/test/test_path.c`, `src/core/ftp/server.c`, `tools/test/test_server.c`.
- **Approach:** In `path_check_rename`, reject a destination whose name is a sidecar before the protection table; keep the source check as is. Add the server-level RNFR/RNTO case.
- **Patterns to follow:** `path_check_write`'s `path_is_sidecar` refusal and its test.
- **Test scenarios:**
  - RNTO `ux0:data/x.vftp.part` answers 550 and changes nothing.
  - RNTO `ux0:data/x.vftp.old` answers 550.
  - A normal RNTO still answers 250.
- **Verification:** the denial is in the shared path rule, not only in the FTP handler.

### U3. Hold the listing the UI is showing

- **Goal:** The worker cannot reuse a listing slot while the UI still renders its rows; a newer listing is adopted without a torn read.
- **Requirements:** R3.
- **Files:** `src/core/handoff.c`, `src/core/handoff.h`, `src/app/app.c`, `tools/test/test_handoff.c`.
- **Approach:** Add a handoff call that, given the pane, adopts the newest published generation when it is newer than the held one: release the held slot, acquire the newest, and report the rows. When nothing newer exists, leave the held slot in place. The app calls it in the drain step and releases all held slots only at destroy. The worker allocator's existing held-slot skip then protects the visible rows.
- **Patterns to follow:** the current `handoff_listing_acquire`/`release` pair and the `app->listing_held` bookkeeping.
- **Test scenarios:**
  - Publish generation 1, hold it, publish generation 2: adoption returns generation 2 and the old slot is released.
  - Hold generation 2, publish 3, 4, 5: the held slot is never chosen by `handoff_listing_begin`, and adoption returns the newest.
  - Nothing newer: adoption returns the held generation and keeps the slot.
- **Verification:** a held slot is never the allocator's choice, and the app's row pointer only changes when a new generation is adopted.

### U4. Decide the data-connection identity

- **Goal:** A data connection from another session on the same host cannot be accepted as the armed session's, or the limitation is recorded with evidence.
- **Requirements:** R4.
- **Dependencies:** none.
- **Files:** `src/core/ftp/server.c`, `src/platform/net.h`, `src/platform/psp2_net.c`, `tools/fake/fake_net.c`, `tools/test/test_server.c`.
- **Approach:** Determine whether `NetPort` can listen on an ephemeral port and report the assigned port in the `227` reply. If yes, arm one listener per PASV, accept one peer on it, and close it; this removes the shared address race without changing the client's control-peer rule (D63). If the port cannot be read or the change would break the fixed-port contract, keep D59 and record the same-host limitation in `FIXES.md` with the reasoning.
- **Execution note:** This unit may end in a recorded decision instead of code. Record the evidence either way.
- **Test scenarios:**
  - With a fake ephemeral listener: two sessions from one address, only the PASV owner's data peer is accepted; the other is refused.
  - With the fixed port retained: a same-address non-owner connection is refused once the owner is known, or the limitation is asserted in the record.
- **Verification:** no data connection can deliver payload to a session that did not arm it, or the accepted residual is named.

### U5. Close the evidence gaps and the record

- **Goal:** The owner RETR metadata and the two-item queue terminal have host tests, and `docs/FIXES.md` no longer lists a settled item as open.
- **Requirements:** R5, R6.
- **Files:** `tools/test/test_server.c`, `tools/test/test_integration.c`, `docs/FIXES.md`, `docs/VERIFICATION.md`.
- **Approach:** Add a server test with a busy guard that asserts the owner's SIZE/MLST of its RETR source answers 213/250 while a second session gets 450. Fix the queue test's mark sequence (the root listing starts on A.BIN; mark A, move, mark B, or connect to a folder with an UP row) and assert `N ITEM(S) CANCELLED` and the reconnect. Move the closed items out of the open lists and record the ones U4 decides to defer.
- **Test scenarios:**
  - Owner RETR: A's SIZE 213, B's SIZE 450, after release A's path is queryable.
  - Two-item queue: first item loses the reply, dock says kept and names one cancelled; after reconnect the next transfer runs.
- **Verification:** the two tests fail against the pre-fix behavior and the open list contains only what is genuinely open.

---

## Verification Contract

| Check | Command | Proves |
|---|---|---|
| Host suites | `./tools/test/run.sh` | the new tests and the full suite |
| Sanitizers | `./tools/test/asan.sh` | every suite memory- and UB-clean |
| Preview | `./tools/preview/run.sh` | 40 states, including the kept dock |
| Build | `./build.sh` | one VPK, zero warnings, stack gate green |
| Docs | the link/fence check in `AGENTS.md` | the record is coherent |

## Definition of Done

- R1-R6 are met, or U4's outcome and any deferred residual are recorded with evidence.
- The two new tests fail against the pre-fix code where they describe a defect.
- `./tools/test/run.sh`, `./tools/test/asan.sh`, `./tools/preview/run.sh`, and `./build.sh` pass with no warnings.
- `docs/FIXES.md` lists only genuinely open items; `docs/VERIFICATION.md` carries the host run and leaves the console rows pending.
- One uniquely named VPK exists; `BUILD_NUMBER` matches it.
- The project stays `building`; the console matrix is the next job.

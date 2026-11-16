---
title: vitaFTPShell Documentation Truth Sweep - Plan
type: docs
date: 2026-11-16
topic: docs-truth-sweep
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-brainstorm
execution: code
---

# vitaFTPShell Documentation Truth Sweep - Plan

## Goal Capsule

- **Objective:** A reader who opens the project finds one current statement per fact, a small active set that points at the owners of status, evidence, and decisions, and a mechanical check that fails when a summary drifts from its source. The documented behavior matches the build that just landed.
- **Means:** An ownership table, an active/archive split, a truth pass over every stale claim, a three-feature traceability table, and a documentation gate that lands first with a shrinking baseline.
- **Product authority:** `STRATEGY.md`, the v1 plan, and `docs/DECISIONS.md`. The v1 plan stays the behavior source; this plan changes no application behavior.
- **Open blockers:** None. Retry, remote recursion, and the ambition are decided below.
- **Execution profile:** documentation plus one checked Python tool and its CI wiring. No `src/` change.
- **Stop conditions:** a decision is deleted rather than superseded; layout geometry changes; `verified` is claimed without a console run; a volatile fact is copied into a second document; the file-level upload statement DR15 requires is lost.

## Product Contract

### Summary

One sweep collapses the documentation set into a small hierarchy with one owner per fact, archives completed plans and rejected design directions, corrects every known contradiction including the per-session port change that just landed, and adds a documentation gate to the host suite and CI. The gate lands with an explicit baseline of today's violations and refuses anything new; the sweep shrinks that baseline to empty, so the tree is never red and completeness is measurable.

### Problem Frame

`README.md` says the app is pre-alpha and that the VPK only launches and exits. `AGENTS.md` and `docs/PROJECT.md` each carry a build-by-build narrative pinned to a different build, so every build leaves at least two of them false. Their reference counts disagree with the v1 plan: `AGENTS.md` says `R1-R73`, `KTD1-KTD18`, `U1-U17`, while the plan holds R1–R74, KTD1–KTD25, and U1–U28.

Scope statements contradict the settled product. `README.md`, `docs/PROJECT.md`, and `PROJECTS.md` still list multiple server clients and multiple active transfers among the non-goals, though D86 accepts four clients and D72 runs two lanes, and the v1 plan still carries a deferred bullet for multiple simultaneous server clients. `AGENTS.md` states recursive folder operations as v1 without qualifying that remote recursion is the deferred half.

The code that just landed moved the server to one data port per session slot, and the documents did not move with it. D39's rule, D59's and D63's consequence text, `ARCHITECTURE.md`'s two server paragraphs, `DESIGN.md`'s port sentence, KTD6, U7, R49, R50, `README.md`, and `PROJECTS.md` all still describe one fixed `control+1` port and a 1024–65534 editor bound. `FIXES.md` still records the same-host data-port limitation as accepted.

Two promises have no code behind them. `DESIGN.md` and D35 offer `X TO RETRY`, and no retry command exists. Remote recursive transfer appears as v1 in the agent contract while the v1 plan defers it.

Nothing checks any of this. The only link and fence check lives in the workspace-root instructions and runs by hand; `tools/test/run.sh` and CI check layering, tests, previews, and assets, never prose.

The set also mixes finished work with live work. Three completed remediation plans are not archived, 68 design renders sit beside the settled sheets, and `docs/design/README.md` still marks five directions "awaiting choice" and a sixth "awaiting approval" although every direction was chosen.

### Key Decisions

- **One fact, one home.** Each kind of fact has exactly one owner document; every other document links to it. Governs DR1, DR4, DR5.
- **Status is not history.** Summaries carry a short current state and links; `PROJECTS.md` owns the board, `docs/VERIFICATION.md` the hardware evidence, `docs/FIXES.md` the defects, and git the rest. Governs DR4, DR5.
- **Archive, do not rewrite history.** Completed plans and rejected design directions move to `docs/plans/archive/` and `docs/design/archive/` with index rows; only an exact duplicate with no unique rationale and no inbound link is deleted. Governs DR2, DR3.
- **The gate lands first, with a shrinking baseline.** `tools/check_docs.py` enters the host suite and CI with a baseline file listing today's violations, refuses anything new, and the sweep ends with the baseline empty and removed. (session-settled: user-approved — chosen over writing the docs first and adding the gate last: the sweep's own edits are checked while they happen, and progress becomes a count instead of a judgment.) Governs DR13.
- **The one-press retry promise is retired for v1.** A failed item is retried by running it again, and `X TO RETRY` leaves `docs/DESIGN.md` and every other live document. (session-settled: user-directed — chosen over keeping the promise and planning the behavior separately: no document may describe an action the build lacks.) Governs DR9.
- **Remote recursive transfer is a v1 requirement that is planned, not built.** The v1 plan's R22/R23 become active requirements, and the one status owner records them as planned. (session-settled: user-directed — chosen over keeping the deferral: the owner wants remote recursion in v1, with the implementation planned separately.) Governs DR8.
- **The ambition is stated with measurable proxies.** `STRATEGY.md` gains a short Ambition subsection using signals the project already records, and claims nothing as achieved. (session-settled: user-directed — chosen over withdrawing the wording: the goal is real and only the document was missing.) Governs DR12.
- **This sweep executes the residual plan's record unit.** The port record that `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md` leaves in its U5 lands here, so exactly one plan writes the superseding decision, the port text, and the summary corrections; that plan keeps its console-matrix rows. (session-settled: user-directed — chosen over land U5 against the moving documentation surface: one sweep, one writer.) Governs DR10, DR11.
- **One sweep supersedes the 0826 consolidation plan.** That plan is the primary input to this one and is archived on completion, so exactly one documentation plan is active. (session-settled: user-directed — chosen over amending it in place: it belongs to another session and its own open decisions are settled here.) Governs DR2.

### Actors

- A1. The project owner reading status before a build or a release.
- A2. An agent reading `AGENTS.md` and the read order before changing code.
- A3. A future maintainer returning to a decision.

### Requirements

Requirements use `DR` so they cannot be confused with the product requirements `R<n>` in the v1 plan.

**Ownership and archive**

- DR1. `docs/README.md` carries the ownership table, every markdown document in the repository appears in it as an owner or under one, and each fact has exactly one owner. The design renders are the one exclusion: `docs/design/README.md` owns them as a family.
- DR2. Every file under `docs/plans/` and `docs/design/` is either active and indexed or archived and indexed; no orphan remains, and the 0826 plan is one of the archived plans.
- DR3. An archived plan or design carries one index row naming its outcome and where its decisions, fixes, or evidence live; only an exact duplicate with no unique rationale and no inbound link is deleted.

**Status and truth**

- DR4. `README.md`, `AGENTS.md`, and `docs/PROJECT.md` each carry the state paragraph as a marked block between `<!-- status:begin -->` and `<!-- status:end -->`; the prose inside is identical in all three, only the two relative link targets differ, and no block carries a build number, SHA, test count, or build-by-build narrative.
- DR5. `PROJECTS.md` keeps the board and trims its log to the current phase, moving unique history to `docs/HISTORY.md` only when unique history survives the trim and dropping anything git or `docs/FIXES.md` already carries.
- DR6. Every contradiction found by the audit is corrected, and a re-grep with the retired-phrase list the gate owns returns no live occurrence.
- DR7. The v1 plan's self-references match its contents: R1–R74, KTD1–KTD25, and U1–U28, with no deferred bullet the plan no longer carries, including the multiple-simultaneous-server-clients bullet that D86 and R50 settle.

**Decisions, ports, and evidence**

- DR8. Remote recursive transfer is stated once as a v1 requirement that is planned and not built, and every deferred statement of it is rewritten: D65's status and scope item, the v1 plan's Key Decision and its deferred-list bullet, and the one status owner. No page implies the transfer works.
- DR9. D35 is superseded and the retry promise is gone wherever a document offers one: the README transfer-dock row, `docs/DESIGN.md`'s dock-failure line and its data-channel-failure state, the v1 plan's data-failure flow step and its deferred-decision row, and the workspace states sheet. Each says a failed item is retried by running it again, and the preview fixture that renders the same state is corrected with them.
- DR10. The new decision entry states the per-session port rule once, and every live statement of the server's ports agrees with it: D39's rule is superseded, D59's, D63's, and D86's port sentences are amended, the bind-failure rule in R49 splits into a control bind that leaves the server OFF and a data bind that answers 425, and each other named document states only what its own subject needs with a reference to that entry. This sweep executes the record unit that `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md` leaves in its U5.
- DR11. `docs/FIXES.md` records the per-session ports as a host-verified row with `PENDING` for the packaged build, since this sweep produces none, no longer lists the same-host data-port limitation as accepted, and restates only the residuals that remain.

**Traceability and ambition**

- DR12. `docs/PROJECT.md` carries a three-row traceability table mapping each primary feature to its requirements, flows, and matrix rows, and `STRATEGY.md` carries the ambition with measurable proxies.

**Drift guard**

- DR13. `tools/check_docs.py` enforces broken markdown links and resolvable backticked paths that begin with `docs/`, unbalanced fences, a state block that differs across the three documents or whose links do not resolve, unindexed plans and designs, dead R/U/KTD/D references, a whole-set range whose endpoint does not match its owner's highest ID, build tokens outside their owners, a retired phrase outside its owner allowlist (`one active transfer`, `multiple server clients`, `pre-alpha`, `TO RETRY`, unqualified `recursive folders`, and the old fixed-port wording), a markdown file missing from the ownership table, and the traceability table's three rows with their R-IDs and M-rows; it runs from `tools/test/run.sh` and CI, and it ends the sweep with an empty, removed baseline. The phrase check reads every markdown file plus the design states sheets, and its allowlist is archived files, dated plan bodies, and the historical rows of `docs/FIXES.md` and `docs/VERIFICATION.md`. A baseline entry keys on file, check, and matched text; entries may only be removed, and a regeneration that would add one fails. Archived files stay subject to the link, fence, and reference checks.
- DR14. The verification record states what the sweep proves and what remains unverified, and no document claims `verified` without a console run.
- DR15. `docs/VERIFICATION.md` records file-level remote upload as working with the build that proves it, and marks external-site upload and remote mutation as pending rather than PASS.

### Key Flows

- F1. Gate-first sweep
  - **Trigger:** The sweep starts.
  - **Steps:** Land the gate with a baseline snapshot; correct decisions; correct the v1 plan; correct the evidence and summaries; archive; build the traceability table; shrink the baseline after each pass; delete it when empty.
  - **Outcome:** The gate is green at every commit, and its baseline count records how much drift is left.
  - **Covers:** DR6, DR13.
- F2. Port record reconciliation
  - **Trigger:** A port statement is found.
  - **Steps:** Add the superseding decision; amend the consequence sentences that name the old port; correct the architecture, design, requirement, and summary text; confirm the search finds only the new rule and the decision history.
  - **Outcome:** One rule, one history, no page left asserting a single port.
  - **Covers:** DR10, DR11.

### Acceptance Examples

- AE1. A reader lands on the front page
  - **Covers:** DR4.
  - **Given:** A reader opens `README.md`.
  - **When:** They look for the project's state.
  - **Then:** They see `building`, the three roles, and links to the board and the evidence page, with no build number and no "pre-alpha".
- AE2. New drift is refused
  - **Covers:** DR13.
  - **Given:** A summary document gains a build number outside its owner, or a whole-set range ends below its owner's highest ID.
  - **When:** `./tools/test/run.sh` runs.
  - **Then:** The gate fails, names the file and line, and the baseline offers no cover.
- AE3. An unindexed plan is refused
  - **Covers:** DR2, DR13.
  - **Given:** A completed plan sits in `docs/plans/` with no index row.
  - **When:** The gate runs.
  - **Then:** It fails and names the file.
- AE4. The baseline reaches zero
  - **Covers:** DR13.
  - **Given:** The sweep's final commit.
  - **When:** The gate runs.
  - **Then:** It passes with no baseline file present, and no allowlisted violation remains.
- AE5. A planned requirement reads as planned
  - **Covers:** DR8.
  - **Given:** A reader looks up remote recursive transfer.
  - **When:** They follow the one status owner.
  - **Then:** They see it named as a v1 requirement that is not built, with a link to R22/R23, and no page says the transfer works.
- AE7. A cited design sheet cannot vanish
  - **Covers:** DR13.
  - **Given:** A decision entry cites a design sheet as a backticked `docs/design/...` path.
  - **When:** The sheet is archived and the path is not rewritten.
  - **Then:** The gate fails and names the citing file and line.
- AE8. The state block cannot diverge
  - **Covers:** DR4, DR13.
  - **Given:** One of the three documents edits its state prose.
  - **When:** The gate runs.
  - **Then:** It fails, because the block's prose no longer matches the other two.
- AE6. The port rule appears once
  - **Covers:** DR10, DR11.
  - **Given:** A maintainer searches the docs for the data port.
  - **When:** They read the results.
  - **Then:** They find the per-session rule in one place, its history in the decision record, and no live statement of a single fixed data port.

### Success Criteria

- The active set is the root four, the `docs/` pages plus `docs/HISTORY.md` if DR5 creates it, the v1 plan and the residual implementation plan, this sweep's own plan archived on completion, the settled design sheet per screen, and the archive indexes.
- The gate passes with no baseline, and the audit's patterns return no live occurrence.
- `README.md`, `AGENTS.md`, and `docs/PROJECT.md` send status readers to the owners and pin no build.
- The port rule and the three decided scope items are consistent across every live document that mentions them.
- `PROJECTS.md` stays `building`; no document claims `verified`.

### Scope Boundaries

- **Deferred for later:** Implementing remote recursive transfer, which gets its own behavior plan.
- **Out of scope:** one-press retry; D35's supersession retires it for v1 and a failed item is retried by running it again.
- **Outside this product's identity:** Any application behavior change, protocol change, layout geometry, new product feature, or rewrite of `FIXES.md`/`VERIFICATION.md` evidence; deleting decision history.
- **Considered and not built:** Generating the index tables from a manifest. It would remove hand-edited index drift, but it adds a second source of truth and a generator to maintain for four small tables the gate can check instead.

### Dependencies / Assumptions

- The landed code is the reference for the port rule and for the four-client, two-lane behavior.
- The 0826 plan is an input, not an approved contract; its stale claims are re-verified rather than trusted.
- The docs gate is Python standard library only, matching `tools/check_worker_stack.py`.
- CI runs `tools/test/run.sh`, so wiring the gate there covers CI without a separate workflow step. `build.sh` does not run that script, so a docs failure blocks CI and the local suite, not a local build.
- The gate verifies structure and known wording, not the truth of prose nobody has written yet. A new scope claim can still appear; the ownership rule and review carry that class, and the gate does not claim otherwise.
- The workspace-root link and fence check keeps its role for the sibling projects; the project gate is authoritative for this repository and runs beside it, with no parent-repository edit.

### Outstanding Questions

None. Both open questions are resolved above, and the gate's limit is recorded under Dependencies.

### Sources / Research

- `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md` — its U5 record unit is executed here; its console-matrix rows stay open.
- `docs/plans/archive/2026-11-16-0826-docs-consolidation-plan.md` — the primary input; its ownership table, correction matrix, and drift-guard checks are carried forward, and its superseded claims are corrected here.
- The documentation audit for this plan: status duplication in `README.md`, `AGENTS.md`, and `docs/PROJECT.md`; reference counts in `AGENTS.md`; the port statements listed under DR10; five "awaiting choice" rows and one "awaiting approval" row in `docs/design/README.md`; the plan, design, retry, and recursion statements mapped file by file.
- `src/core/ftp/server.h` — the landed per-session port contract and `SERVER_PORT_MAX`.
- `tools/test/run.sh`, `.github/workflows/ci.yml`, `tools/check_assets.sh`, and the workspace-root link check — what runs today.
- `docs/FIXES.md` — the same-host data-port residual this sweep retires.

---

## Planning Contract

### Key Technical Decisions

- KTD1. The gate is a standalone stdlib-only Python script invoked from `tools/test/run.sh`, matching `tools/check_worker_stack.py`: findings to stderr, exit 1 for a finding, exit 2 for usage, and one `docs guard: OK` line on success. CI already runs that script as one step, so no workflow change is needed.
- KTD2. A baseline entry keys on file, check name, and matched text, never on a line number. Entries may only be removed; a regeneration that would add one fails, which stops the sweep from re-baselining drift it introduced.
- KTD3. Reference resolution has two forms: markdown links, and backticked paths that begin with `docs/`. External workspace citations (`vita-cockpit/…`, `agent-wiki/…`) and URLs are out of scope, so the check does not report them.
- KTD4. The status block is fixed prose between `<!-- status:begin -->` and `<!-- status:end -->`, identical across the three documents except for the two relative link targets. The gate compares the prose and resolves both links.
- KTD5. Recursion and retry are resolved in `docs/DECISIONS.md` before any summary is rewritten, because every summary cites a decision and the evidence page cites the requirement. Decisions first is what keeps the sweep from inventing a fact twice.
- KTD6. Archive moves use `git mv`, and inbound references are rewritten in the same unit as the move, because a moved file with a stale inbound link is the exact failure the gate reports.

### Assumptions

- The v1 plan's external citations are not files in this repository and stay unchecked.
- The design renders do not link to each other or to code; every edge is in markdown, so a text-level rewrite covers the moves.
- `tools/test/run.sh` remains the single CI entry point; `build.sh` keeps its own checks.

### Sequencing

U1 first, so every later unit is measured. U2 next, because decisions are what the summaries cite. U3 and U4 follow in that order, since archive moves rewrite links the summaries carry. U5 lands last and records the evidence and the traceability table.

---

## Implementation Units

### U1. Documentation gate with a shrinking baseline

- Goal: A checked documentation gate runs beside the existing guards, fails on the classes the sweep must not reintroduce, and starts from an explicit baseline of today's violations.
- Requirements: DR13; KTD1, KTD2, KTD3, KTD4. Covers AE2, AE3, AE7, AE8.
- Dependencies: none.
- Files: `tools/check_docs.py`, `tools/test/run.sh`, `docs/drift-baseline.txt`.
- Approach:
  - Write the script with one function per check: link and backticked-path resolution, fence balance, plans and design inventory, ownership-table completeness, reference existence and range maxima (R74, KTD25, U28, D91 at the time of writing), build tokens outside the owner allowlist, the status block, the traceability table's rows, and the retired-phrase list.
  - Read `docs/drift-baseline.txt` when present; a finding covered by an entry is reported only under `--verbose`, an uncovered finding fails.
  - Add `--self-test`, which builds a temporary fixture tree, asserts each check fails on its own fixture and passes on a clean one, and exits 2 on a missing argument.
  - Invoke it from `tools/test/run.sh` after the asset guard, before the suite loop, so it fails fast.
- Test scenarios:
  - A stale `R1-R73` range fails on the upper-bound check while `R73` alone would pass.
  - A markdown link to a missing file fails, and a backticked `docs/design/...` path to a missing sheet fails with the citing file and line.
  - A markdown file under `docs/` missing from the ownership table fails.
  - A status block edited in one of the three documents fails; an identical block with correct relative links passes.
  - A retired phrase in a summary fails; the same phrase inside a dated plan passes.
  - A baseline entry matching a finding suppresses it; a regeneration that would add an entry exits non-zero.
  - `--self-test` passes on a clean fixture and fails each check's fixture.
- Verification: `python3 tools/check_docs.py --self-test` passes; `./tools/test/run.sh` prints `docs guard: OK` and still runs every suite.

### U2. Decisions and the behavioral record

- Goal: The decisions the sweep needs are recorded before any summary cites them: the per-session port rule, the retry retirement, remote recursion as planned v1, the one-home rule, and the gate.
- Requirements: DR6, DR7, DR8, DR9, DR10, DR11; KTD5.
- Dependencies: U1.
- Files: `docs/DECISIONS.md`, `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`, `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md`, `docs/ARCHITECTURE.md`, `docs/DESIGN.md`.
- Approach:
  - Add D92 stating the per-session port rule and superseding D39's port rule; amend the port sentences in D59, D63, and D86 while leaving their other rules intact.
  - Rewrite D65's status, its item 3, and its consequences so remote recursion is a v1 requirement that is planned, not a deferred refusal.
  - Supersede D35 with the run-it-again disposition, and keep its history.
  - Add decisions for the one-home rule and the documentation gate.
  - Correct the v1 plan in place: KTD6, U7's approach and scenario, the resolved-question row, R49's bind-failure split, R50's port sentence, R22/R23 as active requirements, the Key Decision at line 55, the deferred bullets at 387 and 391, the one-client U7 scenario at line 998, the retry flow step and its deferred-decision row, and the stale risk rows that describe the removed warning and the excluded app folder.
  - Rewrite `docs/ARCHITECTURE.md`'s listener paragraphs and `docs/DESIGN.md`'s port sentence and dock retry lines to the per-session rule and the retirement, each citing D92.
  - Replace the residual plan's U5 with a pointer to D92 and this sweep, so no executor runs the record twice.
- Test scenarios: `Test expectation: none -- record text; the gate and the grep pass in U5 cover it.`
- Verification: D92 exists and supersedes D39; a search finds no live deferred statement for R22/R23; the gate reports the reference maxima as resolved.

### U3. Summaries, status, and the board

- Goal: The three summary documents stop carrying status history, every stale scope and retry line is corrected, and the board keeps the live state with its log trimmed.
- Requirements: DR1, DR4, DR5, DR6, DR7, DR9; KTD4.
- Dependencies: U2.
- Files: `README.md`, `AGENTS.md`, `docs/PROJECT.md`, `PROJECTS.md`, `docs/README.md`, `docs/design/2026-11-08-workspace-a-states.html`, `tools/preview/preview.c`, `docs/HISTORY.md` when DR5 creates it.
- Approach:
  - Replace each status section with the marked block and two links; delete the pre-alpha badge text, the "VPK only launches and exits" note, the suite and state counts, and the build narratives rather than updating them.
  - Correct the scope lines: four server clients, two lanes, local recursion in and remote recursion planned, and no retired phrase.
  - Remove `retry` from the dock description, the workspace states sheet, and the preview fixture that renders the same state, and correct `AGENTS.md`'s read-order counts to R1–R74, KTD1–KTD25, and U1–U28.
  - Trim `PROJECTS.md`'s log to the current phase, move unique history to `docs/HISTORY.md`, and re-sort what remains by date.
  - Update `docs/README.md`'s ownership table and its description of the plans and design folders.
- Test scenarios:
  - The same status block appears in all three documents with resolving links (Covers AE8).
  - A grep for the audit's retired phrases returns no live hit.
  - `README.md` states `building`, three roles, and no build number (Covers AE1).
- Verification: the gate's status-block, phrase, and token checks pass; the summaries contain no build token.

### U4. Archive moves and indexes

- Goal: Completed plans and rejected design directions leave the active set with an index row and intact inbound links.
- Requirements: DR2, DR3; KTD6. Covers AE3, AE7.
- Dependencies: U2, U3.
- Files: `docs/plans/README.md`, `docs/plans/archive/README.md`, `docs/design/README.md`, `docs/design/archive/README.md`, `docs/DESIGN.md`, `docs/DECISIONS.md`, `README.md`, `docs/ARCHITECTURE.md`, `docs/PROJECT.md`, `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`, `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md`.
- Approach:
  - Create both archive folders with an index that gives each file one row: outcome, build, and where its decisions or evidence live.
  - Move the three completed remediation plans and the 0826 consolidation plan with `git mv`; move this sweep's own plan when the sweep is complete.
  - Move the rejected direction renders and superseded one-off renders, and keep the settled sheet per screen.
  - Rewrite the inbound references in the same unit: the plans index rows, the v1 plan's citation of the b50 plan, the residual plan's citation of the b67 plan, the design index rows, the backticked design references in `DESIGN.md` and `DECISIONS.md`, and the README image link.
  - Replace the design index's five `awaiting choice` rows and one `awaiting approval` row with settled wording.
- Test scenarios:
  - An archived plan cited only by a backticked path fails the gate until the path is rewritten (Covers AE7).
  - A new file in `docs/plans/` with no index row fails the gate (Covers AE3).
  - Every moved sheet's inbound link resolves after the move.
- Verification: the gate's inventory and link checks pass with the archive present.

### U5. Traceability, ambition, and evidence

- Goal: The three primary features map to requirements and matrix rows, the ambition is stated with proxies, and the evidence page records what this pass proved.
- Requirements: DR11, DR12, DR13, DR14, DR15.
- Dependencies: U1, U2, U3, U4.
- Files: `docs/PROJECT.md`, `STRATEGY.md`, `docs/VERIFICATION.md`, `docs/FIXES.md`, `PROJECTS.md`, `docs/drift-baseline.txt`.
- Approach:
  - Add the three-row traceability table to `docs/PROJECT.md` and add its presence to the gate's checks.
  - Add the Ambition subsection to `STRATEGY.md` with the proxies the project already records.
  - Record the per-session ports as host-verified with `PENDING` for a packaged build, retire the same-host residual, and record file-level remote upload as working with external rows pending. This sweep produces no build.
  - Before archiving this plan, reword its own mentions of the baseline file and the conditional history page so they are prose rather than paths the gate resolves.
  - Shrink the baseline to empty and delete the file; regenerate it only by removal.
  - Update the board's row and next action, and archive this plan with its index row.
- Test scenarios:
  - `./tools/test/run.sh` passes with no baseline file present (Covers AE4).
  - A traceability row missing its R-IDs or M-rows fails the gate.
  - The evidence page names remote recursion as planned and does not claim `verified`.
- Verification: the gate passes with no baseline, the grep returns no live hit, and `git diff --stat` shows no `src/` change.

---

## Verification Contract

| Gate | Command | Proves |
|---|---|---|
| Gate self-test | `python3 tools/check_docs.py --self-test` | Each check fails on its own fixture and passes on a clean tree |
| Host suite | `./tools/test/run.sh` | The layering guard, the docs guard, and every host suite |
| Audit grep | the retired-phrase list in `tools/check_docs.py` | No live contradiction survives |
| Link and fence | the same gate | No broken markdown link, backticked `docs/` path, or unbalanced fence |
| Scope of change | `git diff --stat` against the sweep's base | No `src/` file changed, and only docs plus the gate |
| Console | none | This pass proves documentation only; `verified` stays out of reach |

## Definition of Done

- Every DR is satisfied, and the gate passes with the baseline deleted.
- The active set is the root four, the docs pages, the v1 plan and the residual implementation plan, the settled sheet per screen, and the archive indexes.
- D92 supersedes D39, D35 and D65 are rewritten, and no live document promises retry or defers remote recursion.
- The three summary documents share one status block and pin no build.
- `PROJECTS.md` stays `building`; no document claims `verified`.
- No `src/` change appears in the diff, and abandoned helper code is removed.

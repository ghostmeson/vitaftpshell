---
title: vitaFTPShell Documentation Consolidation - Plan
type: docs
date: 2026-11-16
topic: docs-consolidation
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-brainstorm
execution: docs
---

# vitaFTPShell Documentation Consolidation - Plan

## Goal Capsule

- **Objective:** Make the documentation set small, layered, truthful, and hard to
  stale: one home per fact, one active plan, one board, one evidence record, and
  a mechanical check that fails when the summaries drift from the sources.
- **Means:** An ownership table, an active/archive inventory, a truth pass over
  the summary docs, a feature-to-requirement-to-matrix traceability table, and a
  documentation gate wired into the existing host-test/CI path.
- **Product authority:** `STRATEGY.md`, the v1 plan, and `docs/DECISIONS.md`.
  The v1 plan remains the behavior source of truth; this plan changes no
  behavior.
- **Open blockers:** Three owner decisions — remote recursive folder transfer
  (R22/R23), one-press retry (D35), and whether the “defacto standard” ambition
  becomes a stated strategy goal. Recommended defaults are in the Open Decisions
  table.
- **Execution profile:** docs. The only code-adjacent change is the documentation
  check script and its CI/test wiring.
- **Stop conditions:** a decision is deleted rather than superseded; layout
  geometry changes; the project claims `verified` without a console run; a
  volatile fact is copied into a second document; the remote-upload correction is
  lost.

---

## Product Contract

### Summary

The v1 plan is current. The documents that summarize it are not. Build-by-build
status was copied into `README.md`, `AGENTS.md`, `docs/PROJECT.md`, and
`PROJECTS.md`, so every build leaves several stale claims behind. Completed
remediation plans are not indexed, rejected design directions sit beside settled
sheets, and there is no traceability from the three primary product features to
requirements and hardware rows.

This plan collapses the set into a small hierarchy, removes or archives what is
no longer active, corrects every known contradiction, and adds a check that makes
the same drift fail before it ships.

### Problem Frame

Known contradictions to remove:

- `README.md` says the app is pre-alpha, has not run on hardware, and that the
  VPK only launches and exits. It also says one client slot, one active slot,
  “retry”, and lists multiple server clients as a non-goal.
- `docs/PROJECT.md` still names b63, R1-R70, client-side recursive folders, one
  active transfer, and multiple server clients as a non-goal.
- `PROJECTS.md` lists multiple server clients and multiple active transfers in
  the not-in-v1 list, and leaves “recursive folders” unqualified.
- `AGENTS.md` says R1-R73 and U1-U17, and says recursive folder operations are
  v1 without the remote deferral.
- The v1 plan still lists multiple simultaneous server clients as deferred even
  though D86 and R50 implement four.
- Three completed remediation plans are unindexed and unreferenced.
- The design folder mixes settled sheets, rejected directions, and “awaiting
  choice” labels.
- No document maps the three primary features to R-IDs and M-rows.
- D35, `DESIGN.md`, and the v1 plan promise `X TO RETRY`, but the code has no
  retry command.
- Remote file upload works when the client is connected on the remote pane. The
  documents must not imply that file-level upload is missing.

### Key Decisions

1. **One fact, one home.** Each kind of fact has exactly one owner document.
   Other documents link to it and never restate it. The ownership table below is
   binding.
2. **Status is not history.** `README.md`, `AGENTS.md`, and `docs/PROJECT.md`
   carry a short current-state paragraph with links. `PROJECTS.md` owns the live
   board, `VERIFICATION.md` owns hardware evidence, `FIXES.md` owns defects, and
   git owns the rest.
3. **Archive, do not rewrite history.** Completed plans move to
   `docs/plans/archive/`; rejected design directions move to
   `docs/design/archive/`. Delete only exact duplicates. Never delete a
   `DECISIONS.md` entry, a `FIXES.md` row, verification evidence, or a settled
   design sheet.
4. **Traceability is a first-class document.** A three-row table maps each
   primary feature to its requirements, its user flows, and its hardware matrix
   rows. It lives in `docs/PROJECT.md` and is checked by the docs gate.
5. **Drift is mechanical, not a review habit.** A documentation check runs in
   `tools/test/run.sh` and CI. It fails on broken links, unindexed plans/designs,
   dead requirement/decision references, and volatile build facts outside their
   owner documents.
6. **Remote file upload is working.** The client uploads files to a connected
   remote server from the remote pane. The open items are recursive folder
   transfer (R22/R23) and external-server verification evidence, not file
   upload.

### Requirements

Requirements are numbered `DC` (documentation consolidation) so they cannot be
confused with the product decisions `D<n>`.

- **DC1. Ownership.** Establish the information architecture and ownership table
  in `docs/README.md`; every active document appears in it.
- **DC2. Active/archive inventory.** Every file under `docs/plans/` and
  `docs/design/` is either active and indexed, or archived and indexed. No
  orphans.
- **DC3. Truth pass.** Correct every contradiction listed in Problem Frame and
  any equivalent claim found by `git grep`.
- **DC4. Traceability.** Add the three-feature table to `docs/PROJECT.md` and
  keep it current.
- **DC5. D35 resolution.** Either implement one-press retry or supersede D35 and
  remove the claim from `DESIGN.md`, the v1 plan, and `README.md`. No document
  may promise a behavior the code does not have.
- **DC6. Remote-upload evidence.** Record the working remote file upload in
  `VERIFICATION.md`; add hardware rows for upload to an external login site and
  for remote rename/delete/mkdir on an external server. Do not claim PASS
  without evidence.
- **DC7. Ambition.** Add the “defacto standard for console file navigation”
  ambition to `STRATEGY.md` with a measurable proxy, or withdraw the wording.
  Do not leave it only in the conversation.
- **DC8. Drift guard.** Add `tools/check_docs.py` with the checks in the Drift
  Guard section; wire it into `tools/test/run.sh` and CI.
- **DC9. Maps and indexes.** Update `docs/README.md`, `docs/plans/README.md`,
  `docs/design/README.md`, and the agent definition of done to use the new
  structure.
- **DC10. Verification.** Run the docs gate, the link/fence check, and the host
  suite; confirm no source behavior changed and no `verified` claim appeared.

### Scope Boundaries

**In scope:** documentation, indexes, archive folders, decisions, evidence rows,
the traceability table, and the docs-check tool.

**Out of scope:** application behavior, protocol changes, layout geometry,
new product features, rewriting `FIXES.md` or `VERIFICATION.md` evidence, and
deleting decision history. If D35 is implemented rather than retired, that
behavior change gets its own plan.

---

## Planning Contract

### Information Architecture

| Fact | Owner document | Everyone else |
|---|---|---|
| Why the product exists, users, boundaries, metrics, ambition | `STRATEGY.md` | Links |
| Behavior requirements, flows, acceptance examples, matrix definition | v1 plan | Links to R/F/AE/M IDs |
| Numbered product and architecture decisions | `docs/DECISIONS.md` | Links to D IDs |
| Scope, non-goals, roadmap, failure policy, feature traceability | `docs/PROJECT.md` | Links |
| Live phase, current build, last job, next action | `PROJECTS.md` | Links |
| Hardware matrix and evidence | `docs/VERIFICATION.md` | Links |
| Defect index with build and HW/HOST status | `docs/FIXES.md` | Links |
| Layout, geometry, input | `docs/DESIGN.md` | Links |
| Layers, ports, threading, module rules | `docs/ARCHITECTURE.md` | Links |
| Agent rules, read order, build/release/safety contract | `AGENTS.md` | Links |
| Public front door, build/install, safety summary | `README.md` | Links |
| Documentation map and ownership | `docs/README.md` | — |
| Active and archived plans | `docs/plans/README.md` | — |
| Active and archived design sheets | `docs/design/README.md` | — |

**Rules:**

- A build number, SHA, hardware status, or test count appears only in
  `PROJECTS.md`, `docs/VERIFICATION.md`, `docs/FIXES.md`, `docs/DECISIONS.md`
  (historical entries), or a dated plan. It never appears in `README.md`,
  `AGENTS.md`, `docs/PROJECT.md`, `docs/ARCHITECTURE.md`, or `docs/DESIGN.md`.
- A requirement is stated once, in the v1 plan. Summaries cite `R<n>`.
- A decision is stated once, in `DECISIONS.md`. Summaries cite `D<n>`.
- A status claim links to `PROJECTS.md` or `VERIFICATION.md`; it is not repeated.

### Active / Archive Inventory

**Plans — active:**

- `docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md` — behavior source of
  truth.
- `docs/plans/2026-11-16-0643-fix-residual-network-closure-plan.md` — current
  implementation plan.
- This plan until it is applied, then archive it.

**Plans — archive after verifying absorption:**

- `2026-11-15-1213-fix-b50-review-remediation-plan.md` — b55 remediation.
- `2026-11-15-2043-fix-review-remediation-plan.md` — b66 review remediation.
- `2026-11-15-2311-fix-b67-open-issues-plan.md` — b66/b68 open issues.

For each: confirm its units are reflected in `DECISIONS.md`, `FIXES.md`, or
`VERIFICATION.md`; add a one-line outcome to the archive index; move it to
`docs/plans/archive/`. If a plan contains unique rationale not captured
elsewhere, move that rationale into a decision first.

**Design — active:**

- The settled state sheets and renders referenced by `DESIGN.md`:
  `workspace-a-*`, `sites-c-*`, `site-editor-a-*`, `server-a-*`, `actions-a-*`,
  `confirm-a-*`, `text-entry*`, and the launcher-icon files.
- One directions sheet per screen while it remains the record of the chosen
  direction.

**Design — archive:**

- Rejected direction renders (`workspace-b/c`, `sites-a/b`, `site-editor-b/c`,
  `server-b/c`, `actions-b/c`, `confirm-b/c`) and superseded one-off renders.
- Fix every “awaiting choice/approval” label in `docs/design/README.md`; the
  choices are settled. Mark the 2026-11-08 SERVER sheets as the historical
  one-client/warning record and point to D86/D88.

**Delete only:** exact duplicate files with no unique rationale and no inbound
link. Git preserves the history. Anything with unique rationale is archived,
not deleted. The active set should be small enough to read in one sitting; the
archive exists for rationale, not for hoarding.

### Workstreams

**1. Settle the open decisions.** Apply the recommended defaults unless the owner
chooses otherwise. Record the outcomes in `DECISIONS.md` before rewriting the
summaries.

**2. Establish the architecture and archive.** Write the ownership table, create
`docs/plans/archive/` and `docs/design/archive/` with indexes, and move or delete
the inactive files. Update `docs/README.md` and the two folder indexes.

**3. Consolidate status and history.** Strip build-by-build narratives from
`README.md`, `AGENTS.md`, and `docs/PROJECT.md`. Keep a three-line state with
links to `PROJECTS.md`/`VERIFICATION.md`. Trim `PROJECTS.md`'s log to the current
phase or the last ten jobs; delete the rest if it duplicates `FIXES.md` and git,
otherwise move unique history to `docs/HISTORY.md`.

**4. Truth pass.** Correct the known claims in the Correction Matrix below and
run `git grep` for the same patterns elsewhere: `one client`, `one active`,
`multiple server clients`, `b63`, `R1-R70`, `R1-R73`, `U1-U17`, `recursive
folders` (unqualified), and `TO RETRY`.

**5. Traceability and evidence.** Add the three-feature table to
`docs/PROJECT.md`; add the external upload and remote-mutation rows to the
verification matrix; record the working remote file upload with its actual
evidence.

**6. Drift guard and verification.** Add `tools/check_docs.py`, wire it into
`tools/test/run.sh` and CI, run every check, and update `PROJECTS.md` with the
docs job and its next action.

### Correction Matrix

| File | Action |
|---|---|
| `README.md` | Rewrite the status paragraph: `building`, three roles, link to `PROJECTS.md` and `VERIFICATION.md`. Delete “pre-alpha”, “has not run on hardware”, and “the current VPK only launches and exits”. Replace one client/one active slot with four clients and the two-lane rule (D72/D86). Remove the “retry” claim unless D35 is implemented. Remove multiple server clients from the non-goals. Keep build/install, safety, and a short doc map; link architecture and behavior instead of duplicating them. |
| `AGENTS.md` | Make it the contract only. Replace the build-by-build status with a short state plus links. Fix `R1-R74`, `U1-U28`, and the requirement/unit references. Qualify recursive folders: local recursive is v1; remote recursive is deferred (R22/R23, D65). Remove duplicated history and volatile counts. |
| `PROJECTS.md` | Keep the board, phases, current state, risks, and next action. Trim the log to the current phase or last ten jobs. Fix the not-in-v1 list: remove multiple server clients; say “more than one network transfer or more than one local job”; qualify recursive folders as local-only. |
| `STRATEGY.md` | Add a short Ambition subsection for “the default console-side file navigation and transfer workspace” with a measurable proxy (external-server matrix pass, transfer completion, crash-free sessions, v1 completeness). Keep the no-parity boundary. |
| `docs/PROJECT.md` | Remove the build log; keep scope, non-goals, roadmap, failure policy. Fix the scope table: remote file upload works; remote recursive folder transfer deferred; local recursive in; four server clients; one network plus one local lane; R1-R74. Fix the non-goals. Add the three-feature traceability table. |
| `docs/ARCHITECTURE.md` | Spot-check for one-client/one-slot claims; it already states four clients and two lanes. Remove any build history; link instead. |
| `docs/DESIGN.md` | Keep layout law. Remove build-number amendments and retry claims if D35 is retired; link D86/D88 instead. No geometry changes. |
| `docs/DECISIONS.md` | Append superseding entries for D35 and any scope change. Add decisions for “one fact, one home” and the docs gate. Never delete D9/D35 history. |
| `docs/VERIFICATION.md` | Add rows for external upload to a login site and remote rename/delete/mkdir on an external server. Record the working remote file upload with its actual build and evidence; mark PART when the scope is app-local only. Keep M1-M13. |
| `docs/README.md` | Update the map and add the ownership table, archive rule, and docs-check command. |
| `docs/plans/README.md` | Index the active plans and the archive; define the archive rule. |
| `docs/plans/archive/README.md` | One line per archived plan: outcome, build, where its decisions/fixes live. |
| `docs/design/README.md` | Mark chosen directions as settled; remove “awaiting choice/approval”; point to the archive for rejected directions; keep the historical one-client/warning note with D86/D88 links. |
| v1 plan | Fix the deferred “multiple simultaneous server clients” bullet; confirm R1-R74/U1-U28 counts; add one line that remote file upload works and remote recursive folders are deferred. Keep it the behavior source. |
| `docs/features/README.md` | Keep as the future-feature contract; no change beyond a link to the new traceability table. |

### Drift Guard

Add `tools/check_docs.py` (Python standard library, no network) and run it from
`tools/test/run.sh` and CI. It must fail on:

1. Broken relative links and unbalanced code fences under the documentation
   tree.
2. A file in `docs/plans/` or `docs/design/` that is neither active-indexed nor
   archived-indexed.
3. A requirement or unit reference that does not resolve in its owning document:
   `R<n>`, `U<n>`, and `KTD<n>` in the summary/root docs resolve against the v1
   plan; `D<n>` resolves against `DECISIONS.md`. Inside a plan body, numbering is
   plan-scoped, so check only that the reference is not out of range for that
   plan. A range whose end exceeds the source maximum fails.
4. A build token (`vitaftpshell-v...` or `b<number>`) outside the allowlisted
   volatile docs: `PROJECTS.md`, `docs/VERIFICATION.md`, `docs/FIXES.md`,
   `docs/DECISIONS.md`, and dated plans.
5. A missing status pointer: `README.md`, `AGENTS.md`, and `docs/PROJECT.md`
   must each contain a link to `PROJECTS.md` and a link to
   `docs/VERIFICATION.md`. This is a presence check, not prose analysis.
6. A traceability table missing one of the three primary features, an R-ID, or an
   M-row.

The script names the offending file and line. Start with these high-signal
checks; do not add prose-style rules that would produce false positives.

### Sequencing

0. **Pre-flight.** Re-verify every Problem Frame claim and Correction Matrix row
   against the current tree. Drop anything already fixed and record any new
   drift the audit missed. The plan is a directive, not a blind patch.
1. Owner decisions.
2. Ownership table and archive rules.
3. Archive completed plans and rejected designs.
4. Truth pass in dependency order: v1 plan → `DECISIONS.md` →
   `VERIFICATION.md` → `docs/PROJECT.md` → `PROJECTS.md` → `AGENTS.md` →
   `README.md` → indexes.
5. Traceability table and matrix rows.
6. Docs gate and CI wiring.
7. Run the verification contract and update `PROJECTS.md`.

Decisions come before prose; evidence comes before summaries; indexes come last
so they point at the final names.

### Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Over-pruning loses rationale | Archive, do not delete, anything with unique reasoning; move it to a decision first. |
| The docs gate becomes noisy | Start with link, index, reference, build-token, and traceability checks only. |
| Status changes while the docs pass runs | Status lives only in `PROJECTS.md`/`VERIFICATION.md`; do not pin a build in summaries. |
| The remote-upload correction is lost | DC6 and the traceability table state file-level upload works; only folder transfer and external evidence are open. |
| The pass silently changes behavior | No `src/` changes; D35 is resolved by decision or a separate plan. |

---

## Open Decisions

| Question | Recommended default | If not accepted |
|---|---|---|
| Remote recursive folder transfer (R22/R23) | Keep deferred for v1; state it as a known limit everywhere. | Write a feature doc and a separate plan; do not let the summary docs imply it exists. |
| One-press retry (D35) | Supersede for v1: a failed item is retried by re-running it; remove `X TO RETRY` from the docs. | Implement it in a separate behavior plan; keep the docs claim until the code lands. |
| “Defacto standard” ambition | Add to `STRATEGY.md` with measurable proxies; do not claim it as achieved. | Withdraw the wording and keep the narrower “console as a peer” positioning. |

---

## Verification Contract

The applying agent must show:

- `python3 tools/check_docs.py` passes.
- `./tools/test/run.sh` passes, including the new docs gate.
- The workspace link/fence check reports no issues.
- `git diff --stat` shows only documentation, indexes, decisions, evidence rows,
  and the docs tool; no `src/` changes.
- `git grep` finds no current-scope contradiction for: `one client`, `one active
  transfer`, `multiple server clients`, `b63`, `R1-R70`, `R1-R73`, `U1-U17`,
  unqualified `recursive folders`, or `TO RETRY` (unless D35 is kept and
  implemented).
- The three-feature traceability table exists and each row names its R-IDs and
  M-rows.
- `README.md`, `AGENTS.md`, and `docs/PROJECT.md` send status readers to
  `PROJECTS.md`/`VERIFICATION.md`; none pins a build.
- D35 is resolved in `DECISIONS.md` and all documents agree.
- Remote file upload is recorded as working; no document says file upload is
  missing.
- `PROJECTS.md` remains `building`; `verified` is not claimed without a console
  run.

## Definition of Done

- The active set is exactly: root `README.md`, `STRATEGY.md`, `PROJECTS.md`,
  `AGENTS.md`; `docs/{README,PROJECT,ARCHITECTURE,DESIGN,DECISIONS,FIXES,
  VERIFICATION,THIRD_PARTY}.md`; the active plans and settled design sheets;
  and the two archive indexes.
- No completed plan or rejected design is mixed into the active set.
- Every summary doc links to the owner of each volatile fact.
- The traceability table and the docs gate are in place.
- `PROJECTS.md` has a docs-consolidation row and the next action.
- No application behavior, layout geometry, or evidence history changed.

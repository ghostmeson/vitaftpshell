# vitaFTPShell documentation

Everything durable about this project lives here except the strategy, the
tracker, and the project contract, which stay at the project root for tool
discovery.

## Map

| Path | What it is | Owner |
|---|---|---|
| `../README.md` | Project overview, v1 scope, build, and safety model | Project |
| `../STRATEGY.md` | Product direction, users, boundaries, metrics | Product |
| `../PROJECTS.md` | The board: phase, last job, single next action, risks, and the job log | Project |
| `../AGENTS.md` | The project contract agents read before editing | Project |
| `README.md` | This map and the documentation rules | Project |
| `PROJECT.md` | Scope, non-goals, roadmap, failure policy | Product |
| `ARCHITECTURE.md` | Layers, ports, threading, handoff, module rules | Engineering |
| `DESIGN.md` | The settled layout, states, and input; the sheets | Design |
| `DECISIONS.md` | Numbered decisions with context and consequences | All |
| `FIXES.md` | Chronological fix index: build, symptom, cause, fix, evidence, hardware status | Engineering |
| `VERIFICATION.md` | Hardware matrix, build evidence, and the per-build test record | Engineering |
| `THIRD_PARTY.md` | Reused code, licenses, and protocol references | Engineering |
| `plans/` | The active v1 and implementation plans with their index; completed plans move to the archive | Planning |
| `features/` | Future feature documents, one dated document per feature | Product |
| `design/` | The screen-by-screen design sheets, the settled renders, and the launcher note; rejected directions move to the archive | Design |

## Why the strategy is not here

`STRATEGY.md` stays at the project root because the Compound Engineering skills
read it there. A copy in `docs/` would create two sources of truth. Link to the
root file instead.

## Archive

A completed plan and a rejected design direction leave the active set for the
archive folder in the same area. Each archive index gives every moved file one
row naming its outcome, the build where it landed, and where its decisions,
fixes, or evidence live. A file moves only with the references that point at it.

## Check

`python3 tools/check_docs.py` verifies links, fences, the plan and design
indexes, this ownership table, R/U/KTD/D references, the shared status block,
and the retired-phrase list. `./tools/test/run.sh` runs it beside the layering
guard and the host suites.

## Naming

- Requirements and implementation plans:
  `plans/YYYY-MM-DD-HHMM-<type>-<topic>-plan.md`
- Feature documents:
  `features/YYYY-MM-DD-<topic>.md`
- Design documents and mockups:
  `design/YYYY-MM-DD-<screen-or-topic>.md`

## Rules

- One fact, one home. Link instead of copying. The board owns status and the
  next action; the evidence page owns hardware results; the fix index owns
  defects; the decisions own choices; the v1 plan owns behavior.
- Update `../PROJECTS.md` when a job changes the phase or the next action.
- Add a numbered entry to `DECISIONS.md` when a choice would surprise a future
  reader.
- Add a row to `FIXES.md` for every fixed defect, with the build, the evidence,
  and an honest `HW`/`HOST`/`PENDING` status.
- A feature document is not a requirements plan. Use `ce-brainstorm` for new
  behavior and link the result here.
- Do not put a finding only in chat.

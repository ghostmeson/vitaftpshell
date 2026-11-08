# vitaFTPShell documentation

Everything durable about this project lives here except the strategy, the
tracker, and the project contract, which stay at the project root for tool
discovery.

## Map

| Path | What it is | Owner |
|---|---|---|
| `../STRATEGY.md` | Product direction, users, boundaries, metrics | Product |
| `../PROJECTS.md` | Current phase, last job, next action, risks | Project |
| `../AGENTS.md` | The project contract agents read before editing | Project |
| `PROJECT.md` | Scope, non-goals, roadmap, failure policy | Product |
| `ARCHITECTURE.md` | Layers, ports, threading, handoff, module rules | Engineering |
| `DESIGN.md` | The settled layout, states, and input; the sheets | Design |
| `DECISIONS.md` | Numbered decisions with context and consequences | All |
| `THIRD_PARTY.md` | Reused code, licenses, and protocol references | Engineering |
| `plans/` | The v1 plan: requirements, decisions, units, verification | Planning |
| `features/` | Future feature documents | Product |
| `design/` | The screen-by-screen design sheets and renders | Design |

## Why the strategy is not here

`STRATEGY.md` stays at the project root because the Compound Engineering skills
read it there. A copy in `docs/` would create two sources of truth. Link to the
root file instead.

## Naming

- Requirements and implementation plans:
  `plans/YYYY-MM-DD-HHMM-<type>-<topic>-plan.md`
- Feature documents:
  `features/YYYY-MM-DD-<topic>.md`
- Design documents and mockups:
  `design/YYYY-MM-DD-<screen-or-topic>.md`

## Rules

- One fact, one home. Link instead of copying.
- Update `../PROJECTS.md` when a job changes the phase or the next action.
- Add a numbered entry to `DECISIONS.md` when a choice would surprise a future
  reader.
- A feature document is not a requirements plan. Use `ce-brainstorm` for new
  behavior and link the result here.
- Do not put a finding only in chat.

# Plans

Requirements and implementation plans live here. One plan is one coherent work
unit; do not use this folder as a scratchpad.

## Naming

`YYYY-MM-DD-HHMM-<type>-<topic>-plan.md`

## Current

| Plan | Kind | Status |
|---|---|---|
| [`2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](2026-11-08-1103-feat-vitaftpshell-v1-plan.md) | Implementation-ready unified plan | Behavior source of truth; the 2026-11-14 and 2026-11-16 correction passes applied; the console matrix is the open row |
| [`2026-11-16-0643-fix-residual-network-closure-plan.md`](2026-11-16-0643-fix-residual-network-closure-plan.md) | Implementation-ready fix plan | Code units landed; its record unit was carried out by the truth sweep; console matrix pending |
| [`2026-11-16-0845-docs-documentation-truth-sweep-plan.md`](2026-11-16-0845-docs-documentation-truth-sweep-plan.md) | Documentation sweep plan | Active until the sweep lands, then archived |

Completed plans move to [`archive/`](archive/README.md), one index row each.
A plan is archived only after its units are absorbed by `../DECISIONS.md`,
`../FIXES.md`, or `../VERIFICATION.md`, and its inbound citations are rewritten.

## Rules

- A requirements plan is the source of truth for behavior until an
  implementation plan enriches the same file.
- Do not fork a plan into a second file for a small change. Update the plan and,
  when the change is a real decision, add an entry to `../DECISIONS.md`.
- A new feature gets a feature document under `../features/` first; its
  implementation plan lands here.
- Keep the frontmatter and the stable headings intact so downstream tools can
  find the plan.

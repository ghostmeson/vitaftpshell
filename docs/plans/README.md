# Plans

Requirements and implementation plans live here. One plan is one coherent work
unit; do not use this folder as a scratchpad.

## Naming

`YYYY-MM-DD-HHMM-<type>-<topic>-plan.md`

## Current

| Plan | Kind | Status |
|---|---|---|
| [`2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](2026-11-08-1103-feat-vitaftpshell-v1-plan.md) | Implementation-ready unified plan | Behavior source of truth; the 2026-11-14 and 2026-11-16 correction passes applied; R22/R23 are planned and not built, and the console matrix is the open row |
| [`2026-11-16-2011-feat-local-workspace-plan.md`](2026-11-16-2011-feat-local-workspace-plan.md) | Implementation-ready unified plan | Implemented on the host through its nine units; matrix rows M24-M27 are pending (the `imc0:` leg is BLOCKED on the PCH-1000) |

Completed plans move to [`archive/`](archive/README.md), one index row each.
A plan is archived only after its units are absorbed by `../DECISIONS.md`,
`../FIXES.md`, or `../VERIFICATION.md`, and its inbound citations are rewritten.
The v1 plan stays here after its code lands because it owns the R, KTD, and U
numbering every other document cites; its remaining requirements live in this
file, not in a second plan.

## Rules

- A requirements plan is the source of truth for behavior until an
  implementation plan enriches the same file.
- Do not fork a plan into a second file for a small change. Update the plan and,
  when the change is a real decision, add an entry to `../DECISIONS.md`.
- A new feature gets a feature document under `../features/` first; its
  implementation plan lands here.
- Keep the frontmatter and the stable headings intact so downstream tools can
  find the plan.

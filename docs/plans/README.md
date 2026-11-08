# Plans

Requirements and implementation plans live here. One plan is one coherent work
unit; do not use this folder as a scratchpad.

## Naming

`YYYY-MM-DD-HHMM-<type>-<topic>-plan.md`

## Current

| Plan | Kind | Status |
|---|---|---|
| [`2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](2026-11-08-1103-feat-vitaftpshell-v1-plan.md) | Requirements-only unified plan | v1 contract; implementation sections are added by `ce-plan` |

## Rules

- A requirements plan is the source of truth for behavior until an
  implementation plan enriches the same file.
- Do not fork a plan into a second file for a small change. Update the plan and,
  when the change is a real decision, add an entry to `../DECISIONS.md`.
- A new feature gets a feature document under `../features/` first; its
  implementation plan lands here.
- Keep the frontmatter and the stable headings intact so downstream tools can
  find the plan.

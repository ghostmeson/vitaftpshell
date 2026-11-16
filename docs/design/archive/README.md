# Design archive

The settled sheet for each screen stays beside its family in `../` because the
states sheets reference their renders by relative path, and moving one half of a
family would break the record. This folder marks where the rejected directions
belong: a direction that lost stays in its family, and `../README.md` records it
as rejected next to the chosen one.

| Family | Chosen | Rejected and kept as the record |
|---|---|---|
| WORKSPACE | A, two ledgers | B, C |
| SITES | C, hero site card + full table | A, B |
| SITE EDITOR | A, preview card + field table | B, C |
| SERVER | A, status + client panel | B, C |
| ACTIONS | A, action ledger + target | B, C |
| CONFIRM | A, centered interrupt | B, C |

A rejected sheet is never deleted: it is the reason the chosen one is what it
is. The layout authority is `../../DESIGN.md`; the decisions are in
`../../DECISIONS.md`.

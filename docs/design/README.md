# Design documents and mockups

This folder holds the screen-by-screen design pass for vitaFTPShell. The visual
phase is complete; the sheets below are the record. `docs/DESIGN.md` is the
settled layout authority.

## What belongs here

- rough wireframes and direction-only mockups;
- screen-by-screen notes from the user review;
- settled layout decisions that update `../DESIGN.md`;
- reference images or rendered states that are worth keeping.

## Naming

- Notes: `YYYY-MM-DD-<screen-or-topic>.md`
- Images: `YYYY-MM-DD-<screen-or-topic>-<state>.png`

## Process

1. Use `frontend-design` to create rough options.
2. Label them as direction-only; do not imply final colors or pixel geometry.
3. Review one screen at a time with the user.
4. Record the chosen direction in `../DESIGN.md` and, when it is a real decision,
   in `../DECISIONS.md`.
5. Hand the settled geometry to the implementation plan.

## Constraints

The visual language is the COCKPIT design system, restated in
[`../DESIGN.md`](../DESIGN.md) and `src/gfx/theme.c`. The project inherits its
palette, type scale, spacing scale, panel chrome, and restraint rules. Do not
invent a second alert colour, a new type size, or a decorative icon.

## Index

| Date | File | State |
|---|---|---|
| 2026-11-08 | [WORKSPACE directions](2026-11-08-workspace-directions.html) | Three direction-only compositions; A was chosen |
| 2026-11-08 | [A — two ledgers](2026-11-08-workspace-a.png) | Direction A render, 1920×1088 |
| 2026-11-08 | [B — one instrument](2026-11-08-workspace-b.png) | Direction B render, 1920×1088 |
| 2026-11-08 | [C — focus](2026-11-08-workspace-c.png) | Direction C render, 1920×1088 |
| 2026-11-08 | [A states](2026-11-08-workspace-a-states.html) | Six WORKSPACE states; awaiting approval |
| 2026-11-08 | [A idle](2026-11-08-workspace-a-idle.png) | Two marked, dock idle |
| 2026-11-08 | [A transferring](2026-11-08-workspace-a-transferring.png) | Download at 42% |
| 2026-11-08 | [A done](2026-11-08-workspace-a-done.png) | Batch complete, destination refreshed |
| 2026-11-08 | [A failed](2026-11-08-workspace-a-failed.png) | Data channel timed out, session healthy |
| 2026-11-08 | [A disconnected](2026-11-08-workspace-a-disconnected.png) | Control channel lost, reconnect offered |
| 2026-11-08 | [A empty and reading](2026-11-08-workspace-a-empty-reading.png) | Empty local folder, remote listing loading |
| 2026-11-08 | [SITES directions](2026-11-08-sites-directions.html) | Three compositions; awaiting choice |
| 2026-11-08 | [SITES A](2026-11-08-sites-a.png) | Ledger + detail + password note |
| 2026-11-08 | [SITES B](2026-11-08-sites-b.png) | Single ledger + summary strip |
| 2026-11-08 | [SITES C](2026-11-08-sites-c.png) | Hero site card + full table |
| 2026-11-08 | [SITES C states](2026-11-08-sites-c-states.html) | First run, populated, empty, login failed, save unavailable |
| 2026-11-08 | [SITES C first run](2026-11-08-sites-c-first-run.png) | Seeded FTP.GNU.ORG |
| 2026-11-08 | [SITES C populated](2026-11-08-sites-c-populated.png) | Six sites, HOME NAS selected |
| 2026-11-08 | [SITES C empty](2026-11-08-sites-c-empty.png) | No sites |
| 2026-11-08 | [SITES C login failed](2026-11-08-sites-c-login-failed.png) | 530 reply on the card |
| 2026-11-08 | [SITES C save unavailable](2026-11-08-sites-c-save-unavailable.png) | In-memory list |
| 2026-11-08 | [SITE EDITOR directions](2026-11-08-site-editor-directions.html) | Three field layouts; awaiting choice |
| 2026-11-08 | [SITE EDITOR A](2026-11-08-site-editor-a.png) | Preview card + field table |
| 2026-11-08 | [SITE EDITOR B](2026-11-08-site-editor-b.png) | Single tall form |
| 2026-11-08 | [SITE EDITOR C](2026-11-08-site-editor-c.png) | Two-column form |
| 2026-11-08 | [SITE EDITOR A states](2026-11-08-site-editor-a-states.html) | New, clean edit, invalid, first save |
| 2026-11-08 | [Editor new anonymous](2026-11-08-site-editor-a-new-anonymous.png) | Default new site |
| 2026-11-08 | [Editor clean edit](2026-11-08-site-editor-a-edit-clean.png) | Existing site, no changes |
| 2026-11-08 | [Editor invalid](2026-11-08-site-editor-a-invalid.png) | Empty required name |
| 2026-11-08 | [Editor first save](2026-11-08-site-editor-a-first-save.png) | Password warning before saving |
| 2026-11-08 | [SERVER directions](2026-11-08-server-directions.html) | Three compositions; awaiting choice |
| 2026-11-08 | [SERVER A](2026-11-08-server-a.png) | Console: status, warning, client |
| 2026-11-08 | [SERVER B](2026-11-08-server-b.png) | Bands: status, warning, client |
| 2026-11-08 | [SERVER C](2026-11-08-server-c.png) | Instrument: one panel |
| 2026-11-08 | [SERVER A states](2026-11-08-server-a-states.html) | Off, starting, on, browsing, transferring, bind failure |
| 2026-11-08 | [Server off](2026-11-08-server-a-off.png) | Launch state |
| 2026-11-08 | [Server starting](2026-11-08-server-a-starting.png) | Listener binding |
| 2026-11-08 | [Server on, no client](2026-11-08-server-a-on-idle.png) | Running alone |
| 2026-11-08 | [Server browsing](2026-11-08-server-a-browsing.png) | One client browsing |
| 2026-11-08 | [Server transferring](2026-11-08-server-a-transferring.png) | One client uploading |
| 2026-11-08 | [Server bind failed](2026-11-08-server-a-bind-failed.png) | Port in use |
| 2026-11-08 | [ACTIONS directions](2026-11-08-actions-directions.html) | Three compositions; awaiting choice |
| 2026-11-08 | [ACTIONS A](2026-11-08-actions-a.png) | Action ledger + target |
| 2026-11-08 | [ACTIONS B](2026-11-08-actions-b.png) | Single action table |
| 2026-11-08 | [ACTIONS C](2026-11-08-actions-c.png) | Pane-mode actions |
| 2026-11-08 | [ACTIONS A states](2026-11-08-actions-a-states.html) | Local single, local multi, remote single, protected root |
| 2026-11-08 | [Actions local single](2026-11-08-actions-a-local-single.png) | No marks, copy/move dim |
| 2026-11-08 | [Actions local multi](2026-11-08-actions-a-local-multi.png) | Three marks, copy/move to this folder |
| 2026-11-08 | [Actions remote single](2026-11-08-actions-a-remote-single.png) | Download to the local pane |
| 2026-11-08 | [Actions protected](2026-11-08-actions-a-protected.png) | Volume root, destructive actions dim |
| 2026-11-08 | [CONFIRM directions](2026-11-08-confirm-directions.html) | Three placements; awaiting choice |
| 2026-11-08 | [CONFIRM A](2026-11-08-confirm-a.png) | Centered interrupt |
| 2026-11-08 | [CONFIRM B](2026-11-08-confirm-b.png) | Bottom sheet |
| 2026-11-08 | [CONFIRM C](2026-11-08-confirm-c.png) | Full screen |
| 2026-11-08 | [CONFIRM A states](2026-11-08-confirm-a-states.html) | Short delete, long delete, conflict, cancel, stop, discard |
| 2026-11-08 | [Confirm delete 3](2026-11-08-confirm-a-delete-3.png) | Short list |
| 2026-11-08 | [Confirm delete 12](2026-11-08-confirm-a-delete-12.png) | Five-row scrolling list |
| 2026-11-08 | [Confirm conflict](2026-11-08-confirm-a-conflict.png) | Three options |
| 2026-11-08 | [Confirm cancel](2026-11-08-confirm-a-cancel-download.png) | Cancel a transfer |
| 2026-11-08 | [Confirm stop](2026-11-08-confirm-a-stop-server.png) | Stop during a write |
| 2026-11-08 | [Confirm discard](2026-11-08-confirm-a-discard-changes.png) | Discard editor changes |
| 2026-11-08 | [TEXT ENTRY](2026-11-08-text-entry.html) | IME contract, cancelled, unavailable |
| 2026-11-08 | [Text entry open](2026-11-08-text-entry-open.png) | System keyboard contract |
| 2026-11-08 | [Text entry cancelled](2026-11-08-text-entry-cancelled.png) | Field unchanged |
| 2026-11-08 | [Text entry unavailable](2026-11-08-text-entry-unavailable.png) | Inline fallback |

The HTML sheets draw at 1:1 with the real 5×7 face; open them in a browser. The
PNGs are the same canvases at 2× for records.

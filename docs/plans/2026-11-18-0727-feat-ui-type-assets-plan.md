---
title: UI Type, Icons, and Visual Assets - Plan
type: feat
date: 2026-11-18
topic: ui-type-assets
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-brainstorm
execution: code
status: approved 2026-11-18; implementation not started
---

# UI Type, Icons, and Visual Assets - Plan

> **Approved 2026-11-18; implementation not started.** This plan is the source
> for U1-U11. It was drafted and reviewed on 2026-11-17 and 2026-11-18; the
> resolved questions are recorded below, and decision D105 carries the settled
> design. No code is written yet, and the console rows are PENDING.
>
> **How to add to it:** put new items in [Additions](#additions), one row each.
> Every row becomes one unit, with its own evidence and acceptance line. Nothing
> in this pass ships as "misc".

## Goal Capsule

- **Objective:** Every filename and path in the app reads as a name — case
  preserved, marks drawn, accents drawn, no silent holes — and every visual
  surface (launcher, LiveArea, boot frame, screens, dock, dialogs) shares one
  deliberate language, without changing behaviour. Folder and file-type icons
  join that language: one 16 px family, one row rail.
- **Means:** Spleen 6×12 at the 1.5 step for data strings; the settled 5×7
  stencil stays for chrome, values, and labels; folder and type icons are
  16×16 1-bit masks from two MIT sets plus three drawn shapes; assets stay
  generated, indexed-8-bit, and guarded.
- **Product authority:** `STRATEGY.md` (the console is the workspace),
  `docs/DESIGN.md` (layout law), `docs/DECISIONS.md` D103 and D104 (the shipped
  art), and the new type decision this plan asks for.
- **Execution profile:** design first, then one implementation branch, host
  suites and the docs gate on every unit, the console matrix at freeze.
- **Stop conditions:** a geometry change outside `DESIGN.md`; a second writer
  of shared state; a new platform claim outside an adapter; an unlicensed font
  or asset; a visual claim without a host render or console evidence.
- **Not active scope:** behaviour, path rules, transfer policy, server policy,
  screen inventory, and input. This plan changes how surfaces look, not what
  they do.

---

## Product Contract

### Summary

The app's in-app type is the last placeholder. The launcher icon, LiveArea page,
gate thumbnail, and boot frame were replaced in b83 (D103, D104). The file list
still uses the 5×7 display stencil for filenames: it folds case, drops nineteen
marks, has no lowercase or accents, and advances silently over anything it does
not carry. That is a correctness problem before it is a taste problem, and it is
visible in the one place a file manager must be exact.

This pass replaces the data face, keeps the stencil where it belongs, and gives
the remaining visual additions a home so they land as deliberate changes rather
than a sweep.

The row's left gutter is the second gap. Every entry is a page-shaped blank, so
a folder, a photo, a song, a plugin, a game, a disc image, and an executable look
the same. The icon pass gives each row a 16×16 type mark, drawn from a 1-bit
mask with no runtime asset loading, and moves the mark and risk badges into a
dedicated rail. The batch mark stays a 6×6 square; a star is reserved for a
future favorite, and the risk diamond becomes red. The ALL DRIVES root gets
three storage classes — card, drive, game card — so the mounts stop looking
identical.

### Problem Frame

The stencil was drawn to shout. It is five pixels wide, seven rows tall,
uppercase only, and it was authored for labels such as `LOCAL`, `TRANSFER`, and
`DELETE 3 ITEMS`. Filenames are the opposite kind of text: mixed case, long,
full of punctuation, often accented, and read in bulk.

The proof is in the sheet:

- `README.md` and `readme.md` render as the same row, because `lookup()` folds
  lowercase away. A remote Unix server can hold both in one folder.
- Nineteen characters ordinary filenames use are absent: `! # $ ' ( ) * ? @ =
  \ ^ ` { } ~ ; "`. The renderer advances without drawing, so `Holiday (2019)`
  becomes `HOLIDAY  2019 ` with invisible holes.
- `Pokémon` loses its accent; a CJK name draws nothing and consumes three cells
  per character.
- The name column truncates around 26 cells, so a normal Vita backup name gets
  an ellipsis.

The row budget is hard. The name sits at `y+8` in a 30 px row, leaving 22 px
for the whole glyph box. That is why most open bitmap faces do not fit: a
12-row terminal face is 24 px at 2.0 before the inset.

### Key Decisions

- **Two faces, two jobs.** The stencil keeps the app's voice: labels, titles,
  values, buttons, the wordmark, and the legend. A reading face takes data:
  filenames, paths, site names, hosts, and transfer subjects. A face is chosen
  by role, never by screen or taste. Governs R1, R7, R8, R11.
- **Spleen 6×12 at 1.5.** Measured from `spleen-6x12.bdf`: 548 glyphs, 544
  inked, BSD-2-Clause, eight-row capitals, six-row x-height, two-row
  descenders, six-pixel advance. At the micro step it gives a 12 px cap, a
  9 px x-height, a 9 px advance, and 37 cells beside `4.0 MB`. Governs R1-R6.
- **The name drops one ladder step, deliberately.** Names move from
  `PX_LABEL` 2.0 to `PX_MICRO` 1.5. The baseline stays at 22 px; the name's top
  moves from `y+8` to `y+10`. That is the accepted cost for full coverage and
  eleven more characters. Governs R1, R7, R9.
- **Spleen 5×8 is rejected as-is.** It fits at 2.0 with integer scaling and 33
  cells, but every accented Latin letter is an empty placeholder (99 empty
  slots in all): `é` draws nothing. It stays as the fallback only with a composed accent block. Governs
  R4, R15.
- **The stencil's metrics do not move.** `FONT_GLYPH_H` stays 7, so `LINE_H`,
  `READOUT_STRIDE`, panel rhythm, and every label keep their positions. The
  reading face is a separate table with its own height and advance. Governs
  R9, R10.
- **Assets are generated, guarded, and already shipped.** The launcher and
  LiveArea art from D103/D104 is the baseline. This pass does not redraw it
  unless an addition says so, and any change keeps indexed 8-bit PNGs, the
  generator, and the drift check. Governs R16.
- **Every addition is a unit.** The remaining icon, asset, and visual items are
  first-class rows in [Additions](#additions), each with a surface, a reason,
  and evidence. The folder and file-type icons are core units (U8–U10), not
  additions. Governs R17.
- **Heroicons for content, Bootstrap for storage, three drawn shapes.**
  Heroicons 16/solid (MIT) covers the folder, up, and the content types,
  including plugin (`cpu-chip`), package (`cube`), and exe (`window`). The
  storage classes are Bootstrap Icons (MIT): `sd-card-fill` for cards and
  `device-ssd-fill` for the drive. The gamepad, the disc, and the game-card
  cartridge are drawn on the same grid. Both families are filled 16 px and MIT.
  Governs R22-R25, R27.
- **Three storage classes.** `ux0:` and `imc0:` are cards, `ur0:`, `uma0:`, and
  `app0:` are the drive, and `gro0:`/`grw0:` are the game card. The value still
  says `DRIVE`/`DRIVE RO`; the class is shape only. Governs R22, R23.
- **Icons are shapes, not a second colour code.** Files use `C_DIM`; folders,
  volumes, and up use `C_WHITE`; the selected row uses `C_FIELD`. The numeric
  sizes stay white. No file type gets a colour. Governs R26.
- **The risk diamond is red.** The current mark is yellow; the review asked for
  red. This moves risk out of the yellow warning family and into red, which also
  carries failure and refusal. The diamond keeps its shape and position; yellow
  keeps network activity, waiting local work, and the batch square. Governs R25,
  R26.
- **The batch mark stays a square; the star is a future favorite.** The review
  asked for a star, and a star normally means "favorite", so the 6×6 square
  stays the transient batch mark and the star is deferred to a favorites
  feature. The square and the red risk diamond share the x 31 rail; the name
  moves to x 42. Governs R25.
- **The icon is a bitmap, not a runtime asset.** 16×16 1-bit masks live in gfx
  and are drawn with the existing rect primitives. No SVG, PNG, or texture is
  loaded at runtime; the source SVGs, the drawn masks, and the two notices are
  vendored for provenance. Governs R24, R27.
- **The mapping is policy.** `file_icon_for(name, row_type)` lives in core,
  matches extensions case-insensitively and mounts by name, and is host-tested
  across the content types and the three storage classes. The renderer asks for
  the icon; the row struct does not grow. Governs R23, R28.

### Requirements

**The reading face**

- R1. Workspace filenames draw in Spleen 6×12 at 1.5, on the row baseline of
  22 px (top `y+10`). The selected row is field-on-cyan and remains legible.
- R2. Case is preserved. `README.md` and `readme.md` are distinct rows.
- R3. Every printable ASCII mark draws from the face; no character advances
  without ink except the space.
- R4. Latin-1 accented letters draw in lower and upper case (`é É ü Ü ñ Ñ å Å
  ø Ø`, and the rest of the range). A name with an accent is not a hole.
- R5. An unsupported codepoint renders one tofu box, not one silent cell per
  UTF-8 byte. Names are decoded as UTF-8 at the renderer, and a truncated name
  is never cut mid-codepoint.
- R6. `font_fit` truncates at the reading face's 9 px advance and appends `..`;
  the result never overlaps the size column or the pane edge.
- R7. Paths (local and remote) use the reading face at 1.5, top `y+24.5`,
  baseline 36.5, so sighted names in a path are cased and accented too.
- R8. Values (`3.2 GB`, `DIR`, `DRIVE RO`, `UP`, `ASIDE`), labels, titles,
  buttons, the wordmark, and the legend stay stencil.
- R9. No pane or row geometry changes: 456×352 panes, 30 px rows, ten visible,
  value right 442, baselines, and the ladder constants. The one row change is
  the icon rail and the name's two-pixel shift in R25.
- R10. `FONT_GLYPH_H` remains 7. The reading face declares its own rows,
  advance, cap height, and x-height; `LINE_H` and `READOUT_STRIDE` do not move.

**The rest of the surfaces**

- R11. The other data surfaces are reviewed one at a time and take the reading
  face only where a filename or path is the content: sites list, site editor
  host, server client path, dock subject and path, confirm item names, actions
  destination and selected name. Readouts at `PX_VALUE` and above (address,
  counts, site name) stay stencil.
- R12. A surface that takes the face is positioned by baseline, not by a
  guessed top; its baseline and the stencil text beside it must agree.
- R13. A surface that keeps the stencil records why in the plan's unit notes.

**Provenance and guards**

- R14. The Spleen table is reproducible from a vendored BDF by a generator, and
  the generator's `--check` mode fails the host gate when the C table drifts
  from the BDF. The BDF's copyright and BSD-2-Clause notice are kept.
- R15. `LICENSES/SPLEEN-BSD-2.txt` exists and `docs/THIRD_PARTY.md` names the
  face, its version, its source, and the files it enters.
- R16. The launcher art remains generated by
  `tools/art/build_launcher_art.py`; the art generator still reads the stencil
  table explicitly, and a second table in `font.c` cannot redirect it. A new
  asset is indexed 8-bit and joins `tools/check_assets.sh`.
- R17. Every visual addition has a unit, a surface, a screenshot, and an
  acceptance line; no addition is "while we are here".

**Verification**

- R18. Host tests cover case, every mark, accented names, tofu, truncation at
  9 px, the reading baseline, and the updated render traces.
- R19. The full host gate is green: layering, worker stack, assets, art drift,
  docs, and every suite.
- R20. The console run records the 1.5 verdict on the PCH-1000: a name, a
  selected row, a long name, an accented name, a CJK name, and a path. The
  project is not `verified` without that run.
- R21. If 1.5 reads soft on hardware, the documented fallback is Spleen 5×8 at
  2.0 plus a composed accent block; GohuFont 11 at 2.0 is the no-glyph-work
  option at the cost of taller capitals and 26 cells.

**Icons**

- R22. Every row draws a 16×16 icon: a folder for a directory, an up arrow for
  the `..` row, a plugin for a taiHEN module, and a type for every other file.
  A mount draws its storage class: card, drive, or game card.
- R23. A file's icon comes from its name: case-insensitive, the last extension;
  a dotfile with no other dot, a trailing dot, and an absent or unknown
  extension are generic. Categories: text, image, audio, video, archive, code,
  binary, plugin, package, game, disc, exe, generic. Archive covers `zip`, `7z`,
  `rar`, `tar`, `gz`, `tgz`, `bz2`, `xz`, `zst`, `lz`, `cab`; plugin covers
  `skprx`, `suprx`, `prx`; game covers the console/handheld list plus `pbp`,
  `wbfs`, `wad`, `gcm`; disc covers `iso`, `cso`, `img`, `mdf`, `nrg`, `dmg`,
  `cue`, `chd`, `gdi`, `cdi`; audio adds `at9`, `vag`, `oma`; video adds `pmf`;
  exe covers `exe`, `msi`, `bat`, `cmd`, `com`. `.bin` stays binary. `..` and
  directories never use the extension table. A volume's icon comes from its
  name: `ux0:` and `imc0:` card, `ur0:`, `uma0:`, and `app0:` drive, `gro0:` and
  `grw0:` game card; any other mount is the drive.
- R24. Icons are 16×16 1-bit masks in gfx, drawn with the existing rect
  primitives. No SVG, PNG, or texture is decoded at runtime; no platform header
  enters the table; no per-frame allocation.
- R25. The icon sits at x 14, y+7. The batch mark is the 6×6 square at x 31,
  y+18; the risk diamond is 8×8 at x 31, y+4 and is `C_RED`. The name moves from
  x 40 to x 42 so the rail has clearance; the value column and the 30 px row do
  not move. The name still fits 37 Spleen cells. The star is not shipped; it is
  a future favorite.
- R26. File icons use `C_DIM`; folder, volume, and up use `C_WHITE`; the
  selected row uses `C_FIELD`. The numeric sizes stay white. The risk diamond is
  `C_RED`; the batch square stays `C_YELLOW`; no file type gets a colour.
- R27. The content set is Heroicons 16/solid (MIT): fourteen icons, including
  `cpu-chip` for a plugin, `cube` for a package, and `window` for an exe. The
  storage pair is Bootstrap Icons (MIT): `sd-card-fill` and `device-ssd-fill`.
  The gamepad, the disc, and the game-card cartridge are project masks on the
  same 16×16 grid. The source SVGs, the drawn masks, and both notices are
  vendored; `docs/THIRD_PARTY.md` names each set, its version, and the exact
  files.
- R28. The mapping is a pure core function with host tests: case, dotfile, no
  extension, multi-dot, unknown, every category, every mount, and row-type
  precedence.
- R29. The mask table is tested for 19 categories × 16 rows, at least one bit
  each, and the draw placement is asserted on a sample row; the selected row
  uses the field knockout and the square's cutout.
- R30. The console run covers the seven mounts (three classes), a folder, the up
  row, a text, image, audio, video, archive, code, binary, plugin, package,
  game, disc, and exe file, a marked row (square), a red risk row, and a
  selected row, judged at 1×.

### Key Technical Decisions

- **KTD1 — Generate the C table from the BDF.** Vendor
  `spleen-6x12.bdf` under `tools/font/`, add
  `tools/font/import_spleen.py`, and emit a C table plus its metrics. The
  alternative — a hand-copied table — cannot be re-derived and cannot be
  checked. This mirrors the launcher art's generator and `--check` discipline.
- **KTD2 — Face-aware text, no hidden state.** Add a `FontFace` enum
  (`FONT_STENCIL`, `FONT_SPLEEN`) and `font_text_face`, `font_width_face`,
  `font_fit_face`; the existing `font_text`, `font_width`, and `font_fit`
  remain stencil wrappers, so untouched call sites do not change. A global
  "current face" is rejected: it makes a render depend on call order.
- **KTD3 — Position the reading face by baseline.** Add
  `font_text_face_baseline(...)` (or a `baseline` parameter) and derive the
  glyph top from the face's cap height. The row's invariant is the 22 px
  baseline, not the top pixel; a face change should not repeat the arithmetic
  at every call site. The path line's baseline is 36.5.
- **KTD4 — Decode UTF-8 at the renderer.** The stencil walks bytes today; the
  reading face walks codepoints. One unsupported codepoint is one tofu. The
  logic lives in `src/gfx/font.c` (no platform header) and is unit-tested with
  a mixed ASCII/2-byte/3-byte/4-byte string.
- **KTD5 — Two tables, two metrics, one ladder.** The reading face has
  `FONT_SPLEEN_ROWS 12`, `FONT_SPLEEN_ADVANCE 6`, cap 8, x-height 6, descender
  2. `FONT_GLYPH_H` 7 and the stencil advance 6 stay. `LINE_H` is a stencil
  concept and is not reused for a name row.
- **KTD6 — Explicit table markers for tools.** The art generator must keep
  reading `GLYPHS[]`; the new table is `SPLEEN_GLYPHS[]` and the generator's
  split marker stays unique to the stencil. Add an assertion in the art script
  that it loaded the expected glyph count (51 today) so a wrong table fails
  loudly instead of drawing the wrong wordmark.
- **KTD7 — Two families by role, three drawn shapes.** Heroicons 16/solid (MIT)
  carries content; Bootstrap Icons (MIT) carries the storage pair
  (`sd-card-fill`, `device-ssd-fill`), because Heroicons has no card or drive
  vocabulary. The gamepad, the disc, and the game-card cartridge are drawn on
  the same grid. The star is not in this pass; it is reserved for a future
  favorite. A third family is rejected.
- **KTD8 — Vendored SVGs, committed masks, no runtime loader.** Rasterise the
  fourteen Heroicons and two Bootstrap device icons once at 16×16 with a 50 %
  alpha threshold, commit the 16-row masks, and keep the source SVGs plus the
  conversion recipe under `tools/icons/`. The gamepad, disc, and cartridge are
  drawn in `tools/icons/project/` and join the table. A build-time rasteriser is
  not worth a dependency for sixteen fixed icons; the sources, the drawn masks,
  and both notices are the provenance. A host test pins every mask's shape.
- **KTD9 — A badge rail, the square, and a two-pixel name shift.** The icon ends
  at x 30; the square and the red risk share the 31–39 rail; the name moves to
  x 42. The square is 6×6 at y+18; the diamond is 8×8 at y+4. The rail is the
  tightest part of the row, so the render is the gate. The star is a future
  favorite, not a batch mark.

### Units

**U1 — Import the face.**
Vendor `spleen-6x12.bdf`, add the generator and the C table, the BSD-2 notice,
and `docs/THIRD_PARTY.md` entries. No call site changes. Tests: table count
(548), inked count (544), cap/x-height/descender/advance metrics, `--check`
drift, and the art generator's 51-glyph assertion. Exit: the host gate is green
and nothing on screen has changed.

**U2 — Face-aware text and measure.**
Add the `FontFace` enum, the `_face` text/width/fit functions, the baseline
variant, and the reading metrics. Existing wrappers keep every current call
site identical. Tests: stencil output is byte-identical for a sample; the
reading face measures 9 px per cell at 1.5; `fit` truncates correctly at both
faces. Exit: the app renders exactly as before.

**U3 — The workspace panes.**
Switch row names and both path lines; set the name top to `y+10` and the path
top to `y+24.5`. Update `test_render_workspace` and the screen traces for the
new px, y, and strings. Capture 1:1 and 2× renders of the workspace in local
and remote mode, the selected row, a long name, an accented name, and a CJK
name. Exit: the pane reads as a file list, no geometry moves, and the size
column still aligns.

**U4 — The other surfaces, one at a time.**
Review, decide, and record: sites list, site editor host, server client path,
dock subject and path, confirm item names, actions destination and selected
name. For each: take the face or keep the stencil (R13), position by baseline
(R12), and add a screenshot. Exit: every data surface has a recorded decision;
no screen is half-converted.

**U5 — UTF-8, tofu, and truncation.**
Decode codepoints in the reading renderer, map the fallback, decode only
complete sequences when truncating, and add the mixed-script tests. Exit: a CJK
name is seven boxes, not twenty-one blanks; a truncated name never ends in a
broken byte.

**U6 — Documentation and assets.**
Record D105 (the two-face rule, the name's step, the baseline, the icon family,
the mask pipeline, the row rail, the red risk, the deferred star), update
`DESIGN.md`'s type constraint and the workspace row spec, add the design sheet
with the before/after and the icon set, add the three licences, update
`THIRD_PARTY.md`, `PROJECTS.md`, the plan index, and `FIXES.md` if the case and
mark failures are recorded as a fixed defect. Exit: the docs gate is green.

**U7 — Build and console.**
Build one VPK, confirm the font and icon tables and notices are packaged, run
the console matrix for the new type and the icons, and record the 1.5 verdict.
Exit: the new artifact is named, the matrix rows are written, and `verified` is
claimed only with the hardware run.

**U8 — Vendor and rasterise the icons.**
Vendor the fourteen Heroicons 16/solid SVGs and the two Bootstrap device SVGs
(`sd-card-fill`, `device-ssd-fill`) under `tools/icons/`, add both MIT notices
and the conversion recipe, draw the gamepad, disc, and cartridge under
`tools/icons/project/`, and add `src/gfx/file_icons.c` with the 16×16 masks and
`file_icon_draw`. Tests: 19 × 16 rows, at least one bit each, the table count,
and both licences. Exit: no row draws an icon yet; the table and the notices
are in place.

**U9 — The extension and mount mapping.**
Add `src/core/fileicon.c` with `FileIcon file_icon_for(const char *name,
UiRowType type)`, the extension table, including plugin (`skprx`, `suprx`,
`prx`), game (the console/handheld list plus `pbp`, `wbfs`, `wad`, `gcm`), disc
(`iso`, `cso`, `img`, `mdf`, `nrg`, `dmg`, `cue`, `chd`, `gdi`, `cdi`), audio
(`at9`, `vag`, `oma`), video (`pmf`), exe (`exe`, `msi`, `bat`, `cmd`, `com`),
and the archive list; and the mount table (`ux0:`/`imc0:` card, `ur0:`/`uma0:`/
`app0:` drive, `gro0:`/`grw0:` game card). `.bin` stays binary. Tests: every
extension category, case, dotfile, no extension, trailing dot, multi-dot,
unknown, every mount, and the type precedence. Exit: the mapping is proven
without touching the renderer.

**U10 — Draw the icon rail.**
Draw the icon at x 14, y+7 in `workspace.c`; draw the 6×6 square and the red
risk at the x 31 rail; move the name to x 42; set folder, volume, and up to
white and files to dim; add the layout constants. Update the render traces and
the design sheet. Capture 1:1 and 2× renders of the ALL DRIVES root with the
seven mounts in three classes, both panes with a folder, the up row, every
category, a plugin, a game, a disc, an exe, a marked row, a red risk row, and a
selected row. Exit: the type reads, the storage classes are distinguishable,
the square does not crowd the name, and the host suite is green.

**U11 — The additions.**
One unit per row in [Additions](#additions), in the agreed order. Each unit:
change, screenshot, acceptance line, and its own commit. Exit: the addition is
visible, documented, and does not silently alter a settled surface.

### Additions

The place for the remaining design and asset work. One row per item. Fill the
columns; I will turn each into a unit with a concrete file list and a test or
screenshot gate.

The folder, plugin, game, disc, exe, and storage-class requests are **U8–U10**,
not additions; they are part of the core visual pass. No additions are queued as
of 2026-11-18. The table stays for later items.

| ID | Item | Type | Surface | Why | Evidence | Unit |
|---|---|---|---|---|---|---|
| A1 | | | | | | |
| A2 | | | | | | |
| A3 | | | | | | |

Type is one of `fix`, `add`, `change`. Surface is a screen, the launcher, the
LiveArea page, the boot frame, a repo image, or the design docs. Evidence is the
screenshot, the console run, or the test that proves it. If an item is a new
screen or a geometry change, stop: it needs its own decision and plan before it
enters this table.

### Resolved in the review

1. **Scope.** Workspace panes first; the other data surfaces are a later
   per-screen review. The confirm and the dock follow at 1.5 when that review
   reaches them.
2. **Unsupported characters.** One tofu per codepoint.
3. **Size column.** Numeric sizes stay white.
4. **Hardware fallback.** Wait for the PCH-1000; Spleen 5×8 at 2.0 plus accents
   is documented, not built.
5. **Icon family.** Heroicons for content, Bootstrap for the storage pair, three
   drawn shapes (gamepad, disc, cartridge).
6. **Plugin.** `cpu-chip`, one icon for `skprx`, `suprx`, and `prx`.
7. **Star.** Reserved for a future favorite; the batch mark stays the 6×6
   square.
8. **Rail.** Square and red risk at x 31; name x 42.
9. **Icon colour.** Folder, volume, and up white; files dim.
10. **Game and disc.** Drawn gamepad and drawn disc.
11. **Volumes.** Three classes: card (`ux0`, `imc0`), drive (`ur0`, `uma0`,
    `app0`), game card (`gro0`, `grw0`). The class is shape only.
12. **Extra categories.** None; PDFs, fonts, and VM disks stay in the current
    categories.
13. **Extensions.** Added `at9`, `vag`, `oma`, `pmf`, `pbp`, `chd`, `gdi`,
    `cdi`, `wbfs`, `wad`, `gcm`; `.bin` stays binary.
14. **Additions.** None in this pass.
15. **Baseline API.** A baseline-positioned entry point.
16. **Spleen provenance.** Vendored BDF plus a generator with `--check`.

### Deferred

- The star/favorite feature; the mask is not shipped.
- The confirm/dock type review and its mocks.
- The 1.5 hardware verdict; the Spleen 5×8 fallback.

### Verification Contract

**Host.** `./tools/test/run.sh` must be green after every unit: the layering
guard, the worker stack check, the asset guard, the art drift check, the docs
gate, and every suite. New coverage: the Spleen table and metrics, case, marks,
accents, tofu, truncation at 9 px, the reading baseline, the icon mapping, the
19 mask shapes, the mount mapping, the icon rail placement, and
the updated render traces.

**Visual.** 1:1 and 2× renders for the workspace (local, remote, selected, long
name, accented, CJK), the path line, every surface that takes the face, and the
icon rows (the seven mounts in three classes, folder, up, every category,
plugin, game, disc, exe, marked, red risk, selected). The screenshots live beside the plan
and in `docs/design/` once approved.

**Console.** One VPK, one run on the PCH-1000: the workspace names, the selected
row, a long name, an accented name, a CJK name, a path, the sites list, the
dock, a confirm, and the icon rows (the seven mounts in ALL DRIVES, folder, up,
every category, plugin, game, disc, exe, marked, red risk, selected). Record the
1.5 verdict, the icon legibility, the storage classes, and any softness in
`docs/VERIFICATION.md`. `[C]` claims stay in adapters; this is a rendering
change with no platform API.

**Release.** One uniquely named VPK, one artifact in `build/`, the unstripped
ELF kept. The project stays `building` until the console rows exist.

### Risks

| Risk | Mitigation |
|---|---|
| 1.5 reads soft for primary text on the panel | The U7 console check; Spleen 5×8 at 2.0 plus accents is the fallback (R21) |
| Spleen's lowercase is less legible at 12 px than the survey suggests | The 2× pane and the console run; Gohu 11 is the taller alternative |
| The lighter name weight beside the stencil sizes | Numeric sizes stay white; the 1× console run checks the balance |
| A second glyph table breaks the art generator's parser | KTD6: a unique marker and a 51-glyph assertion |
| The two faces are mixed by taste, screen by screen | Role rules R8/R11 and the U4 decision record |
| Licence or provenance drift | The vendored BDF, the licence file, and `THIRD_PARTY.md` |
| Scope creep in the additions | One unit per row, each with a screenshot and acceptance |
| The 31–39 rail crowds the name at x 42 | The U10 screenshot gate; the 1× console run |
| The deferred star leaves the batch mark as a square | A future favorite needs its own decision; the star is not shipped |
| The red risk diamond shares the failure colour | The diamond shape and the row context distinguish it; the palette docs record the change |
| The plugin chip reads as hardware, not a plugin | The puzzle alternatives are recorded; the chip is chosen for 16 px |
| A second icon family for storage devices looks mixed | Both families are filled 16 px and MIT; the ALL DRIVES contact sheet is the gate |
| The game-card cartridge is the only drawn storage icon | The 1× ALL DRIVES run; the cartridge shape is the same one used for the game class |
| Filled icons read heavier than the HUD's lines | Navigation white, files dim; the row render, not the strip |
| Mask provenance or a wrong icon file | Vendored SVGs, the conversion recipe, the MIT notice, and the mask shape tests |
| Doc conflicts with other active sessions | It landed on 2026-11-18 as its own commit; the other sessions were checked first |

### Sequencing

1. **Phase 0 — Brainstorm.** This document and the resolved decisions. No code.
2. **Phase 1 — Foundation.** U1-U2: the table, the licence, the face API, the
   metrics. The screen is unchanged.
3. **Phase 2 — Workspace.** U3: names and paths; the screenshot gate.
4. **Phase 3 — Surfaces and icons.** U4-U5, U8-U10: the review, UTF-8, the
   icon table, the mapping, the rail.
5. **Phase 4 — Docs and build.** U6-U7: the decision, DESIGN, licences, VPK,
   console.
6. **Phase 5 — Additions.** U11, only if the table gains a row.

The type and icon changes are one commit series; the additions do not ride on
it. The branch and the commit boundary are chosen when the plan is approved, so
the visual work stays independent of the cross-pane feature branch and the other
sessions.

### Documentation Duties

- **Decision entry:** the two faces and their roles; Spleen 6×12 at 1.5; the
  name's step and baseline; the fallback; the icon family, the 1-bit mask
  pipeline, and the x 42 rail. It supersedes only the single-face rule and the
  name x in `DESIGN.md`, not the palette, the ladder, or any screen.
- **`docs/DESIGN.md`:** the type constraint, the workspace row spec (name 1.5,
  top `y+10`, baseline 22), the icon column (16×16 at x 14, y+7), the rail at
  x 31, the name at x 42, the path line, and the surfaces that stay stencil.
- **`docs/design/`:** the sheet with the shipped stencil, the proof, the Spleen
  pane, the rejected faces, and the icon row set; the additions' screenshots.
- **`docs/THIRD_PARTY.md` and `LICENSES/`:** Spleen 6×12 (BSD-2-Clause),
  Heroicons 16/solid (MIT), and Bootstrap Icons (MIT), with versions, sources,
  the fourteen content files, the two storage files, and a note for the three
  drawn masks.
- **`PROJECTS.md`:** the board row and the log entry after every job.
- **`docs/FIXES.md`:** the case and mark failures as a fixed visual defect, with
  `HOST` status until the console run.
- **The plan:** this file, under `docs/plans/`; it is the source for U1-U11.

### What Not To Do

- Do not invent screen geometry or a new row height; `DESIGN.md` is law.
- Do not add a second writer, a global face, or a hidden render state.
- Do not mix faces by taste; role decides.
- Do not redraw the launcher art without an addition and a decision.
- Do not ship a truecolor PNG, an unlicensed font or icon, or an asset without
  a generator or a provenance note.
- Do not load an SVG, PNG, or texture at runtime for a row icon.
- Do not give file types their own colours.
- Do not mix families inside a role: content is Heroicons, storage is the
  Bootstrap card/SSD pair.
- Do not ship the star as the batch mark; it is a future favorite.
- Do not use yellow for the risk diamond; it is red.
- Do not ship the whole icon library; only the icons the mapping uses.
- Do not convert the 3.0+ readouts (address, site name, counts) to the reading
  face.
- Do not claim `verified` without the PCH-1000 run.
- Do not let a visual finding live only in chat.

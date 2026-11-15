# Launcher icon

**Date:** 2026-11-15
**Status:** settled (D84)
**Artifact:** `sce_sys/icon0.png` — 128×128, indexed 8-bit, 43 colours
**Previews:** [4× render](2026-11-15-launcher-icon.png),
[4× with the launcher mask](2026-11-15-launcher-icon-bubble.png)

## The problem

The Vita home screen puts `icon0.png` inside a bubble whose corners are cut.
Anything drawn to the edge — a panel, a border, a wordmark baseline — is clipped
and reads as a broken rectangle. The icon has to survive that mask and still
read at launcher size, where the bubble is only a few centimetres wide.

## The mark

**Two lanes, one transfer.** Two opposed arrows:

- the upper arrow is `C_YELLOW` and points right: network activity, the FTP lane;
- the lower arrow is `C_CYAN` and points left: the local lane and the cursor.

The meaning comes from the settled product shape — two live panes exchanging
files — not from decoration. A folder, a globe, or a terminal prompt would each
say less, and a wordmark would repeat the label the launcher already prints.

## Geometry

- Canvas 128×128, `C_FIELD` (true black), fully opaque.
- Mark bounding box x 29–99, y 34–93 (70×60), centred on (64, 64).
- Shaft 11 px; head 25 px tall, 19 px deep; 16 px gap between the arrows.
- No border, panel, rule, glow, gradient, or shadow.
- Colours are exactly `C_YELLOW` `#FCEE0A` and `C_CYAN` `#2AD8E8` from
  `src/gfx/theme.h`.
- Drawn at 8× and box-filtered to 128, then indexed to 43 colours so
  `tools/check_assets.sh` accepts it. The installer rejects truecolor art with
  `0x8010113D`.

## Mask check

The bubble preview applies a 26 px corner radius. Every point of the mark stays
more than 20 px inside a cut corner, and the 70 px width fits inside the 76 px
straight edge of the bubble at its vertical midpoint. No pixel of the mark is
clipped.

## Restraint

No red and no green. Those are reserved for failure and success / server ON, and
a launcher icon must never look like an alert. No animation and no glow; the
bubble supplies its own bevel and reflection. The mark is flat, like every other
element in the HUD.

## Changes

Any future revision keeps the centred 70×60 box and the palette. A change to the
mark or its meaning is a design decision and needs a superseding entry in
`docs/DECISIONS.md`.

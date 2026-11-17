#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the launcher and LiveArea art from the settled design.

The art is drawn here rather than in an image editor so it stays reproducible:
the palette comes from src/gfx/theme.h by hand, the typeface is read from the
app's own 5x7 glyph table in src/gfx/font.c, and every PNG is written as an
indexed 8-bit file because the Vita installer rejects truecolor art.

    tools/art/build_launcher_art.py
    tools/art/build_launcher_art.py --check   # fail if a PNG has drifted

Outputs:
    sce_sys/icon0.png                        128x128  launcher bubble
    sce_sys/pic0.png                         960x544  boot / loading frame
    sce_sys/livearea/contents/bg.png         840x500  LiveArea background
    sce_sys/livearea/contents/startup.png    280x158  gate thumbnail

Supersedes D84 (the two-arrow mark) per D101; the LiveArea composition is D102.
"""

from __future__ import annotations

import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Supersampling factor. Every shape is rasterised at 3x and box-filtered down,
# which is how the antialiased edges in the settled renders are produced.
S = 3

# src/gfx/theme.h, sRGB. Keep in sync; docs/DESIGN.md is the authority.
FIELD = (0x00, 0x00, 0x00)
PANEL = (0x0D, 0x0E, 0x12)
HAIR = (0x1C, 0x1D, 0x24)
DIM = (0x6E, 0x70, 0x7E)
WHITE = (0xED, 0xED, 0xF0)
YELLOW = (0xFC, 0xEE, 0x0A)
CYAN = (0x2A, 0xD8, 0xE8)

GLYPH_W = 5
GLYPH_H = 7
ADVANCE = 6


# ---------------------------------------------------------------- typeface

def load_glyphs() -> dict[str, list[int]]:
    """Read the app's 5x7 face so the art and the UI never drift apart."""
    src = (ROOT / "src/gfx/font.c").read_text()
    block = src.split("static const Glyph GLYPHS[] = {", 1)[1]
    entries = re.findall(r"\{'(\\?.)',\s*\{([^}]*)\}\}", block)
    glyphs: dict[str, list[int]] = {}
    for ch, rows in entries:
        if ch.startswith("\\"):
            ch = {"\\0": " "}.get(ch, ch[-1])
        values = [int(v.strip(), 16) for v in rows.split(",") if v.strip()]
        if len(values) == GLYPH_H:
            glyphs[ch] = values
    if not glyphs:
        raise SystemExit("could not read the glyph table from src/gfx/font.c")
    return glyphs


GLYPHS = load_glyphs()


# ---------------------------------------------------------------- canvas

class Canvas:
    """A float-coordinate RGB canvas rasterised at S x supersampling."""

    def __init__(self, w: int, h: int) -> None:
        self.w = w
        self.h = h
        self.buf = bytearray(w * S * h * S * 3)

    # -- primitives ------------------------------------------------------
    def _span(self, y: int, x0: int, x1: int, rgb: tuple[int, int, int]) -> None:
        if x1 <= x0:
            return
        row = y * self.w * S
        self.buf[(row + x0) * 3:(row + x1) * 3] = bytes(rgb) * (x1 - x0)

    def rect(self, x: float, y: float, w: float, h: float, rgb) -> None:
        x0 = max(0, int(round(x * S)))
        x1 = min(self.w * S, int(round((x + w) * S)))
        y0 = max(0, int(round(y * S)))
        y1 = min(self.h * S, int(round((y + h) * S)))
        for yy in range(y0, y1):
            self._span(yy, x0, x1, rgb)

    def poly(self, points: list[tuple[float, float]], rgb) -> None:
        """Even-odd scanline fill with pixel-centre coverage at S scale."""
        pts = [(px * S, py * S) for px, py in points]
        ys = [p[1] for p in pts]
        y0 = max(0, int(min(ys)))
        y1 = min(self.h * S, int(max(ys)) + 1)
        n = len(pts)
        for yy in range(y0, y1):
            cy = yy + 0.5
            xs: list[float] = []
            for i in range(n):
                ax, ay = pts[i]
                bx, by = pts[(i + 1) % n]
                if (ay <= cy < by) or (by <= cy < ay):
                    t = (cy - ay) / (by - ay)
                    xs.append(ax + t * (bx - ax))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                x0 = max(0, int(round(xs[i])))
                x1 = min(self.w * S, int(round(xs[i + 1])))
                self._span(yy, x0, x1, rgb)

    def chamfer(self, x: float, y: float, w: float, h: float, ch: float,
                rgb) -> None:
        """The HUD panel shape: one cut top-right corner."""
        self.poly([
            (x, y), (x + w - ch, y), (x + w, y + ch),
            (x + w, y + h), (x, y + h),
        ], rgb)

    # -- type ------------------------------------------------------------
    def glyph(self, ch: str, x: float, y: float, px: float, rgb) -> None:
        rows = GLYPHS.get(ch)
        if rows is None:
            return
        for r, bits in enumerate(rows):
            for c in range(GLYPH_W):
                if bits & (1 << (GLYPH_W - 1 - c)):
                    self.rect(x + c * px, y + r * px,
                              px + 0.02, px + 0.02, rgb)

    def text(self, s: str, x: float, y: float, px: float, rgb,
             track: float = 0.0) -> float:
        cx = x
        for ch in s:
            self.glyph(ch, cx, y, px, rgb)
            cx += (ADVANCE + track) * px
        return cx - x

    def text_width(self, s: str, px: float, track: float = 0.0) -> float:
        return (len(s) * (ADVANCE + track) - 1) * px

    def text_right(self, s: str, right: float, y: float, px: float,
                   rgb) -> None:
        self.text(s, right - self.text_width(s, px), y, px, rgb)

    def text_center(self, s: str, cx: float, y: float, px: float, rgb) -> None:
        self.text(s, cx - self.text_width(s, px) / 2, y, px, rgb)

    # -- the settled wordmark -------------------------------------------
    def wordmark(self, x: float, y: float, px: float) -> None:
        """Halo, then one chroma offset per side, then the solid face.

        The offsets and alphas mirror src/ui/header.c: halo at 10 %, yellow
        chroma at 45 % left, cyan at 35 % right, twice the px as the offset.
        """
        off = px * 0.5
        for dx, dy in ((-off, 0), (off, 0), (0, -off), (0, off)):
            self.text("VITAFTPSHELL", x + dx, y + dy, px,
                      mix(YELLOW, FIELD, 0.10))
        self.text("VITAFTPSHELL", x - off, y, px, mix(YELLOW, FIELD, 0.45))
        self.text("VITAFTPSHELL", x + off, y, px, mix(CYAN, FIELD, 0.35))
        self.text("VITAFTPSHELL", x, y, px, YELLOW)

    # -- output ----------------------------------------------------------
    def flatten(self) -> list[list[tuple[int, int, int]]]:
        w, h = self.w, self.h
        out: list[list[tuple[int, int, int]]] = []
        stride = w * S * 3
        for y in range(h):
            row: list[tuple[int, int, int]] = []
            for x in range(w):
                r = g = b = 0
                for sy in range(S):
                    base = (y * S + sy) * stride + x * S * 3
                    for sx in range(S):
                        i = base + sx * 3
                        r += self.buf[i]
                        g += self.buf[i + 1]
                        b += self.buf[i + 2]
                n = S * S
                row.append((r // n, g // n, b // n))
            out.append(row)
        return out


def mix(fg: tuple[int, int, int], bg: tuple[int, int, int],
        alpha: float) -> tuple[int, int, int]:
    return tuple(int(round(f * alpha + b * (1 - alpha))) for f, b in zip(fg, bg))


# ---------------------------------------------------------------- png

def encode_indexed(rows: list[list[tuple[int, int, int]]]) -> tuple[bytes, int]:
    """Encode an 8-bit indexed PNG (colour type 3); return bytes and palette size."""
    counts: dict[tuple[int, int, int], int] = {}
    for row in rows:
        for rgb in row:
            counts[rgb] = counts.get(rgb, 0) + 1

    ordered = sorted(counts.items(), key=lambda kv: -kv[1])
    palette = [rgb for rgb, _ in ordered[:256]]
    index = {rgb: i for i, rgb in enumerate(palette)}
    if len(ordered) > 256:
        # Fold the rare colours into their nearest kept neighbour. The art is
        # flat, so the rare colours are antialiased edges only.
        for rgb, _ in ordered[256:]:
            index[rgb] = min(
                range(256),
                key=lambda i: sum((a - b) ** 2
                                  for a, b in zip(rgb, palette[i])))

    raw = bytearray()
    for row in rows:
        raw.append(0)
        for rgb in row:
            raw.append(index[rgb])

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    w, h = len(rows[0]), len(rows)
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", bytes(v for rgb in palette for v in rgb))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    return bytes(png), len(palette)


# ---------------------------------------------------------------- the mark

# The mark is a stack of three sheets. The front sheet is the document: V T P S
# down the left margin, four ruled lines on the right. Geometry is in a 128 px
# box; the visible bbox is 83x83 around (56.5, 59.5).
def draw_mark(c: Canvas, cx: float, cy: float, h: float) -> None:
    s = h / 83.0
    ox = cx - 56.5 * s
    oy = cy - 59.5 * s

    def at(px: float, py: float) -> tuple[float, float]:
        return (ox + px * s, oy + py * s)

    def ch(a: float, b: float, w: float, hh: float, corner: float, rgb) -> None:
        px, py = at(a, b)
        c.chamfer(px, py, w * s, hh * s, corner * s, rgb)

    ch(38, 18, 60, 60, 12, DIM)
    ch(25, 25, 66, 66, 12, FIELD)
    ch(28, 28, 60, 60, 12, YELLOW)
    ch(15, 35, 66, 66, 12, FIELD)
    ch(18, 38, 60, 60, 12, CYAN)

    tx, ty = at(23, 42)
    advance = 13.5 * s
    for i, letter in enumerate("VTPS"):
        c.glyph(letter, tx, ty + i * advance, 1.6 * s, FIELD)

    for ly, w in ((46, 34), (60, 28), (74, 34), (88, 20)):
        lx, lpy = at(37, ly)
        c.rect(lx, lpy, w * s, 3.5 * s, FIELD)


# ---------------------------------------------------------------- assets

def icon() -> Canvas:
    c = Canvas(128, 128)
    c.rect(0, 0, 128, 128, FIELD)
    draw_mark(c, 64, 64, 83)
    return c


def livearea() -> Canvas:
    """840x500. Composition A: the workspace, the wordmark, the served drives.

    The shell lays its gate bar across the bottom of the page, so the lowest
    element sits at y 382 and everything below is field.
    """
    c = Canvas(840, 500)
    c.rect(0, 0, 840, 500, FIELD)

    c.wordmark(48, 30, 7)
    c.text("FTP CLIENT / FILE MANAGER / ANONYMOUS SERVER", 48, 88, 2.5, DIM)
    c.rect(48, 114, 744, 1, HAIR)

    def pane(x: int, accent, title: str, cursor: int) -> None:
        outer = [(x, 140), (x + 346, 140), (x + 360, 154),
                 (x + 360, 352), (x, 352)]
        inner = [(x + 2, 142), (x + 344, 142), (x + 358, 156),
                 (x + 358, 350), (x + 2, 350)]
        c.poly(outer, mix(accent, FIELD, 0.55))
        c.poly(inner, FIELD)

        c.text(title, x + 14, 152, 3, accent)
        c.rect(x + 14, 184, 332, 1, HAIR)
        for i in range(5):
            ry = 198 + i * 29
            if i == cursor:
                c.rect(x + 1, ry - 5, 358, 26, CYAN)
                c.rect(x + 14, ry + 1, 150, 9, FIELD)
                c.rect(x + 306, ry + 1, 40, 9, FIELD)
            else:
                c.rect(x + 14, ry, 120 + (i * 37) % 150, 9, HAIR)
                c.rect(x + 306, ry, 40, 9, HAIR)

    pane(48, CYAN, "LOCAL", 2)
    pane(432, YELLOW, "REMOTE", -1)

    c.text("UX0:  UR0:  UMA0:", 48, 370, 3, DIM)
    c.text_right("READ-WRITE", 792, 370, 3, YELLOW)
    return c


def startup() -> Canvas:
    """280x158. The gate thumbnail: mark, wordmark, one line."""
    c = Canvas(280, 158)
    c.rect(0, 0, 280, 158, FIELD)
    draw_mark(c, 140, 56, 84)
    c.text_center("VITAFTPSHELL", 140, 100, 2.2, YELLOW)
    c.text_center("FTP CLIENT AND SERVER", 140, 124, 1.5, DIM)
    return c


def pic0() -> Canvas:
    """960x544. The boot frame: the mark and the wordmark, nothing invented."""
    c = Canvas(960, 544)
    c.rect(0, 0, 960, 544, FIELD)
    draw_mark(c, 480, 240, 200)
    c.wordmark(480 - c.text_width("VITAFTPSHELL", 6) / 2, 370, 6)
    c.text_center("FTP CLIENT / FILE MANAGER / ANONYMOUS SERVER",
                  480, 442, 2.5, DIM)
    return c


ASSETS = [
    ("sce_sys/icon0.png", icon),
    ("sce_sys/pic0.png", pic0),
    ("sce_sys/livearea/contents/bg.png", livearea),
    ("sce_sys/livearea/contents/startup.png", startup),
]


def main() -> int:
    check = "--check" in sys.argv[1:]
    drift: list[str] = []
    for rel, build in ASSETS:
        path = ROOT / rel
        data, colours = encode_indexed(build().flatten())
        if check:
            if not path.exists() or path.read_bytes() != data:
                drift.append(rel)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f"{rel}: indexed, {colours} colours")
    if not check:
        return 0
    if drift:
        print("art drift: " + ", ".join(drift), file=sys.stderr)
        print("  run tools/art/build_launcher_art.py and commit the PNGs",
              file=sys.stderr)
        return 1
    print("art guard: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

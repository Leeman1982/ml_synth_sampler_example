#!/usr/bin/env python3
"""
Regenerates ui_font_knighthawks.h from the "Knighthawks" TrueType font.

Usage:
    pip install freetype-py
    # place your own copy of Knighth.ttf (see doc/branding.md) next to this
    # script, or pass its path as the first argument
    python3 gen_ui_titles.py [path/to/Knighth.ttf] > ../ui_font_knighthawks.h

Add new strings to TITLE_STRINGS / SPLASH_LINES below as needed; anything
wider than MAX_TITLE_W (at TITLE_PX) is silently skipped and falls back to
the normal readable font at runtime instead of aborting the build.
"""
import sys
import freetype

FONT_PATH = sys.argv[1] if len(sys.argv) > 1 else "Knighth.ttf"

TITLE_PX = 9
MAX_TITLE_W = 124

TITLE_STRINGS = [
    "ML Sampler", "Menu", "Load Sample", "Sampler", "Effects", "System",
    "Tune Coarse", "Hold", "Release", "Release (Smpl)", "Delay Level",
    "Delay Feedback", "Delay Length", "LFO Speed", "Phaser Depth", "Phaser G",
    "Vibrato Depth", "Vibrato Intens.", "Pitch Speed", "Pitch Mix",
    "Pitch Feedback", "Tremolo Depth", "Reverb Level", "Input Gain",
    "Load WAV->AllKeys", "Load SF2 Samples", "Load SF2 Instr.", "Load Full SF2",
    "Pin Info", "Memory Info",
]

# (text, pixel size) - individually chosen so each line fits within 124px
SPLASH_LINES = [
    ("O.C.P", 20),
    ("DELTA CITY", 12),
    ("MK1", 20),
    ("by ZOMBI SS", 12),
]


def face_at(px):
    face = freetype.Face(FONT_PATH)
    face.set_pixel_sizes(0, px)
    return face


def glyph_metrics(face, text):
    pen = 0
    top = 0
    bottom = 0
    glyphs = []
    for ch in text:
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        g = face.glyph
        bmp = g.bitmap
        glyphs.append((pen + g.bitmap_left, g.bitmap_top, bmp.width, bmp.rows, bytes(bmp.buffer), bmp.pitch))
        top = max(top, g.bitmap_top)
        bottom = max(bottom, bmp.rows - g.bitmap_top)
        pen += g.advance.x >> 6
    return pen, top, bottom, glyphs


def pack_xbm(width, height, top, glyphs):
    if width <= 0 or height <= 0:
        return b""
    bytes_per_row = (width + 7) // 8
    out = bytearray(bytes_per_row * height)
    for (gx, gtop, gw, rows, buf, pitch) in glyphs:
        gy0 = top - gtop
        for ry in range(rows):
            for rx in range(gw):
                if buf[ry * pitch + rx] > 110:
                    x = gx + rx
                    y = gy0 + ry
                    if 0 <= x < width and 0 <= y < height:
                        out[y * bytes_per_row + (x // 8)] |= (1 << (x % 8))
    return bytes(out)


def c_ident(text):
    out = []
    for ch in text:
        out.append(ch.lower() if ch.isalnum() else '_')
    ident = "".join(out).strip('_')
    while '__' in ident:
        ident = ident.replace('__', '_')
    return ident


def main():
    # --- titles: shared baseline/height, skip strings too wide to fit ---
    face = face_at(TITLE_PX)
    measured = {}
    global_top = 0
    global_bottom = 0
    for s in TITLE_STRINGS:
        pen, top, bottom, glyphs = glyph_metrics(face, s)
        measured[s] = (pen, top, bottom, glyphs)
        if pen <= MAX_TITLE_W:
            global_top = max(global_top, top)
            global_bottom = max(global_bottom, bottom)

    title_height = global_top + global_bottom
    title_entries = []
    skipped = []
    for s in TITLE_STRINGS:
        pen, top, bottom, glyphs = measured[s]
        if pen > MAX_TITLE_W:
            skipped.append((s, pen))
            continue
        data = pack_xbm(pen, title_height, global_top, glyphs)
        title_entries.append((s, pen, title_height, data))

    # --- splash lines: each its own size, own tight crop ---
    splash_entries = []
    for (s, px) in SPLASH_LINES:
        f = face_at(px)
        pen, top, bottom, glyphs = glyph_metrics(f, s)
        h = top + bottom
        data = pack_xbm(pen, h, top, glyphs)
        splash_entries.append((s, pen, h, data))

    lines = []
    lines.append("/*")
    lines.append(" * Auto-generated 1bpp XBM glyph bitmaps rasterized from the \"Knighthawks\"")
    lines.append(" * TrueType font (c) 1999 by ck! [Freaky Fonts], http://www.freakyfonts.de -")
    lines.append(" * personal/non-commercial use only. See doc/branding.md for attribution and")
    lines.append(" * regeneration instructions. Do not hand-edit this file.")
    lines.append(" */")
    lines.append("#ifndef UI_FONT_KNIGHTHAWKS_H_")
    lines.append("#define UI_FONT_KNIGHTHAWKS_H_")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("#include <Arduino.h>")
    lines.append("")

    lines.append(f"#define UI_TITLE_GFX_HEIGHT {title_height}")
    lines.append("")
    for (s, w, h, data) in title_entries:
        ident = f"ui_title_{c_ident(s)}"
        hexb = ", ".join(f"0x{b:02x}" for b in data)
        lines.append(f"static const uint8_t {ident}_bits[] PROGMEM = {{{hexb}}}; /* \"{s}\" {w}x{h} */")
    lines.append("")

    lines.append("struct UiTitleGfx { const char *text; const uint8_t *bits; uint8_t width; uint8_t height; };")
    lines.append("")
    lines.append("static const UiTitleGfx ui_titleGfxTable[] =")
    lines.append("{")
    for (s, w, h, data) in title_entries:
        ident = f"ui_title_{c_ident(s)}"
        esc = s.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'    {{ "{esc}", {ident}_bits, {w}, {h} }},')
    lines.append("};")
    lines.append(f"#define UI_TITLE_GFX_COUNT ({len(title_entries)})")
    lines.append("")

    for (s, w, h, data) in splash_entries:
        ident = f"ui_splash_{c_ident(s)}"
        hexb = ", ".join(f"0x{b:02x}" for b in data)
        lines.append(f"static const uint8_t {ident}_bits[] PROGMEM = {{{hexb}}}; /* \"{s}\" {w}x{h} */")
    lines.append("")

    lines.append("struct UiSplashLine { const uint8_t *bits; uint8_t width; uint8_t height; };")
    lines.append("")
    lines.append("static const UiSplashLine ui_splashLines[] =")
    lines.append("{")
    for (s, w, h, data) in splash_entries:
        ident = f"ui_splash_{c_ident(s)}"
        lines.append(f"    {{ {ident}_bits, {w}, {h} }}, /* \"{s}\" */")
    lines.append("};")
    lines.append(f"#define UI_SPLASH_LINE_COUNT ({len(splash_entries)})")
    lines.append("")
    lines.append("#endif /* UI_FONT_KNIGHTHAWKS_H_ */")

    total = sum(len(d) for (_, _, _, d) in title_entries) + sum(len(d) for (_, _, _, d) in splash_entries)
    sys.stderr.write(f"title_height={title_height} entries={len(title_entries)} skipped={skipped}\n")
    sys.stderr.write(f"splash entries={[(s,w,h) for (s,w,h,_) in splash_entries]}\n")
    sys.stderr.write(f"total glyph bytes={total}\n")

    print("\n".join(lines))


if __name__ == "__main__":
    main()

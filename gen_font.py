#!/usr/bin/env python3
from PIL import Image, ImageFont, ImageDraw

FONTS = [
    ("FONT_XXL", "/usr/share/fonts/truetype/lato/Lato-Bold.ttf", 54),
    ("FONT_XL",  "/usr/share/fonts/truetype/lato/Lato-Bold.ttf", 34),
    ("FONT_LG",  "/usr/share/fonts/truetype/lato/Lato-Bold.ttf", 22),
    ("FONT_MD",  "/usr/share/fonts/truetype/lato/Lato-Semibold.ttf", 17),
    ("FONT_SM",  "/usr/share/fonts/truetype/lato/Lato-Regular.ttf", 14),
]

FIRST_CHAR = 32
LAST_CHAR = 126

out_lines = [
    "#pragma once",
    "#include <cstdint>",
    "",
    "struct GlyphInfo {",
    "    int width;",
    "    int height;",
    "    int offset_x;",
    "    int offset_y;",
    "    int advance;",
    "    uint32_t data_offset;",
    "};",
    "",
    "struct EmbeddedFont {",
    "    int size_px;",
    "    int line_height;",
    "    int ascent;",
    "    GlyphInfo glyphs[95];",
    "    const uint8_t* bitmap;",
    "};",
    ""
]

for name, path, size in FONTS:
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    bitmap_bytes = bytearray()
    glyph_entries = []

    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = chr(code)
        advance = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch)
        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            glyph_entries.append((0, 0, 0, 0, advance, len(bitmap_bytes)))
            continue
        x0, y0, x1, y1 = bbox
        w = x1 - x0
        h = y1 - y0
        img = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((-x0, -y0), ch, fill=255, font=font)
        raw = img.tobytes()
        data_offset = len(bitmap_bytes)
        bitmap_bytes.extend(raw)
        glyph_entries.append((w, h, x0, y0, advance, data_offset))

    out_lines.append(f"static const uint8_t {name}_BITMAP[{len(bitmap_bytes)}] = {{")
    row = []
    for i, b in enumerate(bitmap_bytes):
        row.append(f"{b}")
        if len(row) == 24 or i == len(bitmap_bytes) - 1:
            out_lines.append("    " + ",".join(row) + ",")
            row = []
    out_lines.append("};")
    out_lines.append("")
    out_lines.append(f"static const EmbeddedFont {name} = {{")
    out_lines.append(f"    {size}, {line_height}, {ascent},")
    out_lines.append("    {")
    for w, h, ox, oy, adv, off in glyph_entries:
        out_lines.append(f"        {{{w}, {h}, {ox}, {oy}, {adv}, {off}u}},")
    out_lines.append("    },")
    out_lines.append(f"    {name}_BITMAP")
    out_lines.append("};")
    out_lines.append("")

with open("/home/noa/Documents/reactinspeedtest/embedded_font.hpp", "w") as f:
    f.write("\n".join(out_lines))

print("Generated embedded_font.hpp successfully.")

#!/usr/bin/env python3
"""Turns the .ppm files into masked and unmasked PNGs, a labelled contact sheet and an index table.
Usage: compose.py WORK_DIR OUT_DIR. Needs Pillow."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

def font(size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    try:
        return ImageFont.load_default(size)
    except TypeError:
        return ImageFont.load_default()

def mask_img():
    s = 4
    m = Image.new("L", (480 * s, 480 * s), 0)
    ImageDraw.Draw(m).ellipse((0, 0, 480 * s - 1, 480 * s - 1), fill=255)
    return m.resize((480, 480), Image.LANCZOS)

def main(work, out):
    shots = json.load(open(os.path.join(work, "shots.json")))
    os.makedirs(os.path.join(out, "unmasked"), exist_ok=True)
    m = mask_img()
    black = Image.new("RGB", (480, 480), (0, 0, 0))
    masked = []
    for s in shots:
        im = Image.open(os.path.join(work, "out", s["name"] + ".ppm")).convert("RGB")
        im.save(os.path.join(out, "unmasked", s["name"] + ".png"))
        mi = Image.composite(im, black, m)
        mi.save(os.path.join(out, s["name"] + ".png"))
        masked.append(mi)
    # contact sheet
    cols, th, pad, lab = 6, 240, 12, 44
    rows = (len(shots) + cols - 1) // cols
    W = cols * (th + pad) + pad
    H = rows * (th + lab + pad) + pad
    sheet = Image.new("RGB", (W, H), (28, 28, 32))
    d = ImageDraw.Draw(sheet)
    f = font(11)
    for i, (s, im) in enumerate(zip(shots, masked)):
        x = pad + (i % cols) * (th + pad)
        y = pad + (i // cols) * (th + lab + pad)
        sheet.paste(im.resize((th, th), Image.LANCZOS), (x, y))
        name = s["name"]
        parts = [name[:34], name[34:68]]
        for k, t in enumerate(p for p in parts if p):
            d.text((x, y + th + 4 + k * 14), t, fill=(220, 220, 224), font=f)
        if s.get("note") == "night":
            d.text((x, y + th + 4 + 28), "(dimmed on device; shown bright)", fill=(255, 176, 0), font=f)
    sheet.save(os.path.join(out, "00-contact-sheet.png"))
    # index table
    with open(os.path.join(out, "index.md"), "w") as ix:
        ix.write("# CrowPanel 2.1 screen previews\n\nRendered by preview/ (real panel_*.h code, LVGL 9.5, RGB565). "
                 "Round-masked PNGs are in this folder, unmasked copies in unmasked/. Sample data is made up.\n\n")
        ix.write("| file | state shown |\n|---|---|\n")
        ix.write("| 00-contact-sheet.png | every screen, labelled |\n")
        for s in shots:
            extra = " (night: real backlight would be 5%, shown at normal brightness)" if s.get("note") == "night" else ""
            ix.write("| %s.png | %s%s |\n" % (s["name"], s["desc"], extra))
    print("wrote", len(shots), "pngs")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])

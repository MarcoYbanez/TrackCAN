#!/usr/bin/env python3
"""Extract the layout baked into the SquareLine background image into a JSON file.

The background (UI/DigitalDash/assets/Gauge_Status/dash_bg2.png, 480x272) holds the OFF state of the
RPM bars, the printed captions, the scale digits, the lamp outlines and the boost track. If it is
deleted from the SquareLine project, this file keeps what is needed to rebuild it, and the dash
firmware can read the segment geometry from it.

Needs Pillow. Run:  python3 -I scripts/extract_dash_layout.py
"""
import json
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BG = ROOT / "UI/DigitalDash/assets/Gauge_Status/dash_bg2.png"
OUT = ROOT / "UI/DigitalDash/dash_bg_layout.json"
THRESH = 22  # brightness above the (near black) background counts as ink


def components(img):
    """Connected components (8-neighbour) of non-background pixels: list of dicts."""
    w, h = img.size
    px = img.load()
    seen = [[False] * w for _ in range(h)]
    out = []
    for y in range(h):
        for x in range(w):
            if seen[y][x] or max(px[x, y][:3]) <= THRESH:
                continue
            stack = [(x, y)]
            seen[y][x] = True
            pts = []
            while stack:
                cx, cy = stack.pop()
                pts.append((cx, cy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and max(px[nx, ny][:3]) > THRESH:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            col = Counter(px[p][:3] for p in pts).most_common(1)[0][0]
            out.append({"x": min(xs), "y": min(ys), "w": max(xs) - min(xs) + 1, "h": max(ys) - min(ys) + 1,
                        "pixels": len(pts), "colour": "#%02X%02X%02X" % col})
    return out


def group_words(items, gap=7):
    """Merge letters on the same row that are close together into words (left to right)."""
    items = sorted(items, key=lambda c: (c["y"] // 6, c["x"]))
    words = []
    for c in items:
        for wd in words:
            same_row = abs(wd["y"] - c["y"]) <= 4
            near = 0 <= c["x"] - (wd["x"] + wd["w"]) <= gap
            if same_row and near:
                x2 = max(wd["x"] + wd["w"], c["x"] + c["w"])
                y1, y2 = min(wd["y"], c["y"]), max(wd["y"] + wd["h"], c["y"] + c["h"])
                wd.update(w=x2 - wd["x"], y=y1, h=y2 - y1, letters=wd["letters"] + 1)
                break
        else:
            words.append({"x": c["x"], "y": c["y"], "w": c["w"], "h": c["h"], "colour": c["colour"], "letters": 1})
    return sorted(words, key=lambda d: (d["y"], d["x"]))


def main():
    img = Image.open(BG).convert("RGBA")
    comps = components(img)
    bg_colour = "#%02X%02X%02X" % Counter(img.getdata()).most_common(1)[0][0][:3]

    top_bar = sorted([c for c in comps if 20 <= c["w"] <= 32 and c["h"] <= 12 and c["y"] < 20], key=lambda c: c["x"])
    main_bar = sorted([c for c in comps if 4 <= c["w"] <= 9 and 30 <= c["h"] <= 45], key=lambda c: c["x"])
    lamps = sorted([c for c in comps if 14 <= c["w"] <= 26 and 10 <= c["h"] <= 18 and 66 <= c["y"] <= 100], key=lambda c: c["x"])
    boost_track = [c for c in comps if c["w"] >= 120 and c["h"] <= 14 and c["y"] > 150]
    used = set(map(id, top_bar + main_bar + lamps + boost_track))
    rest = [c for c in comps if id(c) not in used]
    digits = sorted([c for c in rest if 60 <= c["y"] <= 72 and c["h"] <= 10 and c["w"] <= 8], key=lambda c: c["x"])
    others = [c for c in rest if id(c) not in set(map(id, digits))]
    words = group_words(others)

    # Names of the caption groups, read from the image by eye (position -> text). The first group is the
    # 1 px screen border, not text.
    names = {(412, 85): "RPM", (10, 99): "SPEED", (308, 99): "BOOST", (308, 158): "BAR", (10, 209): "MPH"}
    captions, frame = [], None
    for wd in words:
        key = (wd["x"], wd["y"])
        if wd["w"] >= 400:
            frame = wd
        elif key in names:
            captions.append(dict(wd, text=names[key]))
        else:
            captions.append(dict(wd, text="?"))

    # Lit colour of every segment, sampled from the fully lit source frames (same segment geometry).
    def lit_colours(path, segs, y_off):
        im = Image.open(path).convert("RGBA")
        bg = Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)), im)
        out = []
        for sg in segs:
            c = bg.getpixel((min(im.size[0] - 1, sg["x"] - 10 + sg["w"] // 2), min(im.size[1] - 1, sg["h"] // 2)))
            out.append("#%02X%02X%02X" % c[:3])
        return out

    def zone(hexc):
        r, g, b = int(hexc[1:3], 16), int(hexc[3:5], 16), int(hexc[5:7], 16)
        return "red" if r > 200 and g < 100 else "amber" if r > 200 and g > 120 else "cyan"

    main_lit = lit_colours(ROOT / "UI/DigitalDash/assets/Main_Tach/MainTachometer00060.png", main_bar, 20)
    top_lit = lit_colours(ROOT / "UI/DigitalDash/assets/Top_Tach/TopTach00001.png", top_bar, 8)
    for sg, c in zip(main_bar, main_lit):
        sg["lit_colour"], sg["zone"] = c, zone(c)
    for sg, c in zip(top_bar, top_lit):
        sg["lit_colour"], sg["zone"] = c, zone(c)

    # Exact lit pixels: every lit segment is a vertical stripe pattern (rows are identical), so the colour of
    # each column is enough to redraw it pixel for pixel. Columns start at x + lit_x0; the black gap column is
    # not stored. Sampled from the fully lit frames.
    def lit_columns(path, segs, y0):
        im = Image.open(path).convert("RGBA")
        flat = Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)), im).convert("RGB")
        for sg in segs:
            row_y = y0 + sg["h"] // 2 - sg["y"] if False else sg["h"] // 2
            cols = []
            for fx in range(sg["x"] - 10 - 2, sg["x"] - 10 + sg["w"] + 3):
                cols.append((fx, flat.getpixel((fx, row_y)) if 0 <= fx < im.size[0] else (0, 0, 0)))
            lit = [i for i, (_, c) in enumerate(cols) if max(c) > 8]
            # keep the run of lit columns that overlaps the segment, drop neighbours' pixels
            mid = sg["w"] // 2 + 2
            run = [i for i in lit if abs(i - mid) <= sg["w"] // 2 + 1]
            if not run:
                sg["lit_x0"], sg["lit_columns"] = 0, []
                continue
            a, b = min(run), max(run)
            sg["lit_x0"] = cols[a][0] + 10 - sg["x"]
            sg["lit_columns"] = ["#%02X%02X%02X" % cols[i][1] for i in range(a, b + 1)]
            # A dim one pixel baseline sits under every lit segment (row y + h), one extra column wide.
            base_y = im.size[1] - 1
            sg["lit_base_row"] = ["#%02X%02X%02X" % flat.getpixel((min(im.size[0] - 1, cols[i][0]), base_y))
                                  for i in range(a, b + 2)]

    lit_columns(ROOT / "UI/DigitalDash/assets/Main_Tach/MainTachometer00060.png", main_bar, 20)
    lit_columns(ROOT / "UI/DigitalDash/assets/Top_Tach/TopTach00001.png", top_bar, 8)

    layout = {
        "source": str(BG.relative_to(ROOT)),
        "size": list(img.size),
        "background_colour": bg_colour,
        "note": "Coordinates are pixels from the top-left of the 480x272 screen. Colours are the OFF (unlit) "
                "colours baked into the image. Words are named by reading the image: see 'captions'.",
        "top_bar_off_segments": top_bar,
        "main_bar_off_segments": main_bar,
        "lamp_outlines": lamps,
        "scale_digits_left_to_right": digits,
        "boost_track": boost_track,
        "captions": captions,
        "screen_border": frame,
        "zone_counts": {
            "main_bar": dict(Counter(sg["zone"] for sg in main_bar)),
            "top_bar": dict(Counter(sg["zone"] for sg in top_bar)),
        },
    }
    OUT.write_text(json.dumps(layout, indent=1) + "\n")
    print("top bar segments :", len(top_bar), " main bar segments:", len(main_bar), " lamps:", len(lamps),
          " scale digits:", len(digits), " boost track:", len(boost_track), " captions:", len(captions))
    print("wrote", OUT.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

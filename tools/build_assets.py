# ps5-dualsense-overlay - controller artwork pipeline
# Copyright (C) 2026 erickdavestech
# SPDX-License-Identifier: GPL-3.0-or-later

"""Genera los assets del overlay del mando a partir del pack de Zacksly (CC BY 3.0).

Entrada:  assets/source/            SVG originales del pack, sin tocar
Salida:   assets/png/{1280,640,320}/ base + sprites con transparencia
          assets/layout.json         posiciones en unidades de un lienzo de 640x480
          assets/layout.js           lo mismo, para preview.html (file:// no deja hacer fetch)
          assets/preview.png         composición de ejemplo hecha SOLO con layout.json + sprites

El SVG del mando no trae ids: cada pieza se identifica por su índice en el documento (tabla E).
Requiere Python 3 + Pillow y Google Chrome, que se usa headless para medir y rasterizar.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
SRC = ASSETS / "source"
PNG = ASSETS / "png"
CHROME = os.environ.get("CHROME", r"C:\Program Files\Google\Chrome\Application\chrome.exe")

CROP = (1025, 228, 2048, 1536)
W, H = 640, 480
SIZES = {"1280": 2.0, "640": 1.0, "320": 0.5}
DSF = 4
GLOW_M = 12
TRAVEL = 12

ACCENT = "#2F7BFF"
MUTE_ON = "#FF8A3D"
FACE = {"triangle": "#2BD99F", "circle": "#FF4F5E", "cross": "#4C8DFF", "square": "#F06FD6"}

E = {
    "body": 0, "lightbar": 1, "touchpad": 2, "wing_r": 3, "wing_l": 4,
    "dpad_up": 5, "dpad_down": 6, "dpad_left": 7, "dpad_right": 8,
    "cap_l": 9, "cap_r": 10,
    "btn_triangle": 11, "btn_circle": 12, "btn_square": 13,
    "glyph_triangle": 14, "glyph_square": 15, "btn_cross": 16,
    "glyph_cross_a": 17, "glyph_cross_b": 18, "glyph_circle": 19,
    "l1": 20, "r1": 21, "create": 22, "options": 23, "mute": 24,
    "cap_ring_l": 25, "cap_ring_r": 26,
    "ps_a": 37, "ps_b": 38, "ps_c": 39,
    "arrow_down": 47, "arrow_up": 48, "arrow_right": 49, "arrow_left": 50,
}
CAPS = {E["cap_l"], E["cap_r"], E["cap_ring_l"], E["cap_ring_r"]}

PAD_BITS = {
    "l3": 0x0002, "r3": 0x0004, "options": 0x0008,
    "dpad_up": 0x0010, "dpad_right": 0x0020, "dpad_down": 0x0040, "dpad_left": 0x0080,
    "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800,
    "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
    "touchpad": 0x100000, "create": 0x1, "ps": None, "mute": None,
}

SHAPE_RE = re.compile(r"<(?:path|circle|line|polygon|rect|ellipse)\b[^>]*?/>", re.S)

ctrl_src = (SRC / "Solid Color SVG.svg").read_text(encoding="utf-8")
DEFS = re.search(r"<defs>.*?</defs>", ctrl_src, re.S).group(0)
SHAPES = SHAPE_RE.findall(ctrl_src)
assert len(SHAPES) == 52, f"el SVG del mando cambió: {len(SHAPES)} formas"

def attr(i, name):
    return float(re.search(rf'\s{name}="([-\d.]+)"', SHAPES[i]).group(1))

def shape(i, style=None, id=None):
    """Forma original; con style se le quita la clase para que el estilo inline mande."""
    s = SHAPES[i]
    if style:
        s = re.sub(r'\sclass="[^"]*"', "", s)
    extra = (f' id="{id}"' if id else "") + (f' style="{style}"' if style else "")
    return re.sub(r"^<(\w+)", lambda m: m.group(0) + extra, s, count=1)

def solid(color, width=7):
    return f"fill:{color};stroke:{color};stroke-width:{width}px"

def glow(inner):
    return f'<g filter="url(#fx-glow)">{inner}</g>'

BADGE = 270
BADGE_BOTTOM = 438
SHOULDER_CX = {"l": 1435.5, "r": 2659.5}

def icon(name, styles):
    s = (SRC / name).read_text(encoding="utf-8")
    out = []
    for p in re.findall(r"<path\b[^>]*?/>", s, re.S):
        cls = re.search(r'class="([^"]+)"', p).group(1)
        out.append(re.sub(r'\sclass="[^"]*"', "", p).replace("<path", f'<path style="{styles[cls]}"', 1))
    return "".join(out)

def badge(side, mode):
    x, y = SHOULDER_CX[side] - BADGE / 2, BADGE_BOTTOM - 415 / 512 * BADGE
    letter = side.upper() + "2"
    if mode == "idle":
        fill, ink = "fill:#14151A;fill-opacity:.88", "#E9EAEE"
    else:
        fill, ink = f"fill:{ACCENT}", "#FFFFFF"
    body = icon(f"Full Solid {letter}.svg", {"cls-1": fill})
    body += icon(f"Outline {letter}.svg", {"cls-1": f"fill:{ink}",
                                           "cls-2": f"fill:none;stroke:{ink};stroke-miterlimit:10;stroke-width:24px"})
    return f'<svg x="{x:.1f}" y="{y:.1f}" width="{BADGE}" height="{BADGE}" viewBox="0 0 512 512" overflow="visible">{body}</svg>'

CAP_C = {s: (attr(E[f"cap_{s}"], "cx"), attr(E[f"cap_{s}"], "cy")) for s in "lr"}
CAP_R = attr(E["cap_l"], "r")

def wells():
    return "".join(f'<circle cx="{x}" cy="{y}" r="{CAP_R + 3}" style="fill:#040404;stroke:#2B2B2E;stroke-width:8px"/>'
                   for x, y in CAP_C.values())

def cap(side="l", pressed=False):
    g = shape(E[f"cap_{side}"]) + shape(E[f"cap_ring_{side}"])
    if pressed:
        g += glow(shape(E[f"cap_ring_{side}"], style=f"fill:none;stroke:{ACCENT};stroke-width:14px")
                  + shape(E[f"cap_{side}"], style=f"fill:none;stroke:{ACCENT};stroke-width:8px"))
    return f'<g filter="url(#fx-capshadow)">{g}</g>'

def controller(with_caps):
    parts = [badge("l", "idle"), badge("r", "idle")]
    body = []
    for i in range(len(SHAPES)):
        if i == E["cap_l"]:
            body.append(wells())
        if i not in CAPS:
            body.append(shape(i))
    parts.append(f'<g filter="url(#fx-shadow)">{"".join(body)}</g>')
    if with_caps:
        parts += [cap("l"), cap("r")]
    return "".join(parts)

def face(name):
    glyph = {"triangle": [E["glyph_triangle"]], "circle": [E["glyph_circle"]],
             "square": [E["glyph_square"]], "cross": [E["glyph_cross_a"], E["glyph_cross_b"]]}[name]
    ink = "fill:none;stroke:#fff;stroke-width:12px;stroke-linecap:round;stroke-linejoin:round"
    return glow(shape(E[f"btn_{name}"], style=solid(FACE[name]))) + "".join(shape(g, style=ink) for g in glyph)

def dpad(d):
    return glow(shape(E[f"dpad_{d}"], style=solid(ACCENT))) + shape(E[f"arrow_{d}"], style="fill:#fff")

TP_IDX = E["touchpad"]

PRESSED = {
    **{n: face(n) for n in FACE},
    **{f"dpad_{d}": dpad(d) for d in ("up", "down", "left", "right")},
    "l1": glow(shape(E["l1"], style=solid(ACCENT))),
    "r1": glow(shape(E["r1"], style=solid(ACCENT))),
    "create": glow(shape(E["create"], style=solid(ACCENT))),
    "options": glow(shape(E["options"], style=solid(ACCENT))),
    "mute": glow(shape(E["mute"], style=solid(MUTE_ON))),
    "ps": glow("".join(shape(E[k], style=solid(ACCENT, 4)) for k in ("ps_a", "ps_b", "ps_c"))),
    "touchpad": glow(shape(TP_IDX, style=f"fill:{ACCENT};fill-opacity:.35;stroke:{ACCENT};stroke-width:10px")),
}
OTHER = {
    "stick_cap": cap("l"),
    "stick_cap_l3": cap("l", pressed=True),
    "l2_fill": badge("l", "fill"),
    "r2_fill": badge("r", "fill"),
    "touch_dot": glow('<circle cx="2048" cy="674" r="26" style="fill:%s;stroke:#fff;stroke-width:7px"/>' % ACCENT),
}

FULL = f'filterUnits="userSpaceOnUse" x="0" y="0" width="4096" height="2160" color-interpolation-filters="sRGB"'
EXTRA_DEFS = f"""<defs>
<filter id="fx-glow" {FULL}><feGaussianBlur in="SourceGraphic" stdDeviation="12" result="b"/>
 <feMerge><feMergeNode in="b"/><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="fx-shadow" {FULL}><feDropShadow dx="0" dy="10" stdDeviation="14" flood-color="#000" flood-opacity=".55"/></filter>
<filter id="fx-capshadow" {FULL}><feDropShadow dx="0" dy="8" stdDeviation="8" flood-color="#000" flood-opacity=".7"/></filter>
</defs>"""

def svg_doc(body):
    x, y, w, h = CROP
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="{x} {y} {w} {h}">'
            f"{DEFS}{EXTRA_DEFS}{body}</svg>")

def html(body, script=""):
    return ("<!doctype html><html><head><meta charset='utf-8'><style>html,body{margin:0;background:transparent}"
            f"svg{{display:block}}</style></head><body>{svg_doc(body)}<pre id='out'></pre>"
            f"<script>{script}</script></body></html>")

def chrome(args, workdir):
    cmd = [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
           "--no-default-browser-check", f"--user-data-dir={workdir / 'profile'}"] + args
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=120)

def measure(groups, tmp):
    """Cajas (unidades layout) de cada <g id=...>, sin contar trazos ni filtros."""
    body = controller(True) + "".join(f'<g id="m-{k}">{v}</g>' for k, v in groups.items())
    js = ("const r={};document.querySelectorAll('g[id^=m-]').forEach(g=>{const b=g.getBoundingClientRect();"
          "r[g.id.slice(2)]=[b.x,b.y,b.width,b.height]});document.getElementById('out').textContent=JSON.stringify(r);")
    page = tmp / "measure.html"
    page.write_text(html(body, js), encoding="utf-8")
    out = chrome(["--dump-dom", page.as_uri()], tmp / "m").stdout
    return json.loads(re.search(r"<pre id=\"out\">(.*?)</pre>", out, re.S).group(1))

def render(name, body, tmp):
    work = tmp / name
    work.mkdir(parents=True, exist_ok=True)
    page, png = work / "page.html", work / "shot.png"
    page.write_text(html(body), encoding="utf-8")
    chrome([f"--window-size={W},{H}", f"--force-device-scale-factor={DSF}",
            "--default-background-color=00000000", f"--screenshot={png}", page.as_uri()], work)
    img = Image.open(png).convert("RGBA")
    assert img.size == (W * DSF, H * DSF), f"{name}: tamaño inesperado {img.size}"
    return name, img

def box(b, margin):
    """Caja ampliada y ajustada a pares, para que 320 (x0.5) también caiga en píxel entero."""
    x, y, w, h = b
    x0 = max(0, int((x - margin) // 2 * 2))
    y0 = max(0, int((y - margin) // 2 * 2))
    x1 = min(W, int(-(-(x + w + margin) // 2) * 2))
    y1 = min(H, int(-(-(y + h + margin) // 2) * 2))
    return [x0, y0, x1 - x0, y1 - y0]

def save(img, rel, rect=None):
    for size, s in SIZES.items():
        out = PNG / size / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        if rect:
            x, y, w, h = rect
            part = img.crop((x * DSF, y * DSF, (x + w) * DSF, (y + h) * DSF))
            part.resize((round(w * s), round(h * s)), Image.LANCZOS).save(out)
        else:
            img.resize((round(W * s), round(H * s)), Image.LANCZOS).save(out)

def main():
    tmp = Path(tempfile.mkdtemp(prefix="padov-"))
    try:
        sprites = {**PRESSED, **OTHER}
        boxes = measure({**sprites, "touch_area": shape(TP_IDX)}, tmp)

        rects = {}
        for k in sprites:
            margin = 2 if k.endswith("_fill") else GLOW_M
            rects[k] = box(boxes[k], margin)

        cx, cy = boxes["stick_cap"][0] + boxes["stick_cap"][2] / 2, boxes["stick_cap"][1] + boxes["stick_cap"][3] / 2
        half = int(-(-(boxes["stick_cap"][2] / 2 + GLOW_M) // 2) * 2)
        rects["stick_cap"] = rects["stick_cap_l3"] = [round(cx) - half, round(cy) - half, 2 * half, 2 * half]
        dot = boxes["touch_dot"]
        dot_half = int(-(-(dot[2] / 2 + GLOW_M) // 2) * 2)
        dcx, dcy = dot[0] + dot[2] / 2, dot[1] + dot[3] / 2
        rects["touch_dot"] = [round(dcx) - dot_half, round(dcy) - dot_half, 2 * dot_half, 2 * dot_half]

        jobs = {"base": controller(False), "base_full": controller(True), **sprites}
        for old in PNG.glob("*") if PNG.exists() else []:
            shutil.rmtree(old) if old.is_dir() else old.unlink()
        with ThreadPoolExecutor(max_workers=4) as pool:
            for name, img in pool.map(lambda kv: render(kv[0], kv[1], tmp), jobs.items()):
                if name in ("base", "base_full"):
                    save(img, f"{name}.png")
                elif name in PRESSED:
                    save(img, f"pressed/{name}.png", rects[name])
                else:
                    save(img, f"{name}.png", rects[name])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    k = W / CROP[2]
    centers = {s: [round((CAP_C[s][0] - CROP[0]) * k, 2), round((CAP_C[s][1] - CROP[1]) * k, 2)] for s in "lr"}
    ta = boxes["touch_area"]
    layout = {
        "canvas": [W, H],
        "sizes": SIZES,
        "units": "Todas las coordenadas están en unidades de 640x480; multiplica por sizes[carpeta].",
        "base": "base.png",
        "base_full": "base_full.png",
        "buttons": {n: {"sprite": f"pressed/{n}.png", "rect": rects[n],
                        "bit": None if PAD_BITS[n] is None else hex(PAD_BITS[n])} for n in PRESSED},
        "sticks": {
            side: {"center": centers[s], "travel": TRAVEL, "size": rects["stick_cap"][2:],
                   "sprite": "stick_cap.png", "sprite_pressed": "stick_cap_l3.png",
                   "click": f"{s}3", "bit": hex(PAD_BITS[f"{s}3"])}
            for side, s in (("left", "l"), ("right", "r"))
        },
        "triggers": {t: {"sprite": f"{t}_fill.png", "rect": rects[f"{t}_fill"], "fill": "bottom_up",
                         "bit": hex(PAD_BITS[t])} for t in ("l2", "r2")},
        "touchpad": {"rect": [round(v, 2) for v in ta], "dot": "touch_dot.png", "dot_size": rects["touch_dot"][2:],
                     "note": "Mapear ScePadTouch.x/y con la resolución de scePadGetControllerInformation()."},
        "credits": "Button Icons and Controls by Zacksly (CC BY 3.0 | zacksly.itch.io) — modificado",
    }
    (ASSETS / "layout.json").write_text(json.dumps(layout, indent=2, ensure_ascii=False), encoding="utf-8")
    (ASSETS / "layout.js").write_text("window.LAYOUT = " + json.dumps(layout, ensure_ascii=False) + ";\n",
                                      encoding="utf-8")
    compose_preview(layout)
    write_c_sources(layout)
    print(f"ok: {sum(1 for _ in PNG.rglob('*.png'))} png en {PNG}")

def compose_preview(L, size="640"):
    """Simula al renderer: solo base + sprites + layout.json. Si algo se ve descolocado, el layout está mal."""
    s = SIZES[size]
    d = PNG / size
    sc = lambda v: round(v * s)
    img = Image.open(d / "base.png").convert("RGBA")

    def put(rel, x, y):
        img.alpha_composite(Image.open(d / rel).convert("RGBA"), (sc(x), sc(y)))

    for n in ("cross", "r1", "dpad_right", "options"):
        x, y, *_ = L["buttons"][n]["rect"]
        put(L["buttons"][n]["sprite"], x, y)
    for t, v in (("l2", 0.6), ("r2", 1.0)):
        x, y, w, h = L["triggers"][t]["rect"]
        fill = Image.open(d / L["triggers"][t]["sprite"]).convert("RGBA")
        cut = round(fill.height * (1 - v))
        img.alpha_composite(fill.crop((0, cut, fill.width, fill.height)), (sc(x), sc(y) + cut))
    for side, (dx, dy), click in (("left", (-0.7, -0.4), False), ("right", (0.5, 0.6), True)):
        st = L["sticks"][side]
        w, h = st["size"]
        put(st["sprite_pressed"] if click else st["sprite"],
            st["center"][0] + dx * st["travel"] - w / 2, st["center"][1] + dy * st["travel"] - h / 2)
    tx, ty, tw, th = L["touchpad"]["rect"]
    dw, dh = L["touchpad"]["dot_size"]
    put(L["touchpad"]["dot"], tx + tw * 0.3 - dw / 2, ty + th * 0.55 - dh / 2)
    img.save(ASSETS / "preview.png")

ELF_DIR = ROOT / "elf" / "assets"
ELF_SIZES = ("320", "640")

def write_c_sources(L):
    """PNG embebidos + coordenadas en píxeles de cada tamaño, para compilar dentro del .elf.

    pad_assets.h es común; pad_assets_<tamaño>.c lleva los datos (se enlaza solo uno).
    """
    ELF_DIR.mkdir(parents=True, exist_ok=True)
    pngs = [L["base"]] + [b["sprite"] for b in L["buttons"].values()] + \
           [L["sticks"]["left"]["sprite"], L["sticks"]["left"]["sprite_pressed"]] + \
           [t["sprite"] for t in L["triggers"].values()] + [L["touchpad"]["dot"]]
    ident = lambda rel: "PAD_PNG_" + re.sub(r"\W", "_", Path(rel).stem).upper()
    bit = lambda v: "0" if v is None else v

    h = ["/*\n * Generated by tools/build_assets.py - do not edit by hand.\n * Embedded artwork: \"PS5 Button Icons and Controls\" by Zacksly\n * (https://zacksly.itch.io), licensed CC BY 3.0, modified.\n * SPDX-License-Identifier: GPL-3.0-or-later AND CC-BY-3.0\n */",
         "#pragma once", "#include <stdint.h>", "",
         "typedef struct { const char *file; const uint8_t *data; uint32_t size; } pad_png_t;",
         "typedef struct { const char *name; uint32_t bit; int16_t x, y, w, h; uint8_t png; } pad_sprite_t;",
         "typedef struct { float cx, cy, travel; int16_t w, h; uint32_t click_bit; } pad_stick_t;",
         "typedef struct { float x, y, w, h; int16_t dot_w, dot_h; } pad_touchpad_t;", "",
         "enum {"] + [f"    {ident(p)}," for p in pngs] + ["    PAD_PNG_COUNT", "};", "",
         "extern const int pad_overlay_w, pad_overlay_h;",
         "extern const pad_png_t pad_pngs[PAD_PNG_COUNT];",
         f"extern const pad_sprite_t pad_buttons[{len(L['buttons'])}];",
         "extern const pad_sprite_t pad_triggers[2];",
         "extern const pad_stick_t pad_sticks[2];",
         "extern const pad_touchpad_t pad_touchpad;", ""]
    (ELF_DIR / "pad_assets.h").write_text("\n".join(h), encoding="utf-8")

    for size in ELF_SIZES:
        s = SIZES[size]
        px = lambda r: ", ".join(str(round(v * s)) for v in r)
        c = ["/*\n * Generated by tools/build_assets.py - do not edit by hand.\n * Embedded artwork: \"PS5 Button Icons and Controls\" by Zacksly\n * (https://zacksly.itch.io), licensed CC BY 3.0, modified.\n * SPDX-License-Identifier: GPL-3.0-or-later AND CC-BY-3.0\n */", '#include "pad_assets.h"', ""]
        for p in pngs:
            data = (PNG / size / p).read_bytes()
            rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
            c += [f"static const uint8_t {ident(p).lower()}[{len(data)}] = {{", *[f"    {r}," for r in rows], "};"]
        c += ["", f"const int pad_overlay_w = {round(W * s)}, pad_overlay_h = {round(H * s)};", "",
              "const pad_png_t pad_pngs[PAD_PNG_COUNT] = {"]
        c += [f'    [{ident(p)}] = {{"{Path(p).name}", {ident(p).lower()}, sizeof {ident(p).lower()}}},' for p in pngs]
        c += ["};", "", f"const pad_sprite_t pad_buttons[{len(L['buttons'])}] = {{"]
        c += [f'    {{"{n}", {bit(b["bit"])}, {px(b["rect"])}, {ident(b["sprite"])}}},' for n, b in L["buttons"].items()]
        c += ["};", "", "const pad_sprite_t pad_triggers[2] = {"]
        c += [f'    {{"{n}", {t["bit"]}, {px(t["rect"])}, {ident(t["sprite"])}}},' for n, t in L["triggers"].items()]
        c += ["};", "", "const pad_stick_t pad_sticks[2] = {"]
        for st in (L["sticks"]["left"], L["sticks"]["right"]):
            c.append(f'    {{{st["center"][0] * s:.2f}f, {st["center"][1] * s:.2f}f, {st["travel"] * s:.2f}f, '
                     f'{px(st["size"])}, {st["bit"]}}},')
        tp = L["touchpad"]
        c += ["};", "", "const pad_touchpad_t pad_touchpad = {"
              + ", ".join(f"{v * s:.2f}f" for v in tp["rect"]) + f", {px(tp['dot_size'])}}};", ""]
        (ELF_DIR / f"pad_assets_{size}.c").write_text("\n".join(c), encoding="utf-8")

if __name__ == "__main__":
    main()

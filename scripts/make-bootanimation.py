#!/usr/bin/env python3
"""The Echo Shows' boot animation from the Kiosk Satellite mark.

  scripts/make-bootanimation.py [mark.svg] [out_dir]

The four bars bounce on their own while Android boots (part1, looped). Once
the boot is done, bootanimation finishes the loop it is in, then the bars
settle at their size in the mark and the house fades in around them (part2).
part0 grows the bars out of dots at the start.

It writes bootanimation-<height>.zip for each Show's screen height to
common/media/, where show.mk picks the one for the device. Run it
again after a change to the mark. It needs Pillow.
"""
import io, math, os, re, sys, zipfile
from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = os.path.join(REPO, "common/media/mark.svg")
OUT = os.path.join(REPO, "common/media")

# Screen heights of the Echo Show 5 (2nd gen) and the Echo Show 8 (1st gen).
# The mark takes this share of the height.
HEIGHTS = (480, 800)
MARK_SHARE = 0.45
FPS = 30
BACKGROUND = (0, 0, 0)
SS = 4  # supersampling

INTRO_S, LOOP_S, SETTLE_S, FADE_S, HOLD_S = 0.4, 2.0, 0.5, 0.6, 0.5


def flatten(d, steps=24):
    """The points of an SVG path with M, L, H, V, C, S and Z commands, absolute or relative."""
    tokens = re.findall(r"[MmLlHhVvCcSsZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    pts, x, y, cmd, i, ctrl = [], 0.0, 0.0, None, 0, None
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]; i += 1
            if cmd in "Zz":
                continue
        rel = cmd.islower()
        c = cmd.upper()
        n = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4}[c]
        a = [float(v) for v in tokens[i:i + n]]; i += n
        if c in "CS":
            p0 = (x, y)
            if c == "S":
                # The first control point mirrors the last one of the curve before.
                a = [2 * x - ctrl[0], 2 * y - ctrl[1]] if ctrl else [x, y]
                a = ([v - (x if k == 0 else y) for k, v in enumerate(a)] if rel else a) + [float(v) for v in tokens[i - 4:i]]
            p1, p2, p3 = [(a[k] + (x if rel else 0), a[k + 1] + (y if rel else 0)) for k in (0, 2, 4)]
            for s in range(1, steps + 1):
                t = s / steps; u = 1 - t
                pts.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                            u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
            x, y = p3
            ctrl = p2
        else:
            ctrl = None
            if c in "ML":
                x, y = (a[0] + x, a[1] + y) if rel else (a[0], a[1])
            elif c == "H":
                x = a[0] + x if rel else a[0]
            else:
                y = a[0] + y if rel else a[0]
            pts.append((x, y))
            if c == "M":
                cmd = "l" if rel else "L"
    return pts


def load(path):
    """The mark's view box, house (outline, fill, stroke, width) and bars (box, color), left to right."""
    s = open(path).read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', s).group(1).split()]
    styles = {m[0]: dict(re.findall(r"([\w-]+):\s*([^;]+);", m[1]))
              for m in re.findall(r"\.(\w+)\s*\{([^}]*)\}", s)}
    house, bars = None, []
    for cls, d in re.findall(r'<path class="(\w+)" d="([^"]+)"', s):
        st, pts = styles[cls], flatten(d)
        if "stroke" in st:
            house = (pts, st["fill"], st["stroke"], float(st["stroke-width"].replace("px", "")))
        else:
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            bars.append(((min(xs), min(ys), max(xs), max(ys)), st["fill"]))
    return vb, house, sorted(bars)


def ease(t):
    return 0.5 - 0.5 * math.cos(math.pi * max(0.0, min(1.0, t)))


class Mark:
    def __init__(self, path, height):
        (vx, vy, vw, vh), house, bars = load(path)
        self.h = height - height % 2
        self.w = int(round(self.h * vw / vh)) // 2 * 2
        self.k = self.h * SS / vh
        self.vx, self.vy = vx, vy
        self.bars = bars
        self.center = sum((b[1] + b[3]) / 2 for b, _ in bars) / len(bars)
        self.width = bars[0][0][2] - bars[0][0][0]
        self.rest = [b[3] - b[1] for b, _ in bars]
        self.tall = max(self.rest) * 1.1
        pts, fill, stroke, sw = house
        img = Image.new("RGBA", (self.w * SS, self.h * SS), (0, 0, 0, 0))
        dr = ImageDraw.Draw(img)
        xy = [self.px(p) for p in pts]
        dr.polygon(xy, fill=fill)
        # A round-joined stroke: a line along each segment and a dot at each point.
        r = sw * self.k / 2
        for a, b in zip(xy, xy[1:] + xy[:1]):
            dr.line([a, b], fill=stroke, width=int(round(2 * r)))
            dr.ellipse([a[0] - r, a[1] - r, a[0] + r, a[1] + r], fill=stroke)
        self.house = img

    def px(self, p):
        return ((p[0] - self.vx) * self.k, (p[1] - self.vy) * self.k)

    def loop(self, t):
        """The bar heights at t seconds into the loop: each bar on its own beat, back where it started at LOOP_S."""
        out = []
        for i in range(len(self.bars)):
            ph = 2 * math.pi * t / LOOP_S
            v = 0.55 * math.sin(ph * (1 + i % 2) + i * 1.9) + 0.45 * math.sin(ph * (2 + (i * 3) % 2) + i * 0.7 + 1.0)
            out.append(self.width + (self.tall - self.width) * (0.5 + 0.5 * v))
        return out

    def frame(self, heights, house=0.0):
        img = Image.new("RGB", (self.w * SS, self.h * SS), BACKGROUND)
        if house > 0:
            layer = self.house.copy()
            layer.putalpha(layer.getchannel("A").point(lambda a: int(a * house)))
            img.paste(layer, (0, 0), layer)
        dr = ImageDraw.Draw(img)
        for ((x0, _, x1, _), color), hgt in zip(self.bars, heights):
            hgt = max(hgt, self.width)
            a, b = self.px((x0, self.center - hgt / 2)), self.px((x1, self.center + hgt / 2))
            dr.rounded_rectangle([a, b], radius=(x1 - x0) / 2 * self.k, fill=color)
        return img.resize((self.w, self.h), Image.LANCZOS)

    def box(self):
        """The part of the frame the bars can reach, for trim.txt."""
        (x0, _, _, _), _ = self.bars[0]
        (_, _, x1, _), _ = self.bars[-1]
        a = self.px((x0, self.center - self.tall / 2)); b = self.px((x1, self.center + self.tall / 2))
        l, t = int(a[0] / SS) - 2, int(a[1] / SS) - 2
        return max(l, 0), max(t, 0), min(int(math.ceil(b[0] / SS)) + 2, self.w), min(int(math.ceil(b[1] / SS)) + 2, self.h)


def png(img):
    buf = io.BytesIO(); img.save(buf, "PNG", optimize=True); return buf.getvalue()


def build(path, height, out):
    m = Mark(path, int(height * MARK_SHARE))
    start = m.loop(0)
    box = m.box()
    parts = {"part0": [], "part1": [], "part2": []}
    n = int(INTRO_S * FPS)
    for f in range(n):
        e = ease((f + 1) / n)
        parts["part0"].append((m.frame([m.width + (s - m.width) * e for s in start]), box))
    for f in range(int(LOOP_S * FPS)):
        parts["part1"].append((m.frame(m.loop(f / FPS)), box))
    n = int(SETTLE_S * FPS)
    for f in range(n):
        e = ease((f + 1) / n)
        parts["part2"].append((m.frame([s + (r - s) * e for s, r in zip(start, m.rest)]), None))
    n = int(FADE_S * FPS)
    for f in range(n):
        parts["part2"].append((m.frame(m.rest, ease((f + 1) / n)), None))
    desc = (f"{m.w} {m.h} {FPS}\n"
            f"c 1 0 part0 #{'%02x%02x%02x' % BACKGROUND}\n"
            f"c 0 0 part1 #{'%02x%02x%02x' % BACKGROUND}\n"
            f"c 1 {int(HOLD_S * FPS)} part2 #{'%02x%02x%02x' % BACKGROUND}\n")
    name = os.path.join(out, f"bootanimation-{height}.zip")
    with zipfile.ZipFile(name, "w", zipfile.ZIP_STORED) as z:
        z.writestr("desc.txt", desc)
        for part, frames in parts.items():
            trim = []
            for i, (img, b) in enumerate(frames):
                if b:
                    img = img.crop(b)
                    trim.append(f"{b[2] - b[0]}x{b[3] - b[1]}+{b[0]}+{b[1]}\n")
                z.writestr(f"{part}/{i:03d}.png", png(img))
            if trim:
                z.writestr(f"{part}/trim.txt", "".join(trim))
    print(f"{name}: {m.w}x{m.h}, {sum(len(v) for v in parts.values())} frames, {os.path.getsize(name) // 1024} KiB")


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else MARK
    out = sys.argv[2] if len(sys.argv) > 2 else OUT
    os.makedirs(out, exist_ok=True)
    for h in HEIGHTS:
        build(path, h, out)

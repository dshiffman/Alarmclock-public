"""Draws web/icon.png, the home-screen icon for the settings page (180 x 180, as iOS wants).
Run once after changing it: python tools/make_web_icon.py  (needs Pillow)."""
import math
from pathlib import Path
from PIL import Image, ImageDraw

S = 4  # draw big, then shrink, for smooth edges
N = 180 * S
BG, FACE, HAND, ACCENT = (10, 12, 16), (242, 233, 220), (20, 23, 29), (255, 181, 71)

img = Image.new("RGB", (N, N), BG)
d = ImageDraw.Draw(img)
c, r = N / 2, N * 0.36
d.ellipse([c - r, c - r, c + r, c + r], fill=FACE)
for i in range(12):  # hour ticks
    a = math.radians(i * 30)
    r1, r2 = r * (0.78 if i % 3 else 0.70), r * 0.88
    d.line([c + r1 * math.sin(a), c - r1 * math.cos(a), c + r2 * math.sin(a), c - r2 * math.cos(a)],
           fill=HAND, width=int(N * (0.022 if i % 3 == 0 else 0.012)))

def hand(deg, length, width, color):
    a = math.radians(deg)
    d.line([c, c, c + length * math.sin(a), c - length * math.cos(a)], fill=color, width=width)

hand(-60, r * 0.45, int(N * 0.045), HAND)   # hour hand at about 10
hand(60, r * 0.66, int(N * 0.03), HAND)     # minute hand at about 10
hand(180, r * 0.62, int(N * 0.018), ACCENT)  # alarm hand
d.ellipse([c - N * 0.03, c - N * 0.03, c + N * 0.03, c + N * 0.03], fill=ACCENT)

out = Path(__file__).resolve().parent.parent / "web" / "icon.png"
img.resize((180, 180), Image.LANCZOS).save(out, optimize=True)
print("wrote", out)

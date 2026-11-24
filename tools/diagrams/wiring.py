import os
# Generates docs/wiring.svg
W, H = 1400, 1095
out = []
def a(s): out.append(s)
FONT = "font-family='Helvetica, Arial, sans-serif'"
def text(x, y, s, size=16, anchor="middle", weight="normal", color="#222", extra=""):
    a(f"<text x='{x}' y='{y}' font-size='{size}' text-anchor='{anchor}' font-weight='{weight}' fill='{color}' {FONT} {extra}>{s}</text>")
def rect(x, y, w, h, fill, stroke="#333", r=8, sw=2, dash=""):
    d = f" stroke-dasharray='{dash}'" if dash else ""
    a(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{r}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'{d}/>")
def wire(pts, color, w=4):
    p = " ".join(f"{x},{y}" for x, y in pts)
    a(f"<polyline points='{p}' fill='none' stroke='{color}' stroke-width='{w}' stroke-linejoin='round' stroke-linecap='round'/>")
def dot(x, y, color="#333", r=5):
    a(f"<circle cx='{x}' cy='{y}' r='{r}' fill='{color}'/>")

RED, BLK, YEL, GRN, BLU, PUR, ORG, GRY = "#d64545", "#2b2b2b", "#d99a00", "#2f9a5a", "#2f6fd6", "#7b55e0", "#e07a2f", "#888"

a(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>")
a(f"<rect width='{W}' height='{H}' fill='#ffffff'/>")
text(40, 44, "Bedroom alarm clock: wiring", 26, "start", "bold")
text(40, 72, "Freenove FNK0115Q 5\" board, seen from the back. All board connections use its 4-pin JST headers (cables come with the board).", 15, "start", color="#555")

# Board
bx, by, bw, bh = 80, 110, 900, 330
rect(bx, by, bw, bh, "#f6c35a", "#b8862a", 14, 3)
text(bx + 24, by + 36, "Freenove ESP32-S3 Display FNK0115Q (back)", 20, "start", "bold", "#5a3d00")
rect(bx + 30, by + 60, 150, 110, "#5b5f66", "#333", 6)
text(bx + 105, by + 120, "ESP32-S3", 16, color="#fff")
text(bx + bw - 24, by + 36, "on-board speaker amp (NS4168)", 13, "end", color="#7a5600")

# Connectors along the bottom edge
pitch = 18
conns = {
    "P3": (300, "SPI", ["IO19", "IO11", "IO12", "IO13"]),
    "P4": (430, "I2C", ["IO17", "IO18", "IO19", "IO20"]),
    "P5": (560, "", ["GND", "3V3", "IO17", "IO18"]),
    "P6": (690, "", ["GND", "3V3", "IO17", "IO18"]),
    "P7": (815, "Speaker", ["SP-", "SP+"]),
}
pin = {}
cy = by + bh - 34
for name, (cx, title, pins) in conns.items():
    n = len(pins)
    w = n * pitch + 16
    rect(cx - w / 2, cy, w, 34, "#f4f1ea", "#999", 4, 1.5)
    text(cx, cy - 66, f"{name}" + (f" {title}" if title else ""), 14, weight="bold", color="#5a3d00")
    for i, p in enumerate(pins):
        x = cx + (i - (n - 1) / 2) * pitch
        a(f"<text transform='translate({x + 5},{cy - 6}) rotate(-90)' font-size='12' fill='#5a3d00' {FONT}>{p}</text>")
        dot(x, cy + 17, "#777", 4)
        pin[(name, p)] = (x, by + bh)
# USB-C port on the right edge
ux, uy = bx + bw, by + 160
rect(ux - 16, uy - 18, 30, 36, "#c9c9c9", "#666", 5)
text(ux - 24, uy + 5, "USB-C", 12, "end", color="#5a3d00")

def from_pin(key, y, color, x_to=None):
    x, y0 = pin[key]
    dot(x, y0 - 17, color, 5)
    pts = [(x, y0 - 17), (x, y)]
    if x_to is not None:
        pts.append((x_to, y))
    wire(pts, color)
    return pts

def hline_hops(x1, x2, y, hops, color):
    """Horizontal wire from x1 to x2 (x1 > x2) that hops over vertical wires at `hops`."""
    d = f"M{x1},{y}"
    for hx in sorted(hops, reverse=True):
        d += f" H{hx + 8} a8,8 0 0 0 -16,0"
    d += f" H{x2}"
    a(f"<path d='{d}' fill='none' stroke='{color}' stroke-width='4' stroke-linejoin='round' stroke-linecap='round'/>")

# ---- SHT41: power from P6, I2C on P4 ----
sx, sy = 650, 600
x_sda, x_scl, x_gnd, x_vcc = 590, 620, pin[("P6", "GND")][0], 710
from_pin(("P4", "IO19"), 520, BLU, x_sda)
wire([(x_sda, 520), (x_sda, sy)], BLU)
from_pin(("P4", "IO20"), 540, ORG, x_scl)
wire([(x_scl, 540), (x_scl, sy)], ORG)
from_pin(("P6", "GND"), sy, BLK)
from_pin(("P6", "3V3"), 560, RED, x_vcc)
wire([(x_vcc, 560), (x_vcc, sy)], RED)
rect(sx - 90, sy, 180, 90, "#ffffff", "#333", 8)
text(x_sda, sy + 22, "SDA", 12, color=BLU)
text(x_scl + 4, sy + 22, "SCL", 12, color=ORG)
text(x_gnd, sy + 22, "GND", 12, color=BLK)
text(x_vcc + 4, sy + 22, "VIN", 12, color=RED)
text(sx, sy + 60, "SHT41", 18, weight="bold")
text(sx, sy + 80, "or SHT31 (GY-SHT31-D)", 13, color="#555")
text(sx, sy + 118, "Indoor temp + humidity", 17, weight="bold")
text(sx, sy + 140, "outside the case, in the vented pod", 13, color="#555")

# ---- BH1750 light sensor: power from P5, SDA/SCL tapped off the SHT41 wires ----
lx, ly = 228, 600
gx, vx, dx, cx_ = 190, 215, 240, 265
x19, x20 = pin[("P4", "IO19")][0], pin[("P4", "IO20")][0]
from_pin(("P5", "GND"), 455, BLK)
hline_hops(pin[("P5", "GND")][0], gx, 455, [x20, x19], BLK)
wire([(gx, 455), (gx, ly)], BLK)
from_pin(("P5", "3V3"), 470, RED)
hline_hops(pin[("P5", "3V3")][0], vx, 470, [x20, x19], RED)
wire([(vx, 470), (vx, ly)], RED)
dot(x19, 490, BLU, 5)
wire([(x19, 490), (dx, 490), (dx, ly)], BLU)
dot(x20, 505, ORG, 5)
hline_hops(x20, cx_, 505, [x19], ORG)
wire([(cx_, 505), (cx_, ly)], ORG)
rect(lx - 75, ly, 150, 90, "#ffffff", "#333", 8)
text(gx, ly + 22, "GND", 11, color=BLK)
text(vx + 2, ly + 38, "VCC", 11, color=RED)
text(dx + 2, ly + 22, "SDA", 11, color=BLU)
text(cx_ + 4, ly + 38, "SCL", 11, color=ORG)
text(lx, ly + 64, "BH1750", 18, weight="bold")
text(lx, ly + 82, "GY-302 (ADDR open)", 12, color="#555")
text(lx, ly + 118, "Room light sensor", 17, weight="bold")
text(lx, ly + 140, "face up, under the slot on top", 13, color="#555")

text(250, cy - 29, "IO11-13: free", 12, "end", color="#777")

# ---- Speaker on P7 ----
spx, spy = 860, 620
from_pin(("P7", "SP-"), spy - 30, PUR, spx - 20)
from_pin(("P7", "SP+"), spy - 50, PUR, spx + 20)
wire([(spx - 20, spy - 30), (spx - 20, spy)], PUR)
wire([(spx + 20, spy - 50), (spx + 20, spy)], PUR)
a(f"<circle cx='{spx}' cy='{spy + 45}' r='45' fill='#eee' stroke='#333' stroke-width='2'/>")
a(f"<circle cx='{spx}' cy='{spy + 45}' r='22' fill='#ccc' stroke='#555' stroke-width='1.5'/>")
text(spx, spy + 118, "Speaker", 17, weight="bold")
text(spx, spy + 140, "comes with the board", 13, color="#555")

# ---- Power: USB-C supply -> panel jack in the back cover -> slim right-angle cable -> board ----
rect(1180, 240, 170, 60, "#eef3fb", "#333", 10)
text(1265, 266, "USB-C supply", 15, weight="bold")
text(1265, 286, "5 V 2 A", 12, color="#555")
wire([(1180, uy), (1120, uy)], GRY, 6)
rect(1060, uy - 34, 60, 68, "#e8e8e8", "#333", 30)
text(1090, uy + 5, "socket", 12)
text(1090, uy + 56, "Panel socket in", 12, color="#555")
text(1090, uy + 71, "the back cover", 12, color="#555")
wire([(1060, uy), (ux + 14, uy)], GRY, 6)
text(1090, uy + 96, "Inside: flat right-angle", 12, color="#555")
text(1090, uy + 111, "USB-C cable to the board", 12, color="#555")

# ---- Notes ----
def note_box(x, y, w, title, lines, bold_n):
    h = 46 + 24 * len(lines)
    rect(x, y, w, h, "#fafafa", "#bbb", 10, 1.5, "6 4")
    text(x + 20, y + 32, title, 16, "start", "bold")
    for i, line in enumerate(lines):
        text(x + 20, y + 60 + i * 24, line, 14, "start", color="#333" if i < bold_n else "#555")

note_box(1000, 610, 380, "Power", [
    "No battery. After a power cut the clock",
    "restarts, gets the time from WiFi and rings any",
    "alarm it missed (up to 30 minutes late).",
    "Part: flat right-angle USB-C to panel-mount",
    "USB-C socket cable, 30 cm (Amazon B0FQNPK1SQ).",
    "It carries data, so uploads work through the",
    "back socket.",
], 3)
note_box(1000, 835, 380, "Free pins", [
    "IO11, IO12 and IO13 (P3) are spare. P3 pin 1 is",
    "IO19 (I2C data). Leave IO17 / IO18 alone: the",
    "speaker amp uses them. Keep the SD slot empty.",
], 0)

a("</svg>")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/wiring.svg"), "w").write("\n".join(out))

import os
# docs/hub-perfboard.svg: the sensor and LED-strip hub on rows I-X of a 3 x 7 cm perfboard
# (10 columns x 16 rows). 0805 resistors bridge two neighbouring pads, the SOT-23 MOSFET sits on
# a 2x2 block of pads, as on the earlier power board.
W, H = 1250, 1300
P = 62                      # pixels per 2.54 mm hole
X0, Y0 = 520, 200           # centre of pad (0,0); pads are (1..4, 1..11)
COLS, ROWS = 4, 11   # parts area; the board adds an empty rim column each side and an empty row U
MM = P / 2.54
ROWNAME = "UTSRQPONMLKJ"   # the board is turned around: its row U is at the top
out = []
a = out.append
F = "font-family='Helvetica, Arial, sans-serif'"
def text(x, y, s, size=15, anchor="middle", weight="normal", color="#222"):
    a(f"<text x='{x:.1f}' y='{y:.1f}' font-size='{size}' text-anchor='{anchor}' font-weight='{weight}' fill='{color}' {F}>{s}</text>")
def pc(c, r): return X0 + c * P, Y0 + r * P
NET = {"GND": "#2b2b2b", "3V3": "#d64545", "SCL": "#2f9a5a", "SDA": "#2f6fd6", "5V": "#e07a2f",
       "IO12": "#7b55e0", "D": "#b0306a", "DATA": "#16897f"}
J, K, L, M, N, O, Pr, Q, R, S, T = range(1, 12)
links = {   # net: chains of pads joined by solder bridges / bare wire (underside under the headers)
 "GND":  [[(1, r) for r in range(J, T + 1)]],
 "3V3":  [[(2, r) for r in range(J, N + 1)]],
 "SCL":  [[(3, r) for r in range(J, M + 1)]],
 "SDA":  [[(4, r) for r in range(J, M + 1)]],
 "IO12": [[(3, N), (4, N)]],
 "D":    [[(2, O), (3, O)]],
 "DATA": [[(2, Pr), (2, Q)]],
 "5V":   [[(4, r) for r in range(O, T + 1)], [(3, Q), (4, Q)], [(3, T), (4, T)]],
}
parts = [  # 0805 parts bridging two neighbouring pads
 ("R1", "1k", (3, O), (4, O)),
 ("R2", "680", (2, O), (2, Pr), "left"),
]
headers = [  # (row, first col, n, labels, title)
 (J, 1, 4, ["GND", "3V3", "SCL", "SDA"], "IN: from the board (P6 GND, 3V3 · P4 SCL IO20, SDA IO19)"),
 (K, 1, 4, None, "OUT 1: SHT31 (pod)"),
 (L, 1, 4, None, "OUT 2: BH1750 (top slot)"),
 (M, 1, 4, None, "OUT 3: SCD41"),
 (Q, 1, 3, None, "LED strip: GND, DIN, +5V"),
]
leads = [  # (pad, net, direction, label lines)
 ((4, N), "IO12", "right", ["P3 IO12 (LED data)"]),
 ((4, R), "5V", "right", ["+5 V from D1's USB-side pad"]),
]
CAP = ((1, T), (3, T))   # (-) leg, (+) leg
QPOS = (2, N)            # 2x2 block: G top-left, S top-right, D bridge on the row below

a(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>")
a(f"<rect width='{W}' height='{H}' fill='#fff'/>")
text(40, 44, "Sensor and LED hub on the green perfboard", 26, "start", "bold")
text(40, 72, "Top view, as built: the board is turned around, so row U is at the top. Parts in columns 1-4; column 5 and the lettered strip stay bare for the case rails.", 15, "start", color="#555")
text(40, 94, "Same style as the power board: 0805 resistors bridge two neighbouring pads, the SOT-23 MOSFET sits on a 2x2 block.", 15, "start", color="#555")

bx0, by0 = X0 - P * 0.5, Y0 + P * 0.5
a(f"<rect x='{bx0 + P*0.4}' y='{by0}' width='{P*(COLS+1)+P*0.6}' height='{P*ROWS+P}' rx='6' fill='#1f7a3a' stroke='#145028' stroke-width='3'/>")
for x in (X0 + P * (COLS + 1),):  # bare column for the rails
    a(f"<rect x='{x - P*0.5}' y='{by0}' width='{P}' height='{P*ROWS+P}' fill='#ffffff' fill-opacity='0.12'/>")
for c in range(1, COLS + 2):
    for r in range(1, ROWS + 2):
        x, y = pc(c, r)
        a(f"<circle cx='{x}' cy='{y}' r='{P*0.32}' fill='#cfcfcf' stroke='#9a9a9a' stroke-width='1.5'/>")
        a(f"<circle cx='{x}' cy='{y}' r='{P*0.12}' fill='#3a3a3a'/>")
for c in range(1, COLS + 2): text(pc(c, 0)[0], by0 + P * ROWS + P + 22, str(c), 12, color="#777")
for r in range(1, ROWS + 2): text(bx0 - 14, pc(0, r)[1] + 4, ROWNAME[r - 1], 12, "end", color="#777")
text(X0 + P * (COLS + 1), by0 - 40, "bare", 13, color="#2b7a3f")

for net, chains in links.items():
    for ch in chains:
        pts = " ".join(f"{pc(*p)[0]},{pc(*p)[1]}" for p in ch)
        a(f"<polyline points='{pts}' fill='none' stroke='{NET[net]}' stroke-width='13' stroke-linejoin='round' stroke-linecap='round' opacity='0.9'/>")
        for p in ch:
            x, y = pc(*p); a(f"<circle cx='{x}' cy='{y}' r='{P*0.12}' fill='#3a3a3a'/>")

# pin headers (2.54 mm male, straight): two 2x4 doubles for the sensor rows, a 1x3 for the strip
for r, c0, n, labs, title in headers:
    x0, y = pc(c0, r)
    a(f"<rect x='{x0 - P*0.45}' y='{y - P*0.45}' width='{P*(n-1) + P*0.9}' height='{P*0.9}' rx='3' fill='#151515' fill-opacity='0.8' stroke='#000' stroke-width='2'/>")
    for i in range(n):
        a(f"<rect x='{x0 + i*P - 7}' y='{y - 7}' width='14' height='14' fill='#d4af37' stroke='#7a5f10'/>")
    if labs:
        nets = {"GND": "GND", "3V3": "3V3", "SCL": "SCL", "SDA": "SDA"}
        io = {"SCL": "IO20", "SDA": "IO19"}
        for i, s in enumerate(labs):
            text(x0 + i * P, by0 - 14, s, 14, weight="bold", color=NET[nets[s]])
            if s in io: text(x0 + i * P, by0 - 32, io[s], 12, weight="bold", color=NET[nets[s]])
    text(bx0 - 40, y + 5, title, 14, "end", "bold", "#222")
for i, (s, n) in enumerate([("GND", "GND"), ("DIN", "DATA"), ("+5V", "5V")]):
    x, y = pc(1 + i, Q)
    text(x, y + 5, s, 11, weight="bold", color="#fff")
for r0 in (J, L):  # outline of each 2x4 double
    x, y = pc(1, r0)
    a(f"<rect x='{x - P*0.55}' y='{y - P*0.55}' width='{P*3 + P*1.1}' height='{P + P*1.1}' rx='4' fill='none' stroke='#ffd84a' stroke-width='2' stroke-dasharray='6 4'/>")

# electrolytic capacitor: upright can, legs 5 mm apart
(c1, r1), (c2, r2) = CAP
cx, cy = (pc(c1, r1)[0] + pc(c2, r2)[0]) / 2, pc(c1, r1)[1]
a(f"<circle cx='{cx}' cy='{cy}' r='{4*MM}' fill='#1d3f8f' fill-opacity='0.55' stroke='#0d2050' stroke-width='3' stroke-dasharray='8 5'/>")
for (c, r), s in [(CAP[0], "−"), (CAP[1], "+")]:
    x, y = pc(c, r)
    a(f"<circle cx='{x}' cy='{y}' r='9' fill='#e0e0e0' stroke='#333' stroke-width='2'/>")
    text(x, y + 6, s, 18, weight="bold", color="#000")
text(bx0 - 40, cy + 5, "C1 1000 µF 10 V+ (8 x 12 mm can, upright)", 14, "end", "bold", "#0d2050")

# 0805 parts
for name, val, p1, p2, *side in parts:
    (x1, y1), (x2, y2) = pc(*p1), pc(*p2)
    cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
    horiz = y1 == y2
    L, Wd = 2.0 * MM, 1.25 * MM
    w, h = (L, Wd) if horiz else (Wd, L)
    a(f"<rect x='{cx-w/2}' y='{cy-h/2}' width='{w}' height='{h}' fill='#e8e8e8' stroke='#555' stroke-width='1.5' rx='2'/>")
    e = 0.45 * MM
    if horiz: a(f"<rect x='{cx-w/2+e}' y='{cy-h/2}' width='{w-2*e}' height='{h}' fill='#1d1d1d'/>")
    else: a(f"<rect x='{cx-w/2}' y='{cy-h/2+e}' width='{w}' height='{h-2*e}' fill='#1d1d1d'/>")
    lx, ly = (cx, cy - h / 2 - 10) if horiz else (cx + w / 2 + 8, cy + 5)
    anc = "middle" if horiz else "start"
    if side: lx, anc = cx - w / 2 - 8, "end"
    a(f"<text x='{lx}' y='{ly}' font-size='13' font-weight='bold' text-anchor='{anc}' fill='#fff' stroke='#123' stroke-width='3' paint-order='stroke' {F}>{name} {val}</text>")

# Q1 2N7002, D down: G on the top-left pad, S top-right, D on the bridge joining the two pads below
c, r = QPOS
x1, y1 = pc(c, r); qx, qy = x1 + P / 2, y1 + P / 2
bw, bh = 2.9 * MM, 1.6 * MM
lw, ll = 0.45 * MM, 0.9 * MM
for lx, bottom in [(qx - 0.95 * MM, False), (qx + 0.95 * MM, False), (qx, True)]:
    ly = qy + bh / 2 if bottom else qy - bh / 2 - ll
    a(f"<rect x='{lx-lw/2}' y='{ly}' width='{lw}' height='{ll}' fill='#d9d9d9' stroke='#666' stroke-width='1'/>")
a(f"<rect x='{qx-bw/2}' y='{qy-bh/2}' width='{bw}' height='{bh}' fill='#222' stroke='#000' stroke-width='1.5' rx='3'/>")
text(qx, qy - 1, "Q1", 13, weight="bold", color="#fff")
text(qx, qy + 11, "2N7002", 9, color="#ddd")
text(qx - P / 2 - 20, qy - P / 2 - 8, "G", 13, weight="bold", color="#ffd84a")
text(qx + P / 2 + 2, qy - P / 2 - 14, "S", 13, weight="bold", color="#ffd84a")
text(qx, qy + P / 2 + 26, "D", 13, weight="bold", color="#ffd84a")
text(bx0 - 40, qy + 5, "Level shifter: Q1, R1 1k, R2 680", 14, "end", "bold", "#222")

for (c, r), net, d, lab in leads:
    x, y = pc(c, r)
    if d == "up": ex, ey = x, by0 - 50
    else: ex, ey = bx0 + P * (COLS + 2) + 60, y
    a(f"<line x1='{x}' y1='{y}' x2='{ex}' y2='{ey}' stroke='#111' stroke-width='10' stroke-linecap='round'/>")
    a(f"<line x1='{x}' y1='{y}' x2='{ex}' y2='{ey}' stroke='{NET[net]}' stroke-width='7' stroke-linecap='round'/>")
    a(f"<circle cx='{x}' cy='{y}' r='7' fill='#e0e0e0' stroke='#555'/>")
    for i, s in enumerate(lab):
        if d == "up": text(ex, ey - 12 - 17 * (len(lab) - 1 - i), s, 14, "middle", "bold", NET[net])
        else: text(ex + 10, ey + 5 + 17 * i - 8, s, 14, "start", "bold", NET[net])

# legend
ly = by0 + P * ROWS + P + 70
notes = [
 "Coloured bands = solder bridges or bare tinned wire. Under the headers they have to go on the underside; pads not on a band stay separate.",
 "Column 5 and row J stay bare, and so does the lettered strip on the left: the case rails and latch grip the edges. The IO12 and 5 V wires",
 "    (shown to the right) solder to Q4 and M4 from the underside and run up the back of the board, out past row U.",
 "Sensor rows U-R: two 2x4 headers side by side, every column one bus (GND, 3V3, SCL, SDA). Row U takes the wires from the board, T-R go to",
 "    the three sensors. Match each sensor's wires to the labels; all three share the bus at different addresses (0x44, 0x23, 0x62).",
 "LED part: Q1 lifts the 3.3 V data from IO12 (Q4) to 5 V: gate on 3V3 (Q2), source Q3, drain on the P2-P3 bridge, pulled to 5 V by R1.",
 "    R2 (680) runs from the drain to DIN at N2. Column 4 below Q is the 5 V rail; C1 sits across GND (K1, stripe) and 5 V (K3).",
 "Careful in column 4: Q4 (IO12) sits right next to P4 (5 V). Keep them apart.",
 "To try the strip without the shifter: leave Q1 and R1 off and bridge Q3 to P3.",
 "Tallest parts: C1 (about 14 mm) and the headers with plugs on (about 15 mm).",
]
for i, s in enumerate(notes): text(40, ly + 24 * i, s, 14, "start", color="#333")
a("</svg>")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/hub-perfboard.svg"), "w").write("\n".join(out))
print("ok")

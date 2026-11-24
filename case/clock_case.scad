// Bedroom alarm clock case for the Freenove FNK0115Q (5" ESP32-S3 display).
//
// Three printed parts:
//   part = "shell"  front bezel + sides; prints face down, no supports
//   part = "back"   back cover with speaker grille, vents, sensor mount and USB-C jack; prints flat side down
//   part = "pod"    vented pod for the SHT31 breakout; prints roof down, ears on 45 degree ramps
//   part = "base"   weighted stand the clock sits in, so a tap on the screen can't tip it; prints
//                   as it lies (weight pocket open on top, hidden under the clock; no supports)
//   part = "all"    everything assembled (for looking at, not printing)
//
// Export an STL from the command line, e.g.
//   openscad -D 'part="shell"' -o shell.stl clock_case.scad
//
// BEFORE PRINTING: the board dimensions below were measured from Freenove's photos, not
// from a drawing. Check the values marked "MEASURE" with calipers, or print the
// part = "fit" test frame first (a thin slice of the front: window plus screw posts).
//
// Coordinates: y = height (up when the clock stands), z = depth from the front face (0)
// toward the back, x = width (+x is on your LEFT when you face the screen, on your right
// from behind). The board's center is at x = y = 0.

part = "all";

/* [Board: MEASURE] */
board_w = 137.2;     // PCB width (fit test 21 Nov: hole spacing 1/16" short side to side too, so +1.6, split evenly)
board_h = 83.8;      // PCB height (fit test 21 Nov: top holes and top edge were 1/16" low, so +1.6)
hole_inset = 3.4;    // mounting hole centers, in from each PCB edge
stack_h = 6.0;       // front of the touch glass to the front of the PCB
pcb_t = 1.6;
active_w = 108.0;    // visible screen area (5" 800x480 panel)
active_h = 64.8;
active_dx = 0.3;     // visible area center relative to PCB center (+ = to your right, facing the screen)
active_dy = 0.5;     // (+ = up; was 1.3 before the board grew 1.6 at the top)
usb_from_top = 43.3; // USB-C center, down from the PCB's top edge (port is on the left, facing the screen)

/* [Parts you add] */
light_w = 14.18;     // BH1750 board (GY-302, measured): width and length
light_l = 18.38;
light_gap = 2.2;     // room for the parts on its face, between the board and the top wall
light_chip = 6.9;    // BH1750 chip centre from the board's far end (the end without the pins)
speaker = [40.3, 28]; // the board's speaker, rectangular (measured 40.3 x 28 mm, 21 Nov)
// Power comes in at the back: a flat right-angle USB-C male to panel-mount USB-C female
// cable (JSADZKJ, 30 cm, USB 3.1, Amazon B0FQNPK1SQ) runs from the board's side port to a
// screw-on socket in the back cover. It carries data, so firmware still uploads through it.
usb_exit = "back";   // "back", or "side" for the old hole in the side wall
usb_room = 9.5;      // extra room inside each side wall for the right-angle plug head (measured 9 mm past the board edge, 20 Nov)
usb_flange = [25.2, 8.2];  // socket flange, from the listing's drawing
usb_screw_dx = 16.9; // flange screw holes, centre to centre (measured 16.9 mm)
usb_screw_clear = 3.2;     // for the screws that come with the cable
usb_open = [11, 5];  // opening for the plug: USB-C metal is 8.3 x 2.5; room for a slim overmold
usb_panel_t = 1.2;   // cover thinned to this around the socket, so a plug seats fully
// Every screw is an M2.5 flat-head (countersunk) machine screw into a heat-set brass insert:
// initeq M2.5-0.45 "Long" inserts (0.172" = 4.37 mm across, 0.219" = 5.56 mm long;
// initeq: 0.159" = 4.04 mm hole, about 0.8 mm deeper than the insert).
insert_hole = 4.05;
insert_depth = 6.4;
insert_boss = 7.2;        // board posts: about 1.6 mm of plastic round the insert (more would hit the panel)
screw_clear = 2.9;        // M2.5 clearance
screw_head = 5.0;         // M2.5 flat head is 4.7 mm max; 90 degree countersink sized so it sits flush

/* [Case] */
wall = 2.4;
front_t = 2.4;
back_t = 2.4;
depth = 44;          // outer depth of the shell (back cover adds back_t)
clear = 0.6;         // gap around the PCB
bottom_room = 4.5;   // room under the PCB's bottom edge: plugs P3-P7 enter from below and stick out 1/8" (measured)
r_corner = 6;
front_chamfer = 1.5;
window_margin = 0.8; // window is this much bigger than the visible area on each side

$fn = 48;

side_room = usb_exit == "back" ? usb_room : 0;
inner_w = board_w + 2 * clear + 2 * side_room;  // same both sides so the screen stays centred
inner_h = board_h + clear + bottom_room;
board_dy = (bottom_room - clear) / 2;  // board centre sits this far above the case centre
outer_w = inner_w + 2 * wall;
outer_h = inner_h + 2 * wall;
pcb_back_z = front_t + stack_h + pcb_t;
gusset = 9;
gusset_z = pcb_back_z + 3 + gusset;  // full-size gussets start here (clear of board screw heads)
eps = 0.01;

// ---------- helpers ----------

module rounded_rect(w, h, r) {
  offset(r) square([w - 2 * r, h - 2 * r], center = true);
}


module outer_body(h) {
  hull() {
    translate([0, 0, front_chamfer]) linear_extrude(h - front_chamfer) rounded_rect(outer_w, outer_h, r_corner);
    linear_extrude(front_chamfer) rounded_rect(outer_w - 2 * front_chamfer, outer_h - 2 * front_chamfer, r_corner - front_chamfer);
  }
}

function corner_signs() = [[1, 1], [1, -1], [-1, 1], [-1, -1]];
function cover_screw_xy(s) = [s[0] * (inner_w / 2 - gusset / 2), s[1] * (inner_h / 2 - gusset / 2)];

// ---------- shell ----------

module window_cut() {
  w = active_w + 2 * window_margin; h = active_h + 2 * window_margin; c = 1.2;
  translate([-active_dx, board_dy + active_dy, 0])
    hull() {
      translate([0, 0, -eps]) linear_extrude(eps) square([w + 2 * c, h + 2 * c], center = true);
      translate([0, 0, c]) linear_extrude(front_t) square([w, h], center = true);
    }
}

module board_standoffs() {
  for (s = corner_signs())
    translate([s[0] * (board_w / 2 - hole_inset), board_dy + s[1] * (board_h / 2 - hole_inset), front_t - eps])
      difference() {
        cylinder(d = insert_boss, h = stack_h + eps);
        // insert from the top; leaves 2 mm of front face under it
        translate([0, 0, stack_h + eps - insert_depth]) cylinder(d = insert_hole, h = insert_depth + 1);
      }
}

module cover_gussets() {
  for (s = corner_signs())
    difference() {
      hull() {
        translate([s[0] * (inner_w / 2 - gusset / 2), s[1] * (inner_h / 2 - gusset / 2), gusset_z])
          linear_extrude(depth - gusset_z) square(gusset, center = true);
        // 45 degree chamfer underneath so it prints without support
        translate([s[0] * (inner_w / 2 - eps), s[1] * (inner_h / 2 - eps), gusset_z - gusset])
          linear_extrude(eps) square(2 * eps, center = true);
      }
      translate(concat(cover_screw_xy(s), [depth - insert_depth])) cylinder(d = insert_hole, h = insert_depth + 1);
    }
}

module shell() {
  difference() {
    outer_body(depth);
    translate([0, 0, front_t]) linear_extrude(depth) square([inner_w, inner_h], center = true);
    window_cut();
    light_window();
    // USB-C on the left side, facing the screen (only when the cable comes out of the side)
    if (usb_exit == "side")
      translate([outer_w / 2, board_dy + board_h / 2 - usb_from_top, pcb_back_z + 1.6])
        rotate([0, 90, 0]) linear_extrude(4 * wall, center = true) offset(2) square([9 - 4, 14 - 4], center = true);
  }
  board_standoffs();
  cover_gussets();
  light_rails();
}

// ---------- light sensor, under a window in the top ----------
// The BH1750 board slides in from the back, face up and pins last, between two rails under the top
// wall, and looks at the ceiling through a slot. Facing up keeps the screen's own glow
// off it.
light_z1 = depth - 3.5;           // open end, toward the back cover
light_z0 = light_z1 - light_l;  // front end has a stop

module light_window() {
  translate([0, inner_h / 2, light_z0 + light_chip])
    rotate([90, 0, 0]) linear_extrude(4 * wall, center = true) hull() {
      translate([0, -3]) circle(d = 5);
      translate([0, 3]) circle(d = 5);
      translate([0, 3]) rotate(45) square(5, center = true);  // pointed far end: no overhang when printed face down
    }
}

module light_rails() {
  pcb_slot = pcb_t + 0.4;
  y_top = inner_h / 2;
  y_slot = y_top - light_gap;           // board's face rests here
  y_lip = y_slot - pcb_slot;            // lip under the board
  for (sx = [-1, 1]) mirror([sx < 0 ? 1 : 0, 0, 0])
    translate([0, 0, light_z0 - 1.5]) linear_extrude(light_l + 1.5) {
      translate([light_w / 2 + 0.3, y_lip - 1.2]) square([1.5, y_top - y_lip + 1.2 + eps]);
      translate([light_w / 2 - 1.2, y_lip - 1.2]) square([1.5 + 1.5, 1.2]);
    }
  // front stop
  translate([-light_w / 2 - 1.8, y_lip - 1.2, light_z0 - 1.5]) cube([light_w + 3.6, y_top - y_lip + 1.2 + eps, 1.5]);
  // 45 degree ramp under the rails and stop, growing out of the top wall, so they don't start
  // in mid-air when the shell prints face down (same idea as the corner gussets)
  reach = y_top - (y_lip - 1.2);  // how far they stick out from the wall
  hull() {
    translate([-light_w / 2 - 1.8, y_lip - 1.2, light_z0 - 1.5]) cube([light_w + 3.6, reach + eps, eps]);
    translate([-light_w / 2 - 1.8, y_top - eps, light_z0 - 1.5 - reach]) cube([light_w + 3.6, 2 * eps, eps]);
  }
}

// ---------- back cover ----------

speaker_xy = [-44, 18];
sensor_xy = [40, -26];
// USB-C panel socket: low down, so the power cable drops straight to the table, on the
// other side from the sensor pod (left of the bottom vents, seen from behind; the flat cable
// crosses behind the board to reach it).
usb_jack_xy = [-45, -30];
pod_hole_dx = 19.5;  // pod screw holes, either side of the cable hole

// CO2 sensor (Teyleten SCD41 board, measured 21.74 x 13.17 mm, 7.62 mm thick over the
// sensor). It lies flat inside the cover, sensor facing a patch of vents, at the other end
// from the temperature pod so its own warmth doesn't reach the SHT31. It slides in from the
// open end (toward the middle of the cover), sensor end first, until it hits the stop and the
// latch clicks over its other end. The pin end (GND VDD SCL SDA) is at the open end: solder wires into
// the pin holes, leaving from the side without the sensor. The ledges under the long edges reach
// only 0.7 mm in, clear of the sensor's carrier.
co2_pcb = [21.74, 13.17, 1.6];
co2_h = 7.62;        // board plus sensor
co2_gap = 0.8;       // air gap between the sensor's top and the cover
co2_xy = [-40, -9];  // between the speaker and the USB socket
co2_sensor_dx = -5;  // sensor can centre from the board centre (it's on the end away from the pins)

module slots(n, len, w, pitch) {
  for (i = [0:n - 1]) translate([(i - (n - 1) / 2) * pitch, 0]) offset(w / 2 - eps) square([eps, len - w], center = true);
}

// A board that slides in flat on the inside of the cover: grooved rails along two edges, a stop
// at the closed end and a snap latch at the open end. pcb = [length (slide direction), width,
// thickness]; ledge = height of the board's underside; arm_up = how far the latch arms rise
// above the board (taller arms bend more easily).
module slide_holder(xy, pcb, ledge, arm_up = 1, arm_t = 1.0) {
  len = pcb[0] + 0.5;
  hw = pcb[1] / 2 + 0.3;          // groove back, from the board's centre line
  top = ledge + pcb[2] + 0.2;     // groove top; the lip above it slopes at 45
  rail = 1.6;
  translate([xy[0], xy[1], 0]) {
    rail_len = len - 2.5;  // rails stop short of the open end, where the latch arms are
    for (sy = [-1, 1]) translate([-len / 2, 0, 0]) rotate([90, 0, 90]) linear_extrude(rail_len)
      polygon([for (q = [[hw - 1.0, back_t - eps], [hw - 1.0, ledge], [hw, ledge], [hw, top],
                         [hw - 1, top + 1], [hw - 1, top + 1.6], [hw + rail, top + 1.6], [hw + rail, back_t - eps]])
               [sy * q[0], q[1]]]);
    // Latch: two free-standing arms at the open end, rooted only at the cover, so they flex
    // sideways. Each has a wedge facing the open end: the board's end pushes the arms apart as
    // it slides in, and once it's home they spring back behind its end corners (0.6 mm catch).
    // To take the board out, spread the two arm tops with a fingernail and slide it back out.
    catch_x = len / 2 - 0.3;  // board's end when it's home, plus 0.2
    for (sy = [-1, 1]) translate([0, 0, back_t - eps]) linear_extrude(top + arm_up - back_t)
      polygon([for (q = [[len / 2 - 2, hw], [len / 2 + 4, hw], [len / 2 + 4, hw + arm_t], [len / 2 - 2, hw + arm_t]]) [q[0], sy * q[1]]]);
    for (sy = [-1, 1]) translate([0, 0, back_t - eps]) linear_extrude(top + arm_up - back_t)
      polygon([for (q = [[catch_x, hw + eps], [catch_x, hw - 0.9], [catch_x + 3, hw + eps]]) [q[0], sy * q[1]]]);
    // Stop at the closed end: only as high as the board, so it clears anything on that end
    translate([-len / 2 - rail, -hw - rail, back_t - eps]) cube([rail, 2 * (hw + rail), ledge + pcb[2] - back_t]);
  }
}

module co2_holder() slide_holder(co2_xy, co2_pcb, back_t + co2_h - co2_pcb[2] + co2_gap);

// Sensor hub (perfboard trimmed to 5 columns: 31.85 x 14.75 mm): on the inside
// of the cover in the middle, headers facing the board, 3.5 mm off the cover so the solder
// bridges and the IO12 / 5 V wires underneath are clear. Same rails and snap latch as the CO2
// sensor. Its plugs stand about 20 mm off the cover; the board's back is about 34 mm away.
hub_pcb = [31.85, 14.75, 1.5];   // slide direction (along the bus columns), width, thickness (measured)
hub_xy = [6, 0];
hub_lift = 3.5;
module hub_holder() slide_holder(hub_xy, hub_pcb, back_t + hub_lift, arm_up = 4, arm_t = 0.8);

module back_cover() {
  lip_h = 3; lip_w = 1.2; fit = 0.3;
  difference() {
    union() {
      linear_extrude(back_t) rounded_rect(outer_w, outer_h, r_corner);
      // Locating lip that sits just inside the shell, cut away at the corner gussets
      difference() {
        translate([0, 0, back_t - eps]) linear_extrude(lip_h) difference() {
          square([inner_w - 2 * fit, inner_h - 2 * fit], center = true);
          square([inner_w - 2 * fit - 2 * lip_w, inner_h - 2 * fit - 2 * lip_w], center = true);
        }
        for (s = corner_signs()) translate(concat(cover_screw_xy(s), [0])) cube([gusset + 2, gusset + 2, 20], center = true);
      }
      // Fence that locates the speaker (glue it in)
      translate(concat(speaker_xy, [back_t - eps])) linear_extrude(3) difference() {
        offset(2.3) square(speaker + [0.6, 0.6], center = true);
        square(speaker + [0.6, 0.6], center = true);
      }
      co2_holder();
      hub_holder();
      // Two small fingers that stop the light sensor sliding back out of its rails: they sit
      // just behind the board's pin end once the cover is on (between the top vents, and
      // only as low as the board, so the wires underneath are free)
      light_y0 = inner_h / 2 - light_gap - (pcb_t + 0.4);
      for (sx = [-1, 1]) translate([sx * 5 - 0.5, light_y0, back_t - eps])
        cube([1, inner_h / 2 - 1.5 - light_y0 + 0.1, depth - light_z1 - 0.3 + eps]);
    }
    // Countersunk cover screws
    for (s = corner_signs()) translate(concat(cover_screw_xy(s), [0])) {
      translate([0, 0, -1]) cylinder(d = screw_clear, h = back_t + 2);
      translate([0, 0, -eps]) cylinder(d1 = screw_head, d2 = screw_clear, h = (screw_head - screw_clear) / 2);
    }
    // Speaker grille
    translate(concat(speaker_xy, [-1])) linear_extrude(back_t + 2) intersection() {
      offset(2) square(speaker - [8, 8], center = true);
      for (i = [-6:6]) translate([i * 3.2, 0]) square([1.6, speaker[1]], center = true);
    }
    // Vents: warm air out the top, cool air in the bottom (keeps the sensor honest)
    for (y = [outer_h / 2 - 9, -outer_h / 2 + 9])
      translate([10, y, -1]) linear_extrude(back_t + 2) slots(12, 9, 2.2, 5);
    // Vents in front of the CO2 sensor
    translate([co2_xy[0] + co2_sensor_dx, co2_xy[1], -1]) linear_extrude(back_t + 2) slots(4, 10, 1.6, 2.8);
    // Sensor cable hole, and two holes for the pod's screws: they go in from inside the case,
    // heads countersunk into the inner face, into inserts in the pod's ears
    translate(concat(sensor_xy, [-1])) {
      cylinder(d = 7, h = back_t + 2);
      for (dx = [-pod_hole_dx, pod_hole_dx]) translate([dx, 0, 0]) {
        cylinder(d = screw_clear, h = back_t + 2);
        translate([0, 0, 1 + back_t - (screw_head - screw_clear) / 2]) cylinder(d1 = screw_clear, d2 = screw_head, h = (screw_head - screw_clear) / 2 + eps);
      }
    }
    // USB-C panel socket: its flange sits in a pocket on the inside, screws go in from outside
    if (usb_exit == "back") translate(concat(usb_jack_xy, [0])) {
      translate([0, 0, usb_panel_t]) linear_extrude(back_t) offset(0.3) square(usb_flange - [usb_flange[1], 0], center = true);
      translate([0, 0, usb_panel_t]) for (sx = [-1, 1]) translate([sx * (usb_flange[0] - usb_flange[1]) / 2, 0, 0]) cylinder(d = usb_flange[1] + 0.6, h = back_t);
      translate([0, 0, -1]) linear_extrude(back_t + 2) hull() for (sx = [-1, 1]) translate([sx * (usb_open[0] - usb_open[1]) / 2, 0]) circle(d = usb_open[1]);
      for (sx = [-1, 1]) translate([sx * usb_screw_dx / 2, 0, -1]) cylinder(d = usb_screw_clear, h = back_t + 2);
    }
  }
}

// ---------- sensor pod (for an SHT31 breakout, 12.56 x 10.72 mm) ----------
// The board stands on edge in two grooved ribs, so air reaches both faces. Its four pins
// (VIN, GND, SCL, SDA) run along a 10.72 mm edge: it slides in from the open side with that
// edge last (toward the case and the cable notch), until it touches the top. No glue or
// latch: once the pod is screwed on, the cover closes the open side and traps the board.
sensor_pcb = [10.72, 12.56, 1.6];   // width (between the grooves), height, thickness

pod_in = [28, 21]; pod_t = 1.6; pod_ear = 8;
pod_h = sensor_pcb[1] + 0.5;  // inside height
pod_ear_t = 6.5;              // thick enough for an insert; also an air gap to the warm case

// Vent slot with pointed ends (45 degrees), so neither end overhangs whichever way it prints.
module pointed_slots(n, len, w, pitch) {
  for (i = [0:n - 1]) translate([(i - (n - 1) / 2) * pitch, 0]) hull()
    for (sy = [-1, 1]) translate([0, sy * (len - w) / 2]) rotate(45) square(w / sqrt(2), center = true);
}

// As printed: roof on the bed.
module pod_print() { translate([0, 0, pod_h + pod_t]) mirror([0, 0, 1]) pod(); }

module pod() {
  in = pod_in; h = pod_h; t = pod_t; ear = pod_ear;
  wall_x = in[0] / 2 + t;                // outside of the end walls
  reach = pod_hole_dx + ear / 2 - wall_x;  // how far an ear sticks out past the wall
  // Grooved ribs from the two long walls, full height (prints without support)
  for (sy = [-1, 1]) difference() {
    translate([-2, sy > 0 ? sensor_pcb[0] / 2 - 0.9 : -in[1] / 2 - eps, 0])
      cube([4, in[1] / 2 - sensor_pcb[0] / 2 + 0.9 + 2 * eps, h + eps]);
    translate([-(sensor_pcb[2] + 0.4) / 2, sy > 0 ? sensor_pcb[0] / 2 - 1.2 : -sensor_pcb[0] / 2 - 0.3, -1])
      cube([sensor_pcb[2] + 0.4, 1.5, h + 2]);
  }
  difference() {
    union() {
      linear_extrude(h + t) offset(t) square(in, center = true);
      // Ears with inserts; standing the pod off the case leaves an air gap to the warm case
      for (dx = [-pod_hole_dx, pod_hole_dx]) translate([dx, 0, 0]) cylinder(d = ear, h = pod_ear_t);
      translate([0, 0, 0]) linear_extrude(pod_ear_t) square([2 * pod_hole_dx, ear], center = true);
      // 45 degree ramps from the end walls out to the ears, so they don't start in mid-air
      // when the pod prints roof down
      for (sx = [-1, 1]) hull() {
        translate([sx * pod_hole_dx, 0, pod_ear_t - eps]) cylinder(d = ear, h = eps);
        translate([sx * (wall_x + 1) / 2, 0, pod_ear_t - eps]) linear_extrude(eps) square([wall_x - 1, ear], center = true);
        translate([sx * (wall_x - 0.5), 0, min(pod_ear_t + reach, h + t) - eps]) linear_extrude(eps) square([1, ear], center = true);
      }
    }
    translate([0, 0, -eps]) linear_extrude(h) square(in, center = true);
    // Inserts go in from the face that sits against the case
    for (dx = [-pod_hole_dx, pod_hole_dx]) translate([dx, 0, -eps]) cylinder(d = insert_hole, h = insert_depth);
    // Vent slots on every side
    for (a = [0, 180]) rotate(a) translate([0, in[1] / 2, h / 2]) rotate([90, 0, 0]) linear_extrude(2 * t + 1, center = true) pointed_slots(6, 6, 2, 4);
    for (a = [90, 270]) rotate(a) translate([0, in[0] / 2, h / 2]) rotate([90, 0, 0]) linear_extrude(2 * t + 1, center = true) pointed_slots(4, 6, 2, 4);
    translate([0, 0, h - 1]) linear_extrude(t + 2) slots(6, 14, 2, 4);
    // Cable notch toward the case
    translate([0, 0, -1]) cylinder(d = 7, h = 4);
  }
}

// ---------- base: keeps a tap on the screen from tipping the clock over ----------
// Without it the clock (about 250 g, centre of mass ~18 mm behind the glass on a 46 mm deep
// footprint) tips back with under 1 N pushed at the top of the screen; a firm tap is 2-3 N.
// The base adds 30 mm of foot behind the clock and a pocket for ~150 g of steel under the
// screen: then it takes about 3 N at the very top of the screen and 5 N at the middle.
// Coordinates here: X = across, Y = front (0) to back, Z = up, as it prints.
base_rim = 3;        // wall around the clock's footprint
base_tail = 30;      // foot sticking out behind the back cover
base_recess = 4;     // how deep the clock sits in it
base_floor = 1.5;    // under the weight pocket
weight_pocket = [142, 40, 5.5];  // under the screen: two strips of twelve 1/4 oz stick-on wheel weights side by side
base_fit = 0.4;
base_h = weight_pocket[2] + base_floor + base_recess;
case_d = depth + back_t;

module base() {
  w = outer_w + 2 * base_fit + 2 * base_rim;
  y0 = -base_fit - base_rim;
  y_rim = case_d + base_fit + base_rim;   // back of the rim
  difference() {
    union() {
      // Full height around the clock
      translate([0, (y0 + y_rim) / 2, 0]) linear_extrude(base_h) rounded_rect(w, y_rim - y0, 4);
      // Lower tail behind it (keeps clear of the sensor pod)
      translate([0, (y0 + case_d + base_tail) / 2, 0]) linear_extrude(base_h - base_recess) rounded_rect(w, case_d + base_tail - y0, 4);
    }
    // Recess for the clock
    translate([0, case_d / 2, base_h - base_recess]) linear_extrude(base_recess + 1)
      square([outer_w + 2 * base_fit, case_d + 2 * base_fit], center = true);
    // Weight pocket in the recess floor, under the front half of the clock. It opens upward so
    // it prints without a 40 mm bridge; the clock sits on top and hides the weights.
    translate([0, 3 + weight_pocket[1] / 2, base_h - base_recess - weight_pocket[2]]) linear_extrude(weight_pocket[2] + eps)
      offset(1) square([weight_pocket[0] - 2, weight_pocket[1] - 2], center = true);
  }
}

// ---------- fit test: thin front slice with the window and standoffs ----------

module fit_test() {
  intersection() {
    shell();
    translate([0, 0, -1]) linear_extrude(front_t + stack_h + 1) square([outer_w + 2, outer_h + 2], center = true);
  }
}

// Quicker fit test: front and walls only 1.2 mm thick, and big openings beside the window. Still checks the screw holes (inserts go in), the board
// outline and the window position.
module fit_quick() {
  cut = front_t - 1.2;
  translate([0, 0, -cut]) difference() {
    intersection() {
      fit_test();
      translate([0, 0, cut]) linear_extrude(20) square([inner_w + 2.4, inner_h + 2.4], center = true);  // 1.2 mm walls
    }
    // openings in the front beside the window, leaving bars round the posts
    for (sx = [-1, 1]) translate([sx * (outer_w / 2 + active_w / 2 + 2) / 2, board_dy, cut - 1])
      linear_extrude(front_t + 2) square([(outer_w - active_w) / 2 - 2 * wall - 6, board_h - 2 * hole_inset - insert_boss - 4], center = true);
  }
}

// ---------- stand-ins for previews ----------

module board_dummy() {
  translate([0, board_dy, 0]) {
  color("#2a2a2a") translate([0, -0.5, front_t]) linear_extrude(stack_h) square([121, 76], center = true);
  color("#e8a33a") translate([0, 0, front_t + stack_h]) linear_extrude(pcb_t) square([board_w, board_h], center = true);
  color("#111") translate([-active_dx, active_dy, front_t - 0.2]) linear_extrude(0.2) square([active_w, active_h], center = true);
  }
}

if (part == "base") base();
else if (part == "shell") shell();
else if (part == "back") back_cover();
else if (part == "pod") pod_print();
else if (part == "fit") fit_test();
else if (part == "fitq") fit_quick();
else rotate([90, 0, 180]) {  // stood up, for viewing; part = "exploded" pulls the back off
  gap = part == "exploded" ? 45 : 0;
  color("#555a63") shell();
  board_dummy();
  color("#62676f") translate([0, 0, depth + back_t + gap]) mirror([0, 0, 1]) back_cover();
  color("#d9d4cc") translate(concat(sensor_xy, [depth + back_t + gap])) pod();
  // Base under it: its (X, Y, Z) is the case's (x, z, y), with the clock on the recess floor
  color("#4a4e55") multmatrix([[1, 0, 0, 0], [0, 0, 1, -outer_h / 2 - (base_h - base_recess) - gap / 3], [0, 1, 0, 0]]) base();
}

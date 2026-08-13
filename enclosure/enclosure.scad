/*
 * Water Rower Monitor Enclosure
 * Replaces original WaterRower USA monitor
 *
 * Hardware:
 *   - Waveshare ESP32-S3-Touch-LCD-2.8 (single board: screen, touch, speaker
 *     amp and battery charger are all on it — no breadboard, no key strip)
 *   - Supplied 8Ω 2W speaker
 *   - Power comes from an external USB power bank (2x 18650), strapped to the
 *     rower's arm and plugged into the board's USB-C. Nothing about the
 *     battery lives in here, which is what keeps the case thin.
 *
 * Stack, bottom to top:
 *   floor -> speaker + cable bay -> PCB back components -> PCB -> lid,
 *   with the touch glass poking through the lid window so its surface sits
 *   flush with the lid's outer face.
 *
 * Mounts via original tube socket (18mm rod)
 * Printer: Creality CR-10S
 *
 * Export STL: F6 (Render) -> F7 (Export STL)
 * Print: 0.2mm layer, 20% infill, PLA/PETG
 */

$fn = 50;

// ===== Board: Waveshare ESP32-S3-Touch-LCD-2.8 =====
// Published outline is 73.06 x 50.54 mm. MEASURE YOURS before printing —
// every other dimension in this file is derived from these two.
pcb_w = 50.6;            // short side (across the screen)
pcb_l = 73.1;            // long side
pcb_t = 1.6;             // PCB thickness
pcb_back_h = 6;          // tallest part on the back (module can, connectors)

// ===== Touch glass =====
// Visible area of a 2.8" 240x320 panel at 0.1779 mm dot pitch, plus the black
// border of the bonded glass. MEASURE — a wrong window ruins the lid.
screen_w = 42.7;
screen_l = 56.9;
bezel    = 3.5;          // glass border around the visible area
glass_w  = screen_w + bezel * 2;
glass_l  = screen_l + bezel * 2;
glass_h  = 3.5;          // glass + FPC stack above the PCB front face
// Glass position on the PCB, measured from the PCB's lower-left corner with
// the screen facing you. Defaults to centred — MEASURE and correct.
glass_off_x = (pcb_w - glass_w) / 2;
glass_off_y = (pcb_l - glass_l) / 2;

// ===== USB-C port =====
// The power bank lives outside the case, so this opening carries the supply
// cable permanently — sized for a plug shell, not just the connector. A
// right-angle USB-C cable keeps the strain off it.
usb_edge_off = pcb_w / 2;   // along the bottom short edge, from PCB left
usb_w = 14;
usb_h = 7;
lid_usb_notch = true;       // cut a matching relief in the lid edge

// ===== Supplied speaker (8Ω 2W, 2030) =====
spk_w = 30;
spk_l = 20;
spk_h = 5;

// ===== Original Monitor Tube Mount =====
tube_od = 18;                     // 18mm tube (measured!)
tube_socket_id = tube_od + 0.5;   // socket inner diameter (tight fit)
tube_socket_depth = 40;           // how deep the tube goes in
tube_socket_od = tube_od + 8;     // socket wall thickness
tube_screw_span = 20;             // mounting ears, +/- from centre

// ===== Enclosure =====
wall        = 2.5;
corner_r    = 5;
post_margin = 8;      // ring around the PCB that the corner screw posts live in
                      // (8 keeps the screw holes clear of the cradle wall)
post_d      = 6;      // screw post outer diameter
screw_d     = 2.6;    // self-tapping screw core hole
bay_h       = 6;      // speaker + cable layer under the PCB (speaker is 5 mm)

inner_w = pcb_w + post_margin * 2;
inner_l = pcb_l + post_margin * 2;
// Interior height lands exactly on the PCB's front face, so the lid closes flat
// against it and the glass fills the window.
inner_h = bay_h + pcb_back_h + pcb_t;

outer_w = inner_w + wall * 2;
outer_l = inner_l + wall * 2;
outer_h = inner_h + wall;

lid_h = glass_h;      // glass sits flush with the lid's outer face

// PCB origin inside the case
pcb_x = wall + post_margin;
pcb_y = wall + post_margin;
pcb_z = wall + bay_h + pcb_back_h;   // underside of the PCB

// Corner posts sit in the ring outside the PCB footprint
post_inset = wall + post_d / 2 + 0.5;

// Sensor cable slot
cable_w = 8;
cable_h = 4;

// ===== Modules =====

module rounded_box(w, l, h, r) {
    hull() {
        for (x = [r, w - r])
            for (y = [r, l - r])
                translate([x, y, 0])
                    cylinder(h = h, r = r);
    }
}

// ───── Tube Socket (clamps onto the original 18mm arm) ─────
module tube_socket() {
    difference() {
        union() {
            cylinder(d = tube_socket_od, h = tube_socket_depth);

            // Transition flange to mount on case bottom
            translate([0, 0, tube_socket_depth - 3])
                hull() {
                    cylinder(d = tube_socket_od, h = 3);
                    for (a = [-1, 1])
                        translate([a * tube_screw_span, 0, 0])
                            cylinder(d = 12, h = 3);
                }
        }

        // Tube hole
        translate([0, 0, -1])
            cylinder(d = tube_socket_id, h = tube_socket_depth - 3 + 2);

        // Slit for clamping (so it grips the tube)
        translate([-1, -tube_socket_od / 2 - 1, -1])
            cube([2, tube_socket_od + 2, tube_socket_depth + 2]);

        // Set screw hole (M4, to lock tube in place)
        translate([0, 0, tube_socket_depth / 2])
            rotate([0, 90, 0])
                cylinder(d = 4, h = tube_socket_od, center = true);

        // Mounting screw holes (to attach to case)
        for (a = [-1, 1])
            translate([a * tube_screw_span, 0, tube_socket_depth - 5])
                cylinder(d = 3.2, h = 10);
    }
}

// ───── Bottom Case ─────
// Layout (top view):
//
//  ┌──────────────────────────────────┐
//  │ o                              o │  o = corner screw post
//  │   ┌──────────────────────────┐   │
//  │   │  PCB cradle 50.6 x 73.1  │   │
//  │   │  (battery + speaker bay  │   │
//  │   │   underneath)            │   │
//  │   └──────────────────────────┘   │
//  │ o                              o │
//  └──────────────────────────────────┘
//     USB-C edge            sensor cable slot
//
module bottom_case() {
    spk_cx = pcb_x + pcb_w / 2;
    spk_cy = pcb_y + pcb_l - 4 - spk_l / 2;

    difference() {
        union() {
            // Shell with the main cavity already hollowed out. The cradle and
            // posts are added on top of this, so the cavity cannot eat them.
            difference() {
                rounded_box(outer_w, outer_l, outer_h, corner_r);
                translate([wall, wall, wall])
                    rounded_box(inner_w, inner_l, inner_h + 1, max(corner_r - 1, 1));
            }

            // PCB cradle: a solid block; the bay and the board rebate are cut
            // out of it below, leaving ~1.8 mm walls around the board.
            translate([pcb_x - 2, pcb_y - 2, wall])
                cube([pcb_w + 4, pcb_l + 4, inner_h]);

            // Corner screw posts
            for (x = [post_inset, outer_w - post_inset])
                for (y = [post_inset, outer_l - post_inset])
                    translate([x, y, wall])
                        cylinder(d = post_d, h = inner_h);

            // Tube-socket screw bosses. Blind holes, so the bay floor stays
            // sealed and no screw tip pokes up into the board.
            for (a = [-1, 1])
                translate([outer_w / 2 + a * tube_screw_span, outer_l / 2, wall])
                    cylinder(d = 9, h = bay_h);
        }

        // Battery + speaker bay, and clearance for the PCB's back components
        translate([pcb_x + 2, pcb_y + 2, wall])
            cube([pcb_w - 4, pcb_l - 4, pcb_z - wall]);

        // PCB rebate — the board drops in from above and is trapped sideways
        translate([pcb_x - 0.2, pcb_y - 0.2, pcb_z])
            cube([pcb_w + 0.4, pcb_l + 0.4, pcb_t + 2]);

        // USB-C notch, cut from the outside wall through to the board edge
        translate([pcb_x + usb_edge_off - usb_w / 2, -1, pcb_z - 1.5])
            cube([usb_w, pcb_y + 3, usb_h]);

        // Sensor cable slot (long side wall, opposite the battery door)
        translate([outer_w - wall - 1, pcb_y + pcb_l / 2 - cable_w / 2, pcb_z - 1])
            cube([wall + 2, cable_w, cable_h]);

        // Speaker grille in the floor
        for (ix = [-3:3])
            for (iy = [-2:2])
                translate([spk_cx + ix * 4.5, spk_cy + iy * 4.5, -1])
                    cylinder(d = 2.5, h = wall + 2);

        // Tube socket screw holes — blind, into the bosses above
        for (a = [-1, 1]) {
            translate([outer_w / 2 + a * tube_screw_span, outer_l / 2, -1])
                cylinder(d = 2.8, h = wall + bay_h);
            // Countersink so the screw heads sit flush with the outside face
            translate([outer_w / 2 + a * tube_screw_span, outer_l / 2, -0.01])
                cylinder(d1 = 6.4, d2 = 2.8, h = 1.9);
        }

        // Corner post screw holes
        for (x = [post_inset, outer_w - post_inset])
            for (y = [post_inset, outer_l - post_inset])
                translate([x, y, wall])
                    cylinder(d = screw_d, h = inner_h + 1);
    }

    // Speaker retaining lips on the bay floor
    for (a = [-1, 1]) {
        translate([spk_cx + a * (spk_w / 2 + 0.5) - 0.75, spk_cy - spk_l / 2, wall])
            cube([1.5, spk_l, spk_h]);
        translate([spk_cx - spk_w / 2, spk_cy + a * (spk_l / 2 + 0.5) - 0.75, wall])
            cube([spk_w, 1.5, spk_h]);
    }
}

// ───── Lid ─────
module lid() {
    win_x = pcb_x + glass_off_x - 0.3;
    win_y = pcb_y + glass_off_y - 0.3;

    difference() {
        rounded_box(outer_w, outer_l, lid_h, corner_r);

        // Screen window — the glass fills it and ends flush with the top face
        translate([win_x, win_y, -1])
            cube([glass_w + 0.6, glass_l + 0.6, lid_h + 2]);

        // USB-C relief. If the connector is on the board's front face it would
        // otherwise hit the lid; the notch only eats the margin outside the
        // screen window. Set lid_usb_notch = false if your board's port sits
        // low enough to clear it.
        if (lid_usb_notch)
            translate([pcb_x + usb_edge_off - usb_w / 2 - 0.5, -1, -1])
                cube([usb_w + 1, pcb_y + 2, lid_h + 2]);

        // Engraved label on the top margin, on the line between the two screw
        // heads. It cannot go on the bottom margin: the USB-C relief occupies
        // the middle of that edge and would cut the text in half.
        translate([outer_w / 2, outer_l - post_inset - 2.25, lid_h - 0.6])
            linear_extrude(1)
                text("WATER ROWER", size = 4.5, halign = "center");

        // Screw holes + countersink
        for (x = [post_inset, outer_w - post_inset])
            for (y = [post_inset, outer_l - post_inset]) {
                translate([x, y, -1])
                    cylinder(d = 3.2, h = lid_h + 2);
                translate([x, y, lid_h - 1.6])
                    cylinder(d1 = 3.2, d2 = 6, h = 1.7);
            }
    }
}

// ===== Render =====
// PART=0: all (preview), 1: bottom_case, 2: lid, 3: tube_socket
// There is no battery door: the power bank is external.
// CLI export: openscad -o lid.stl -D "PART=2" enclosure.scad
PART = 0;

if (PART == 0 || PART == 1) bottom_case();

if (PART == 0 || PART == 2)
    translate([0, PART == 0 ? outer_l + 15 : 0, 0])
        lid();

if (PART == 0 || PART == 3)
    translate([PART == 0 ? outer_w + 30 : 0, PART == 0 ? outer_l / 2 : 0, 0])
        tube_socket();

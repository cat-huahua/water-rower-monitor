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
 * Board geometry is READ OFF THE VENDOR OUTLINE DRAWING, not guessed:
 *   files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8/
 *     ESP32-S3-Touch-LCD-2.8_structure.pdf
 * Two facts from it drive the whole design:
 *   1. The board has four M2.5-ish mounting holes, 4.50 mm in from each edge.
 *      The board is screwed to posts — nothing clamps its edges.
 *   2. The touch glass (50.54 x 73.06) is BIGGER than the PCB (49.90 x 69.00)
 *      and overhangs it on every edge. So no wall may rise past the PCB front
 *      face anywhere near the board, and the lid window has to clear the
 *      glass, not the PCB.
 *
 * Stack, bottom to top:
 *   floor -> speaker + cable bay -> PCB back components -> PCB -> glass,
 *   with the glass filling the lid window so its surface sits flush with the
 *   lid's outer face.
 *
 * Mounts via original tube socket (18mm rod)
 * Printer: Creality CR-10S
 *
 * Export STL: F6 (Render) -> F7 (Export STL)
 * Print: 0.2mm layer, 20% infill, PLA/PETG
 */

$fn = 50;

// ===== Board: Waveshare ESP32-S3-Touch-LCD-2.8 =====
pcb_w = 49.90;           // drawing: PCB width
pcb_l = 69.00;           // drawing: PCB length
pcb_t = 1.6;
// Drawing gives a 10.00 mm total stack with TP+LCD 3.60 mm above the PCB front
// face, so everything below that face is 6.40 mm; less the board itself leaves
// 4.80 mm of back-side components. 5.0 for luck.
pcb_back_h = 5.0;

// Mounting holes: 4 off, 4.50 mm in from every edge — the drawing dimensions
// the resulting centres as 41.00 x 60.00.
hole_edge = 4.50;
// The drawing does not dimension the hole diameter. Posts are sized for M2.5
// self-tappers; if your holes are M3 clearance the same screws still work,
// just with more slop.
pcb_screw_d = 2.1;       // self-tapper core hole in the post
pcb_post_od = 5.5;

// ===== Touch glass — OVERHANGS the PCB on every edge =====
glass_w = 50.54;         // drawing: TP OD
glass_l = 73.06;         // drawing: TP OD — 4.06 mm longer than the PCB
glass_h = 3.60;          // drawing: TP+LCD above the PCB front face
screen_w = 43.20;        // drawing: LCD AA (visible area)
screen_l = 57.60;        // drawing: LCD AA
// Glass lower-left corner relative to the PCB's lower-left corner, screen
// facing you. Negative because the glass hangs over the board. Centred across
// the width; lengthwise the drawing's 1.9 mm callout puts that much of the
// 4.06 mm overhang at the top, leaving 2.16 mm at the USB-C end.
glass_off_x = -(glass_w - pcb_w) / 2;   // -0.32
glass_off_y = -2.16;

// ===== USB-C port =====
// The board's front face is entirely under glass, so every connector is on the
// back — the opening therefore straddles the PCB underside, not its front.
// The power bank lives outside, so this carries the supply cable permanently:
// sized for a plug shell, and a right-angle cable keeps strain off it.
usb_edge_off = pcb_w / 2;   // along the bottom short edge, from PCB left
usb_w = 14;
usb_h = 8;
lid_usb_notch = false;      // not needed: nothing protrudes above the PCB

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
// The glass is 73.06 long against a 69.00 board, so the lid window reaches
// far past the board's ends. 10 mm of margin is what keeps the window clear
// of the corner screw heads — do not reduce it without re-checking that.
post_margin = 10;
post_d      = 6;      // corner (lid) screw post outer diameter
screw_d     = 2.6;    // lid self-tapping screw core hole
bay_h       = 6;      // speaker + cable layer under the board

inner_w = pcb_w + post_margin * 2;
inner_l = pcb_l + post_margin * 2;
post_h  = bay_h + pcb_back_h;      // floor to PCB underside
inner_h = post_h + pcb_t;          // interior ends flush with the PCB front
outer_w = inner_w + wall * 2;
outer_l = inner_l + wall * 2;
outer_h = wall + inner_h;

lid_h = glass_h;      // glass ends flush with the lid's outer face

// PCB origin inside the case
pcb_x = wall + post_margin;
pcb_y = wall + post_margin;
pcb_z = wall + post_h;             // underside of the PCB

// Corner posts sit in the ring outside the PCB footprint
post_inset = wall + post_d / 2 + 0.5;

// Speaker centre — clear of the four board posts and the tube bosses
spk_cx = outer_w / 2;
spk_cy = pcb_y + pcb_l - 4 - spk_l / 2;

// Sensor cable slot. Sits in the bay, below the board, because the 12PIN
// header is on the back like everything else.
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

// The four board mounting positions, in case coordinates
module at_board_holes() {
    for (x = [pcb_x + hole_edge, pcb_x + pcb_w - hole_edge])
        for (y = [pcb_y + hole_edge, pcb_y + pcb_l - hole_edge])
            translate([x, y, 0])
                children();
}

module at_corner_posts() {
    for (x = [post_inset, outer_w - post_inset])
        for (y = [post_inset, outer_l - post_inset])
            translate([x, y, 0])
                children();
}

module at_tube_bosses() {
    for (a = [-1, 1])
        translate([outer_w / 2 + a * tube_screw_span, outer_l / 2, 0])
            children();
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
//  ┌────────────────────────────────────┐
//  │ o                                o │  o = corner post (lid screws)
//  │    ·        speaker grille      ·  │  · = board post (M2.5)
//  │                                    │
//  │    [tube boss]      [tube boss]    │
//  │                                    │
//  │    ·                            ·  │
//  │ o                                o │
//  └────────────────────────────────────┘
//     USB-C edge          sensor cable slot (right wall)
//
// The board floats on the four inner posts; nothing touches its edges, which
// is required because the glass overhangs them.
module bottom_case() {
    difference() {
        union() {
            // Shell with the main cavity already hollowed out, so the posts
            // added below cannot be eaten by it.
            difference() {
                rounded_box(outer_w, outer_l, outer_h, corner_r);
                translate([wall, wall, wall])
                    rounded_box(inner_w, inner_l, inner_h + 1, max(corner_r - 1, 1));
            }

            // Board mounting posts
            at_board_holes()
                translate([0, 0, wall])
                    cylinder(d = pcb_post_od, h = post_h - wall);

            // Corner screw posts for the lid
            at_corner_posts()
                translate([0, 0, wall])
                    cylinder(d = post_d, h = inner_h);

            // Tube-socket screw bosses. Blind holes, so the floor stays sealed
            // and no screw tip pokes up into the board.
            at_tube_bosses()
                translate([0, 0, wall])
                    cylinder(d = 9, h = bay_h);
        }

        // Board post pilot holes
        at_board_holes()
            translate([0, 0, wall + 1])
                cylinder(d = pcb_screw_d, h = post_h);

        // USB-C opening. Every connector is on the board's back, so this
        // straddles the PCB underside rather than sitting above it.
        translate([pcb_x + usb_edge_off - usb_w / 2, -1, pcb_z - usb_h + 1.5])
            cube([usb_w, pcb_y + 3, usb_h]);

        // Sensor cable slot (long side wall), down in the bay
        translate([outer_w - wall - 1, pcb_y + pcb_l / 2 - cable_w / 2,
                   pcb_z - cable_h - 1])
            cube([wall + 2, cable_w, cable_h]);

        // Speaker grille in the floor
        for (ix = [-3:3])
            for (iy = [-2:2])
                translate([spk_cx + ix * 4.5, spk_cy + iy * 4.5, -1])
                    cylinder(d = 2.5, h = wall + 2);

        // Tube socket screw holes — blind, into the bosses above
        at_tube_bosses() {
            translate([0, 0, -1])
                cylinder(d = 2.8, h = wall + bay_h);
            // Countersink so the screw heads sit flush with the outside face
            translate([0, 0, -0.01])
                cylinder(d1 = 6.4, d2 = 2.8, h = 1.9);
        }

        // Corner post screw holes
        at_corner_posts()
            translate([0, 0, wall])
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
// The window clears the GLASS, not the PCB — the glass is the bigger part.
// The lid never touches the board; the four posts hold it.
module lid() {
    win_x = pcb_x + glass_off_x - 0.3;
    win_y = pcb_y + glass_off_y - 0.3;

    difference() {
        rounded_box(outer_w, outer_l, lid_h, corner_r);

        // Screen window — the glass fills it and ends flush with the top face
        translate([win_x, win_y, -1])
            cube([glass_w + 0.6, glass_l + 0.6, lid_h + 2]);

        // Optional USB-C relief, off by default: nothing on this board rises
        // above the PCB front face, so the lid edge clears the connector.
        if (lid_usb_notch)
            translate([pcb_x + usb_edge_off - usb_w / 2 - 0.5, -1, -1])
                cube([usb_w + 1, pcb_y + 2, lid_h + 2]);

        // Engraved label on the top margin, on the line between the two screw
        // heads — the only strip of lid left once the window is cut.
        translate([outer_w / 2, outer_l - post_inset - 2.25, lid_h - 0.6])
            linear_extrude(1)
                text("WATER ROWER", size = 4.5, halign = "center");

        // Screw holes + countersink
        at_corner_posts() {
            translate([0, 0, -1])
                cylinder(d = 3.2, h = lid_h + 2);
            translate([0, 0, lid_h - 1.6])
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

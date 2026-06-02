/*
 * Water Rower Monitor Enclosure
 * Replaces original WaterRower USA monitor
 *
 * Hardware:
 *   - Large half breadboard (165x55mm)
 *   - ESP32-S3 DevKitC-1
 *   - ST7735S 1.8" 128x160 TFT with 4 built-in keys (K1,K2,#,*)
 *
 * Mounts via original tube socket (18mm rod)
 * Speaker mounted on bottom case wall
 * Printer: Creality CR-10S
 *
 * Export STL: F6 (Render) -> F7 (Export STL)
 * Print: 0.2mm layer, 20% infill, PLA/PETG
 */

$fn = 50;

// ===== Large Half Breadboard (830 holes) =====
bb_w = 165;    // long side
bb_l = 55;     // short side
bb_h = 10;     // height with pins

// ===== TFT Module (with 4 built-in keys on top edge) =====
// PCB dimensions (measure yours and adjust!)
tft_pcb_w = 52;    // total width including button strip
tft_pcb_l = 40;    // total height
tft_screen_w = 29; // visible screen area
tft_screen_l = 37; // visible screen area
tft_btn_strip_h = 10;  // button strip height above screen
// The whole front face: buttons on top, screen below
tft_front_w = tft_pcb_w;
tft_front_l = tft_screen_l + tft_btn_strip_h + 2;

// ===== Original Monitor Tube Mount =====
tube_od = 18;       // 18mm tube (measured!)
tube_socket_id = tube_od + 0.5;  // socket inner diameter (tight fit)
tube_socket_depth = 40;  // how deep the tube goes in
tube_socket_od = tube_od + 8;    // socket wall thickness

// ===== Speaker =====
// Oval speaker with mounting bracket (from photos)
spk_bracket_w = 55;   // bracket total width
spk_bracket_l = 30;   // bracket total height
spk_oval_w = 35;       // oval cone width
spk_oval_l = 24;       // oval cone height
spk_screw_spacing_w = 48;  // screw hole spacing width
spk_screw_spacing_l = 24;  // screw hole spacing height
spk_screw_d = 3;       // screw hole diameter

// ===== 5V 3000mAh USB-C Battery Pack =====
// 18650-based pack with built-in USB-C output
bat_w = 70;    // length
bat_l = 37;    // width
bat_h = 20;    // height

// ===== Enclosure =====
wall = 2.5;
corner_r = 4;
clearance = 1.5;

inner_w = bb_w + clearance * 2;
inner_l = bb_l + bat_l + clearance * 3;  // breadboard + battery side by side
inner_h = 28;

outer_w = inner_w + wall * 2;
outer_l = inner_l + wall * 2;
outer_h = inner_h + wall;

lid_h = 3.5;
lip = 1.5;

// USB cutout
usb_w = 14;
usb_h = 8;

// Sensor cable hole
cable_d = 6;

// Screw
screw_d = 2.5;
boss_d = 7;

// ===== TFT window position on lid =====
// Centered on the right half, showing screen + buttons
tft_win_x = outer_w / 2 + 5;
tft_win_y = (outer_l - tft_front_l) / 2;

// ===== Modules =====

module rounded_box(w, l, h, r) {
    hull() {
        for (x = [r, w - r])
            for (y = [r, l - r])
                translate([x, y, 0])
                    cylinder(h = h, r = r);
    }
}

// ───── Tube Socket (replaces rail clamp) ─────
module tube_socket() {
    difference() {
        union() {
            // Main socket cylinder
            cylinder(d = tube_socket_od, h = tube_socket_depth);

            // Transition flange to mount on case bottom
            translate([0, 0, tube_socket_depth - 3])
                hull() {
                    cylinder(d = tube_socket_od, h = 3);
                    // Flange ears for screw holes
                    for (a = [-1, 1])
                        translate([a * 20, 0, 0])
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
            translate([a * 20, 0, tube_socket_depth - 5])
                cylinder(d = 3.2, h = 10);
    }
}

// ───── Bottom Case ─────
// Layout (top view):
//
//  ┌──────────────────────────────────────────────┐
//  │  ┌─────────────────────────────────────────┐ │
//  │  │         Breadboard (165x55)             │ │
//  │  └─────────────────────────────────────────┘ │
//  │  ┌──────────────┐  ┌───────┐  ┌──────────┐  │
//  │  │   Battery     │  │Speaker│  │ (wiring) │  │
//  │  │   70x37x20   │  │       │  │          │  │
//  │  └──────────────┘  └───────┘  └──────────┘  │
//  └──────────────────────────────────────────────┘
//   USB                                    LDR cable

module bottom_case() {
    // Battery area Y start (below breadboard)
    bat_area_y = wall + clearance + bb_l + clearance;

    difference() {
        rounded_box(outer_w, outer_l, outer_h, corner_r);

        // Inner cavity
        translate([wall, wall, wall])
            rounded_box(inner_w, inner_l, inner_h + 1, max(corner_r - 1, 1));

        // USB-C cutout for battery cable (left short side, lower)
        // Battery USB-C cable routes to ESP32 USB-C
        translate([-1, bat_area_y + bat_l / 2 - usb_w / 2, wall + 2])
            cube([wall + 2, usb_w, usb_h]);

        // Also a notch on left side for ESP32 USB-C (upper area, for programming)
        translate([-1, wall + clearance + bb_l / 2 - usb_w / 2, wall + 4])
            cube([wall + 2, usb_w, usb_h]);

        // LDR 3-wire cable slot (right short side)
        translate([outer_w - wall - 1, outer_l / 2 - 5, wall + bb_h + 3])
            cube([wall + 2, 10, 4]);

        // Tube socket mounting holes (bottom center)
        for (a = [-1, 1])
            translate([outer_w / 2 + a * 20, outer_l / 2, -1])
                cylinder(d = 3.2, h = wall + 2);

        // Speaker grille (bottom wall)
        spk_gx = outer_w / 2;
        spk_gy = bat_area_y + bat_l / 2;
        for (ix = [-3:3])
            for (iy = [-2:2])
                translate([spk_gx + ix * 5, spk_gy + iy * 5, -1])
                    cylinder(d = 2.5, h = wall + 2);

        // Battery door opening (back long side wall)
        // Rectangular hole to slide battery in/out
        bat_door_w = bat_w + 4;
        bat_door_h = bat_h + 2;
        translate([wall + clearance + 3, outer_l - wall - 1, wall + 1])
            cube([bat_door_w, wall + 2, bat_door_h]);

        // Battery door slide grooves (channels in the wall edges)
        // Door slides in from above
        bat_door_groove = 1.5;
        // Left groove
        translate([wall + clearance + 3 - bat_door_groove, outer_l - wall - 0.5,
                   wall + 1])
            cube([bat_door_groove, wall + 1, bat_door_h + 4]);
        // Right groove
        translate([wall + clearance + 3 + bat_door_w, outer_l - wall - 0.5,
                   wall + 1])
            cube([bat_door_groove, wall + 1, bat_door_h + 4]);

        // Charging USB-C pass-through hole (below battery door)
        translate([wall + clearance + 3 + bat_door_w / 2 - 6,
                   outer_l - wall - 1, wall + bat_door_h + 5])
            cube([12, wall + 2, 7]);
    }

    // ── Breadboard supports (upper area) ──
    ledge_h = 3;
    ledge_w = 8;
    ledge_d = 8;

    for (dx = [wall + clearance, wall + clearance + bb_w - ledge_w])
        for (dy = [wall + clearance, wall + clearance + bb_l - ledge_d])
            translate([dx, dy, wall])
                cube([ledge_w, ledge_d, ledge_h]);

    // Side rails for breadboard
    rail_h = ledge_h + bb_h + 2;
    rail_t = 1.5;
    translate([wall + clearance - rail_t, wall + clearance + 10, wall])
        cube([rail_t, bb_l - 20, rail_h]);
    translate([wall + clearance + bb_w, wall + clearance + 10, wall])
        cube([rail_t, bb_l - 20, rail_h]);

    // ── Divider wall between breadboard and battery area ──
    divider_y = wall + clearance + bb_l + clearance / 2;
    translate([wall + 10, divider_y, wall])
        cube([inner_w - 20, 1.5, inner_h * 0.6]);

    // ── Battery cradle (lower area) ──
    bat_x = wall + clearance + 5;
    bat_y = bat_area_y + 2;
    // Side lips to hold battery
    bat_lip_h = bat_h * 0.6;
    // Left lip
    translate([bat_x - 1.5, bat_y, wall])
        cube([1.5, bat_l - 4, bat_lip_h]);
    // Right lip
    translate([bat_x + bat_w, bat_y, wall])
        cube([1.5, bat_l - 4, bat_lip_h]);
    // Back stop
    translate([bat_x, bat_y + bat_l - 4, wall])
        cube([bat_w, 1.5, bat_lip_h]);
    // Front lip (with gap for cable)
    translate([bat_x, bat_y - 1.5, wall])
        cube([bat_w * 0.3, 1.5, bat_lip_h]);
    translate([bat_x + bat_w * 0.5, bat_y - 1.5, wall])
        cube([bat_w * 0.5, 1.5, bat_lip_h]);

    // ── Speaker mounting posts (lower area, right of battery) ──
    spk_cx = outer_w / 2;
    spk_cy = bat_area_y + bat_l / 2;
    spk_post_h = 3;
    for (dx = [-spk_screw_spacing_w/2, spk_screw_spacing_w/2])
        for (dy = [-spk_screw_spacing_l/2, spk_screw_spacing_l/2])
            translate([spk_cx + dx, spk_cy + dy, wall])
                difference() {
                    cylinder(d = 5, h = spk_post_h);
                    cylinder(d = spk_screw_d, h = spk_post_h + 1);
                }

    // ── Screw bosses (lid) ──
    for (x = [wall + 5, outer_w - wall - 5])
        for (y = [wall + 5, outer_l - wall - 5])
            translate([x, y, wall])
                difference() {
                    cylinder(d = boss_d, h = inner_h);
                    cylinder(d = screw_d, h = inner_h + 1);
                }
}

// ───── Lid ─────
module lid() {
    difference() {
        union() {
            rounded_box(outer_w, outer_l, lid_h, corner_r);
            translate([wall + 0.3, wall + 0.3, -lip])
                rounded_box(inner_w - 0.6, inner_l - 0.6, lip, max(corner_r - 1.5, 1));
        }

        // TFT + buttons combined window
        // One big opening for the whole TFT module front face
        // (screen area + button strip on top)
        translate([tft_win_x, tft_win_y, -lip - 1])
            cube([tft_front_w + 1, tft_front_l + 1, lid_h + lip + 2]);

        // Label area (engraved)
        translate([wall + 8, outer_l / 2 - 8, lid_h - 0.5])
            linear_extrude(1)
                text("WATER", size = 6, halign = "left");
        translate([wall + 8, outer_l / 2 + 2, lid_h - 0.5])
            linear_extrude(1)
                text("ROWER", size = 6, halign = "left");

        // Screw holes
        for (x = [wall + 5, outer_w - wall - 5])
            for (y = [wall + 5, outer_l - wall - 5])
                translate([x, y, -lip - 1])
                    cylinder(d = 3.2, h = lid_h + lip + 2);

        // Countersink
        for (x = [wall + 5, outer_w - wall - 5])
            for (y = [wall + 5, outer_l - wall - 5])
                translate([x, y, lid_h - 1.5])
                    cylinder(d = 5.5, h = 2);

        // Vent slots (left side)
        for (i = [0:4])
            translate([wall + 40 + i * 5, wall + 2, -lip - 1])
                cube([2, 6, lid_h + lip + 2]);
    }

    // TFT module support rim (holds the TFT PCB from below)
    rim_h = 2;
    rim_w = 1.5;
    // Left edge
    translate([tft_win_x - rim_w, tft_win_y - rim_w, -lip - rim_h])
        cube([rim_w, tft_front_l + rim_w * 2, rim_h]);
    // Right edge
    translate([tft_win_x + tft_front_w + 1, tft_win_y - rim_w, -lip - rim_h])
        cube([rim_w, tft_front_l + rim_w * 2, rim_h]);
    // Bottom edge
    translate([tft_win_x - rim_w, tft_win_y - rim_w, -lip - rim_h])
        cube([tft_front_w + rim_w * 2 + 1, rim_w, rim_h]);
    // Top edge
    translate([tft_win_x - rim_w, tft_win_y + tft_front_l + 1, -lip - rim_h])
        cube([tft_front_w + rim_w * 2 + 1, rim_w, rim_h]);
}

// ───── Battery Door (slide-in cap) ─────
module battery_door() {
    bat_door_w = bat_w + 4;
    bat_door_h = bat_h + 2;
    door_t = wall;           // door thickness (same as wall)
    groove_d = 1.2;          // rail width (slightly less than groove for fit)
    tab_h = 4;               // extra tab on top for grip

    union() {
        // Main door panel
        cube([bat_door_w, door_t, bat_door_h]);

        // Side rails (slide into grooves)
        // Left rail
        translate([-groove_d, 0.5, 0])
            cube([groove_d, door_t - 0.2, bat_door_h + tab_h]);
        // Right rail
        translate([bat_door_w, 0.5, 0])
            cube([groove_d, door_t - 0.2, bat_door_h + tab_h]);

        // Pull tab on top
        translate([-groove_d, 0, bat_door_h])
            cube([bat_door_w + groove_d * 2, door_t, tab_h]);

        // Finger grip notch (raised bump for pulling)
        translate([bat_door_w / 2 - 8, door_t, bat_door_h + tab_h / 2])
            cube([16, 2, tab_h / 2]);
    }
}

// ===== Render =====
// PART=0: all (preview), 1: bottom_case, 2: lid, 3: tube_socket, 4: battery_door
// CLI export: openscad -o lid.stl -D "PART=2" enclosure.scad
PART = 0;

if (PART == 0 || PART == 1) bottom_case();

if (PART == 0 || PART == 2)
    translate([PART == 0 ? 0 : 0, PART == 0 ? outer_l + 15 : 0, 0])
        lid();

if (PART == 0 || PART == 3)
    translate([PART == 0 ? outer_w + 30 : 0, PART == 0 ? outer_l / 2 : 0, 0])
        tube_socket();

if (PART == 0 || PART == 4)
    translate([PART == 0 ? 0 : 0, PART == 0 ? -30 : 0, 0])
        battery_door();

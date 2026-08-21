/* ====================================================================
   E-BOOK READER 3D ENCLOSURE (PRUSAMENT PC BLEND CARBON FIBER READY)
   Generated on: 2026-08-21
   Dimensions & Tolerances pre-compensated for PCCF Shrinkage (0.2%)
   ==================================================================== */

$fn = 64; // High resolution circle rendering

// --- 1. PARAMETERS (All dimensions in mm) ---

// Shrinkage multiplier (Default 0.2% scale factor = 1.002)
pccf_scale = 1.0 + (0.2 / 100.0);

// Display
display_w = 111 * pccf_scale;
display_h = 171 * pccf_scale;
display_thick = 1.2;
border_left = 6.5;
border_right = 6.5;
border_top = 2.3;
border_bottom = 1.6;
screw_margin = 8;
symmetric_margin_x = 16.7;
active_w = display_w - border_left - border_right;
active_h = display_h - border_top - border_bottom;
pcb_bottom_margin = 12; 

// Outer Shell
case_w = 131.4 * pccf_scale;        // Total width: 131.4 mm
case_h = 217.89999999999998 * pccf_scale;       // Total height: 217.89999999999998 mm
wall_t = 2.2;
corner_r = 6;

// PCB & Internals
pcb_w = 80 * pccf_scale;         // 80 mm
pcb_h = 60 * pccf_scale;        // 60 mm
pcb_thick = 1.5;
pcb_protrusion = 22.5; // 22.5 mm
pcb_display_gap = 6.6;       // 6.6 mm
max_sandwich_thick = 17.5; // 17.5 mm

// Battery Compartment
bat_w = 40;                      // 40 mm
bat_thick = 11.4;               // Thickness behind display
bat_combined_thick = 12.6; // 12.6 mm
bat_taper_len = 18;          // Length of smooth transitions

// Trackball (Center at X=0 relative to PCB)
tb_collar_dia = 6.6;      // 6.6 mm collar ring
tb_ball_hole = 6.8;          // 6.6 + 0.2 = 6.8 mm
tb_seat_depth = 1.8;          // Seat in front cover
tb_y = wall_t + pcb_bottom_margin + 13; // 13 mm from PCB bottom

// Buttons
btn_spacing = 50;            // 50 mm center-to-center
btn_y = wall_t + pcb_bottom_margin + 5; // 5 mm from PCB bottom
btn_cap_w = 30;              // 30 mm (Large Rectangular extends to display edge)
btn_cap_h = 15;             // 15 mm
btn_corner_r = 3.5; // 3.5 mm rounded corners
btn_clearance = 0.4;          // 0.4 mm around cap pocket
btn_lip = 1.5;                // 1.5 mm Retention flange lip
btn_stem_w = 3.5;            // 3.5 mm square switch stem
btn_stem_h = 3.5;           // 3.5 mm
btn_stem_depth = 4;        // 4.0 mm socket depth
btn_stem_wall = 1.2; // 1.2 mm protruding collar wall thickness

// USB-C Cutout (Bottom Right)
usb_metal_w = 9;            // 9 mm
usb_protrusion = 5;  // 5 mm
usb_right_offset = 4.7; // 4.7 mm from right edge of PCB
usb_clearance = 1.2;          // 1.2 mm clearance around USB plug

// Fasteners
screw_post_dia = 5.5;   // 5.5 mm
screw_hole_dia = 2.6;   // 2.6 mm (M2.5 screw or heat insert)

// --- 2. MODULES ---

// Helper: Rounded Box
module rounded_box(w, h, z, r) {
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=z);
        translate([w-r, r, 0]) cylinder(r=r, h=z);
        translate([w-r, h-r, 0]) cylinder(r=r, h=z);
        translate([r, h-r, 0]) cylinder(r=r, h=z);
    }
}

// Helper: Centered Rounded Box
module centered_rounded_box(w, h, z, r) {
    translate([-w/2, -h/2, 0])
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=z);
        translate([w-r, r, 0]) cylinder(r=r, h=z);
        translate([w-r, h-r, 0]) cylinder(r=r, h=z);
        translate([r, h-r, 0]) cylinder(r=r, h=z);
    }
}

// FRONT COVER MODULE
module front_cover() {
    difference() {
        union() {
            // Main outer front shell
            color([0.2, 0.2, 0.22])
            rounded_box(case_w, case_h, wall_t + 2.0, corner_r);
        }
        
                // Display Active Window Cutout (through the shell)
        translate([symmetric_margin_x, wall_t + pcb_bottom_margin + pcb_protrusion + border_bottom, -0.5])
            cube([active_w, active_h, wall_t + 3.0]);
        
        // Display Panel Glass Recess (from the inside face Z=4.0 down to accommodate display thickness)
        translate([symmetric_margin_x - border_left - 0.5, wall_t + pcb_bottom_margin + pcb_protrusion - 0.5, (wall_t + 2.0) - (display_thick + 0.5)])
            cube([display_w + 1.0, display_h + 1.0, display_thick + 1.0]);

        // Flex cable clearance on physical right (printed left)
        translate([symmetric_margin_x - border_left - 1.2, wall_t + pcb_bottom_margin + pcb_protrusion + display_h / 2 - 10.0, (wall_t + 2.0) - (display_thick + 0.5)])
            cube([2.0, 20.0, display_thick + 1.0]);

        // Trackball Ball Opening (Ø6.8 mm)
        translate([case_w / 2, tb_y, -0.5])
            cylinder(d=tb_ball_hole, h=wall_t + 3.0);

        // Ergonomic Trackball Thumb Dimple (Spherical/Conical Dish on outer face Z=0)
        translate([case_w / 2, tb_y, -0.2])
            cylinder(d1=18.0, d2=tb_ball_hole, h=1.6);

        // Trackball Collar Seat Counterbore (Ø6.6 mm, Depth 1.8 mm)
        translate([case_w / 2, tb_y, (wall_t + 2.0) - tb_seat_depth])
            cylinder(d=tb_collar_dia, h=tb_seat_depth + 1.0);

        // Left Button Cap Pocket (Through Hole)
        translate([case_w / 2 - btn_spacing / 2, btn_y, -0.5])
            centered_rounded_box(btn_cap_w + btn_clearance * 2, btn_cap_h + btn_clearance * 2, wall_t + 3.0, btn_corner_r);

        // Left Button Retention Flange Recess (Inside face counterbore pocket 1.5mm deep)
        translate([case_w / 2 - btn_spacing / 2, btn_y, (wall_t + 2.0) - 1.5])
            centered_rounded_box(btn_cap_w + btn_lip * 2 + 0.6, btn_cap_h + btn_lip * 2 + 0.6, 2.0, btn_corner_r + 0.5);

        // Right Button Cap Pocket (Through Hole)
        translate([case_w / 2 + btn_spacing / 2, btn_y, -0.5])
            centered_rounded_box(btn_cap_w + btn_clearance * 2, btn_cap_h + btn_clearance * 2, wall_t + 3.0, btn_corner_r);

        // Right Button Retention Flange Recess (Inside face counterbore pocket 1.5mm deep)
        translate([case_w / 2 + btn_spacing / 2, btn_y, (wall_t + 2.0) - 1.5])
            centered_rounded_box(btn_cap_w + btn_lip * 2 + 0.6, btn_cap_h + btn_lip * 2 + 0.6, 2.0, btn_corner_r + 0.5);

        // M2.5 Screw Holes (Drilled into the solid corners from the back plane down to Z=1)
        screw_post_positions = [
            [corner_r, corner_r],
            [case_w - corner_r, corner_r],
            [corner_r, case_h - corner_r],
            [case_w - corner_r, case_h - corner_r],
            [corner_r, case_h / 2],
            [case_w - corner_r, case_h / 2]
        ];
        
        for (pos = screw_post_positions) {
            translate([pos[0], pos[1], 1.0])
                cylinder(d=screw_hole_dia, h=wall_t + 3.0);
        }
    }
}

// BACK OUTER SHELL MODULE
module back_outer_shell() {
    // Calculated Ramps based on PCB and Battery
    pcb_top_y = wall_t + pcb_bottom_margin + pcb_h; 
    bat_top_y = pcb_top_y + 60; 

    union() {
        // Top Section
        translate([0, bat_top_y + bat_taper_len, 0])
            rounded_box(case_w, case_h - (bat_top_y + bat_taper_len), wall_t, corner_r);
        
        hull() {
            translate([0, bat_top_y + bat_taper_len, 0]) rounded_box(case_w, 1, wall_t, corner_r);
            translate([0, bat_top_y, 0]) rounded_box(case_w, 1, bat_combined_thick, corner_r);
        }

        // Middle Section
        translate([0, pcb_top_y + bat_taper_len, 0])
            rounded_box(case_w, bat_top_y - (pcb_top_y + bat_taper_len), bat_combined_thick, corner_r);

        hull() {
            translate([0, pcb_top_y + bat_taper_len, 0]) rounded_box(case_w, 1, bat_combined_thick, corner_r);
            translate([0, pcb_top_y, 0]) rounded_box(case_w, 1, max_sandwich_thick, corner_r);
        }

        // Bottom Section
        translate([0, 0, 0])
            rounded_box(case_w, pcb_top_y, max_sandwich_thick, corner_r);
    }
}

// BACK INNER CAVITY MODULE
module back_inner_cavity() {
    pcb_top_y = wall_t + pcb_bottom_margin + pcb_h; 
    bat_top_y = pcb_top_y + 60; 
    
    union() {
        // Taper to Middle Section
        hull() {
            translate([0, bat_top_y + bat_taper_len, 0]) rounded_box(case_w - wall_t*2, 1, 0.01, corner_r - 1);
            translate([0, bat_top_y, 0]) rounded_box(case_w - wall_t*2, 1, bat_combined_thick - wall_t, corner_r - 1);
        }

        // Middle Section
        translate([0, pcb_top_y + bat_taper_len, 0])
            rounded_box(case_w - wall_t*2, bat_top_y - (pcb_top_y + bat_taper_len), bat_combined_thick - wall_t, corner_r - 1);

        // Taper to Bottom Section
        hull() {
            translate([0, pcb_top_y + bat_taper_len, 0]) rounded_box(case_w - wall_t*2, 1, bat_combined_thick - wall_t, corner_r - 1);
            translate([0, pcb_top_y, 0]) rounded_box(case_w - wall_t*2, 1, max_sandwich_thick - wall_t, corner_r - 1);
        }

        // Bottom Section
        translate([0, wall_t, 0])
            rounded_box(case_w - wall_t*2, pcb_top_y - wall_t, max_sandwich_thick - wall_t, corner_r - 1);
    }
}

// BACK COVER MODULE (2-Step Ergonomic Stepped Shell for Display, Battery & PCB)
module back_cover() {
    color([0.15, 0.15, 0.18])
    difference() {
        union() {
            difference() {
                back_outer_shell();
                
                // Inner Component Cavity (Starts from the flat parting line Z=0)
                translate([wall_t, 0, -0.01])
                    scale([1, 1, 1.01]) back_inner_cavity();
                    
                // USB-C Port Cutout (Enclosed Oval Hole shifted 4mm lower towards the floor)
                usb_x = case_w / 2 - pcb_w / 2 + usb_right_offset;
                translate([usb_x + 7.0, -1, max_sandwich_thick - 5.5])
                    rotate([-90, 0, 0])
                    hull() {
                        translate([-4, 0, 0]) cylinder(d=6.0, h=wall_t + 2.0);
                        translate([4, 0, 0]) cylinder(d=6.0, h=wall_t + 2.0);
                    }
            }
            
            // Screw Bosses extending from outer shell down to the mating surface Z=0
            screw_post_positions = [
                [corner_r, corner_r, max_sandwich_thick],
                [case_w - corner_r, corner_r, max_sandwich_thick],
                [corner_r, case_h / 2, bat_combined_thick],
                [case_w - corner_r, case_h / 2, bat_combined_thick],
                [corner_r, case_h - corner_r, wall_t],
                [case_w - corner_r, case_h - corner_r, wall_t]
            ];
            
            for (pos = screw_post_positions) {
                // Outer boss diameter 8.0mm to accommodate deep counterbore
                translate([pos[0], pos[1], 0])
                    cylinder(d=8.0, h=pos[2]);
            }
        }
        
        // Subtract screw holes and deep counterbores
        screw_post_positions = [
            [corner_r, corner_r, max_sandwich_thick],
            [case_w - corner_r, corner_r, max_sandwich_thick],
            [corner_r, case_h / 2, bat_combined_thick],
            [case_w - corner_r, case_h / 2, bat_combined_thick],
            [corner_r, case_h - corner_r, wall_t],
            [case_w - corner_r, case_h - corner_r, wall_t]
        ];
        
        for (pos = screw_post_positions) {
            // Clearance hole for M2.5 thread (bottom 2.5mm of the boss for strength)
            translate([pos[0], pos[1], -1])
                cylinder(d=screw_hole_dia + 0.5, h=4.0);
            
            // Deep counterbore for screw head and screwdriver (from Z=2.5 up to the top)
            // This allows the use of uniform 5mm or 6mm long screws everywhere!
            translate([pos[0], pos[1], 2.5])
                cylinder(d=5.5, h=pos[2] + 1);
        }
    }
}

// BUTTON CAP MODULE (Large Rounded Rectangular + Concave Indentation + Retention Flange + Square Stem Socket Collar)
module button_cap() {
    color([0.22, 0.24, 0.28]) // Matte PCCF Anthracite Carbon Fiber
    difference() {
        union() {
            // Main Button Shaft with Smooth Rounded Corners
            centered_rounded_box(btn_cap_w, btn_cap_h, 5, btn_corner_r);
            // Retention Flange Lip (Límec proti vypadnutí dopředu)
            centered_rounded_box(btn_cap_w + btn_lip * 2, btn_cap_h + btn_lip * 2, 1.2, btn_corner_r + 0.5);
            
            // Protruding Square Collar (Outer Box)
            translate([-(btn_stem_w + btn_stem_wall * 2)/2, -(btn_stem_h + btn_stem_wall * 2)/2, -btn_stem_depth])
                cube([btn_stem_w + btn_stem_wall * 2, btn_stem_h + btn_stem_wall * 2, btn_stem_depth]);
        }
        // Top Surface Ergonomic Concave Indentation (Prohlubeň pro palec)
        translate([0, 0, 5 + 12])
            sphere(r=15.0);

        // Inner Socket Hole (Cut out from the protruding collar)
        // Added 0.4mm tolerance (0.2mm per side) for the switch stem
        translate([-(btn_stem_w + 0.4)/2, -(btn_stem_h + 0.4)/2, -btn_stem_depth - 0.1])
            cube([btn_stem_w + 0.4, btn_stem_h + 0.4, btn_stem_depth + 0.2]);
    }
}

// --- 3. RENDER SELECTION ---
// Změňte tuto proměnnou pro export jednotlivých částí pro 3D tisk:
// 0 = Print Layout (Všechny díly rozložené vedle sebe na podložce)
// 1 = Pouze Přední kryt (Front Cover)
// 2 = Pouze Zadní kryt (Back Cover)
// 3 = Pouze Tlačítka (Buttons)
// 4 = Složený 3D pohled (Pro vizualizaci)
render_part = 0; 

if (render_part == 0) {
    // Print Layout
    front_cover();
    
    // Back cover laid flat next to front cover
    translate([case_w + 10, 0, 0])
        back_cover();

    // Buttons laid flat
    translate([-btn_cap_w/2 - 10, btn_y, 0])
        button_cap();
    translate([-btn_cap_w/2 - 10, btn_y + btn_cap_h + 10, 0])
        button_cap();
} else if (render_part == 1) {
    front_cover();
} else if (render_part == 2) {
    back_cover();
} else if (render_part == 3) {
    translate([btn_cap_w/2, btn_cap_h/2, 0]) button_cap();
    translate([btn_cap_w*2, btn_cap_h/2, 0]) button_cap();
} else if (render_part == 4) {
    front_cover();
    
    // Z=4.0 is the parting line (wall_t + 2.0)
    explode_offset = 0;
    translate([0, case_h, (wall_t + 2.0) + explode_offset])
        rotate([180, 0, 0])
        %back_cover(); // Transparent in assembly view
        
    translate([case_w / 2 - btn_spacing / 2, btn_y, wall_t])
        rotate([180, 0, 0])
        button_cap();
        
    translate([case_w / 2 + btn_spacing / 2, btn_y, wall_t])
        rotate([180, 0, 0])
        button_cap();
}

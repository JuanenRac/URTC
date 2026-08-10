// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] UNIVERSAL MULTITOOL PAROL6 (JUANENBOT) - V1.0 -
// PICK & PLACE TOOL - SUCTION CUP PART V1.0
// MODIFICATION: Height overlap corrected in T-slots.
//               Narrow passage (0 to 3.9mm) and nut channel (3.9mm upwards).
// =================================================================

$fn = 100; // Global circle resolution

// --- ANGULAR ADJUSTMENTS TO PREVENT COLLISIONS ---
fastener_angle     = 44; // Rotates the 4 Allen screws relative to their center
suction_cups_angle = 22; // Rotates the radial position of the 4 rails
camera_angle       = 35; // Rotates the upper camera block
sensor_angle       = 22; // Rotates the radial position of the TCRT5000 sensor pocket

// --- BASE DIMENSIONS ---
heat_inserts_dist_x_y   = 46.0; // Square mounting pattern (slave standard)
m3_clearance_diam       = 3.4;  // Clearance for M3 screw
m3_allen_head_diam      = 6.5;  // Counterbore for Allen head
head_counterbore_depth  = 21.0; // Depth of the well to hide screws
box_height              = 33.0; // Total cylinder height
outer_box_diam          = 75.0; // Total outer diameter

// --- CAMERA (Specific parameters for the camera) ---
camera_width            = 38.4;
camera_holes_dist       = 28.0; 
optical_tunnel_diam     = 27.0; // Free space for the lens
camera_pocket_depth     = 22.0; // Depth of the camera box

// --- SENSOR (TCRT5000 Pocket) ---
sensor_pocket_width     = 15.0; 
sensor_pocket_length    = 28.0;
sensor_pocket_depth     = 24.5; 
sensor_pcb_thickness    = 2.0;  // Slot thickness at top to insert PCB

// --- SUCTION CUPS (Adjustable T-slot rails) ---
suction_rail_width      = 4.2;  // Clearance width for M3 screw stud
suction_rail_length     = 2.0;  // Radial adjustment travel
rails_center_radius     = 23.0;

// --- DIMENSIONS FOR M3 SQUARE NUT (DIN 562) ---
square_nut_width        = 6.0 + 0.2; // 6mm + sliding tolerance
square_nut_height       = 2.5 + 0.2; // 2.5mm + sliding tolerance

// Generates the base solid body recovering the outer sensor walls
module cylindrical_body() {
    union() {
        // Main cylinder
        difference() {
            cylinder(h=box_height, r=outer_box_diam/2, center=false);
            // Base rounding / chamfer
            difference() {
                cylinder(h=2.1, r=outer_box_diam/2 + 1, center=false);
                cylinder(h=2.2, r1=outer_box_diam/2 - 2, r2=outer_box_diam/2, center=false);
            }
        }
        
        // RECOVERED GOOD PART: Added side protective bump so sensor is not exposed on sides
        rotate([0, 0, sensor_angle]) {
            translate([-(sensor_pocket_width + 6)/2, (outer_box_diam/2) - sensor_pocket_length, box_height - sensor_pocket_depth]) {
                cube([sensor_pocket_width + 6, sensor_pocket_length, sensor_pocket_depth]);
            }
        }
    }
}

// Creates the 4 Allen mounting points with their counterbored well
module screw_mounts() {
    rotate([0, 0, fastener_angle])
    for (x = [-1, 1], y = [-1, 1]) {
        translate([x * heat_inserts_dist_x_y / 2, y * heat_inserts_dist_x_y / 2, 0]) {
            // Clearance hole for screw
            translate([0, 0, -1]) cylinder(h=box_height + 2, r=m3_clearance_diam/2);
            // Counterbore so screw head does not protrude
            translate([0, 0, -0.1]) cylinder(h=head_counterbore_depth, r=m3_allen_head_diam/2);
        }
    }
}

// Creates the pocket for the camera and screw mounting points
module camera_mount_complete() {
    rotate([0, 0, camera_angle]) {
        // Pocket where camera rests
        translate([-camera_width/2, -camera_width/2, box_height - camera_pocket_depth + 0.01])
        cube([camera_width, camera_width, camera_pocket_depth + 1]);
        // Screw holes for camera
        for(x=[-1,1], y=[-1,1]) {
            translate([x*camera_holes_dist/2, y*camera_holes_dist/2, box_height - camera_pocket_depth - 10])
            cylinder(h=12, r=1.2);
        }
    }
}

// Creates the 4 T-slot rails optimized for square nuts
module adjustable_suction_cups() {
    rotate([0, 0, suction_cups_angle])
    for (f = [45, 135, 225, 315]) {
        rotate([0, 0, f])
        translate([rails_center_radius, 0, 0]) {
            
            // 1. NARROW SCREW PASSAGE (4.2mm slot)
            // Runs from Z = -0.1 to Z = 3.9 (right where nut track begins)
            translate([0, 0, -0.1])
            hull() {
                translate([-suction_rail_length/2, 0, 0]) cylinder(h=4.0, r=suction_rail_width/2);
                translate([suction_rail_length/2, 0, 0])  cylinder(h=4.0, r=suction_rail_width/2);
            }
            
            // 2. INTERNAL WIDE CHANNEL (Where square nut sits and slides)
            // Starts at Z = 3.9 and goes up 6mm breaking into central hollow interior
            translate([0, 0, 3.8])
            hull() {
                translate([-suction_rail_length/2, 0, 0]) cylinder(h=6, r=square_nut_width/2);
                translate([suction_rail_length/2, 0, 0])  cylinder(h=6, r=square_nut_width/2);
            }
        }
    }
}

// Internal sensor cavity
module sensor_pocket_complete() {
    rotate([0, 0, sensor_angle]) {
        
        // 1. INNER POCKET:
        translate([-sensor_pocket_width/2, (outer_box_diam/2) - sensor_pocket_length - 1, box_height - sensor_pocket_depth]) {
            cube([sensor_pocket_width, sensor_pocket_length + 2, sensor_pocket_depth - 2.5]);
        }
        
        // 2. TOP ENTRY SLOT:
        translate([-sensor_pocket_width/2, (outer_box_diam/2) - sensor_pcb_thickness, box_height - 3]) {
            cube([sensor_pocket_width, sensor_pcb_thickness + 2, 4.1]);
        }
        
        // 3. BOTTOM READING WINDOW (Towards LED ring face):
        translate([-sensor_pocket_width/2, (outer_box_diam/2) - sensor_pocket_length, 0]) {
            translate([2.0, sensor_pocket_length - 8, -1])
            cube([sensor_pocket_width - 4, 8, (box_height - sensor_pocket_depth) + 1.5]);
        }
    }
}

// Assembles all modules onto the cylindrical body
module final_box() {
    difference() {
        cylindrical_body(); // Base + side protective walls for sensor
        
        screw_mounts(); // Subtract Allen holes
        camera_mount_complete(); // Subtract camera pocket
        adjustable_suction_cups(); // Subtract T-slot suction cup rails
        sensor_pocket_complete(); // Subtract internal pocket, top slot, and bottom window
        
        // Optical tunnel for camera light passage
        translate([0, 0, -1]) cylinder(h=box_height + 2, r=optical_tunnel_diam/2);
        // LED Ring: circular cutout at bottom
        translate([0, 0, -0.1]) cylinder(h=4.0, r=32.8/2);
    }
}

// Final execution
final_box();
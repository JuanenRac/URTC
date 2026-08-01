// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] PAROL6 UNIVERSAL MULTITOOL (JUANENBOT) - V1.0 -
// PICK & PLACE TOOL - T12 SOLDERING IRON PART V1.0
// MODIFICATION: ANGLE CORRECTION FOR 30.5mm RADIUS (MAXIMUM INTEGRATION)
// =================================================================

$fn = 100;

// --- GENERAL ANGULAR ADJUSTMENTS ---
mounting_angle   = 44;
camera_angle     = 35;
t12_angle        = 135; // Quadrant where soldering iron is integrated

// --- RECALCULATED TILT TO PREVENT OPTICAL INTERFERENCE ---
// At only 30.5mm from center, a larger angle would collide with the lens.
t12_tilt_angle   = 10.5;

// --- BASE DIMENSIONS ---
insert_dist_x_y       = 46.0;
m3_clearance_diam     = 3.4;
m3_allen_head_diam    = 6.5;
head_recess_depth     = 21.0;
housing_height        = 33.0;
housing_outer_diam    = 75.0;
housing_outer_radius  = housing_outer_diam / 2; // 37.5mm

// --- CAMERA AND OPTICAL TUNNEL ---
camera_width         = 38.4;
camera_holes_dist    = 28.0;
optical_tunnel_diam  = 27.0;
camera_pocket_depth  = 22.0;

// --- T12 CONNECTOR DIMENSIONS ---
black_connector_diam   = 9.5;
black_connector_height = 24.0;
press_fit_clearance    = 0.2;
turret_outer_diam      = 18.0;
clamp_radius           = turret_outer_diam / 2;
// NEW ULTRA-COMPACT POSITION
t12_position_radius    = 30.5;
flex_slot_width        = 1.6;

module intersected_cylindrical_body() {
    union() {
        // 1. Main 75mm cylinder
        difference() {
            cylinder(h=housing_height, r=housing_outer_radius, center=false);
            difference() {
                cylinder(h=2.1, r=housing_outer_radius + 1, center=false);
                cylinder(h=2.2, r1=housing_outer_radius - 2, r2=housing_outer_radius, center=false);
            }
        }
        
        // 2. Integrated outer support (restores sufficient gripping material)
        rotate([0, 0, t12_angle]) {
            translate([t12_position_radius, 0, 0]) {
                rotate([0, t12_tilt_angle, 0]) {
                    translate([0, 0, -black_connector_height + 5])
                        cylinder(h=housing_height + black_connector_height - 5, r=turret_outer_diam/2, center=false);
                }
            }
        }
    }
}

module screw_mounts() {
    rotate([0, 0, mounting_angle])
    for (x = [-1, 1], y = [-1, 1]) {
        translate([x * insert_dist_x_y / 2, y * insert_dist_x_y / 2, 0]) {
            translate([0, 0, -1]) cylinder(h=housing_height + 2, r=m3_clearance_diam/2);
            translate([0, 0, -0.1]) cylinder(h=head_recess_depth, r=m3_allen_head_diam/2);
        }
    }
}

module complete_camera_mount() {
    rotate([0, 0, camera_angle]) {
        translate([-camera_width/2, -camera_width/2, housing_height - camera_pocket_depth + 0.01])
            cube([camera_width, camera_width, camera_pocket_depth + 1]);
        for(x=[-1,1], y=[-1,1]) {
            translate([x*camera_holes_dist/2, y*camera_holes_dist/2, housing_height - camera_pocket_depth - 10])
                cylinder(h=12, r=1.2);
        }
    }
}

module mixed_t12_turret_cutout() {
    rotate([0, 0, t12_angle]) {
        translate([t12_position_radius, 0, 0]) {
            rotate([0, t12_tilt_angle, 0]) {
                // Bakelite connector housing
                translate([0, 0, -black_connector_height - 1]) 
                    cylinder(h=black_connector_height + 2, d=black_connector_diam + press_fit_clearance);
                
                // Upper cable outlet
                translate([0, 0, -1])
                    cylinder(h=housing_height + 15, d=6.0);
                
                // Clamp flex slot
                translate([0, -flex_slot_width/2, -black_connector_height - 1])
                    cube([turret_outer_diam, flex_slot_width, black_connector_height + 2]);
                
                // M3 clamping screw
                translate([0, 0, -black_connector_height / 2]) {
                    rotate([90, 0, 0]) {
                        cylinder(h=turret_outer_diam + 4, d=3.3, center=true);
                        translate([0, 0, (turret_outer_diam/2) - 1.5])
                            cylinder(h=4, d=6.2, $fn=6, center=true);
                    }
                }
            }
        }
    }
}

module final_compact_soldering_head() {
    difference() {
        intersected_cylindrical_body(); 
        
        screw_mounts();        
        complete_camera_mount();  
        mixed_t12_turret_cutout(); 
        
        // Central optical tunnel remains 100% circular, flat and unobstructed
        translate([0, 0, -1]) cylinder(h=housing_height + 2, r=optical_tunnel_diam/2);
        
        // Lower free LED ring
        translate([0, 0, -0.1]) cylinder(h=4.0, r=32.8/2);
    }
}

// Render corrected part
final_compact_soldering_head();
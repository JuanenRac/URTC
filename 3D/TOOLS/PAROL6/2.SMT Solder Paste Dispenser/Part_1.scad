// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] PAROL6 UNIVERSAL MULTITOOL (JUANENBOT) - V1.0 -
// PICK & PLACE TOOL - SMART PASTE/SOLDER PASTE DISPENSER PART
// INTEGRATION: INTEGRATED NEMA 8 + CAN DRIVER (38.4x38.4mm PCB)
// =================================================================

$fn = 100; 

// --- GENERAL ANGULAR ADJUSTMENTS ---
mounting_angle        = 44;  
camera_angle          = 35;  
nema8_angle           = 135; // Dispenser quadrant

// --- CALCULATED TILT FOR DISPENSER NEEDLE ---
dispenser_tilt_angle  = 25.5; 

// --- MASTER INTERFACE BASE DIMENSIONS ---
insert_dist_x_y       = 46.0;   
m3_clearance_diam     = 3.4;     
m3_allen_head_diam    = 6.5; 
head_recess_depth     = 21.0; 
housing_height        = 33.0;       
housing_outer_diam    = 75.0;       
housing_outer_radius  = housing_outer_diam / 2; // 37.5mm

// --- CAMERA, LED RING AND NEW CAN DRIVER PCB ---
modular_pcb_width     = 38.4; // Master dimension for Camera and CAN Board
camera_holes_dist     = 28.0; 
optical_tunnel_diam   = 27.0; 
total_pocket_depth    = 22.0; // Shared space for electronics and vision

// --- 10CC SYRINGE AND NEMA 8 MOTOR DIMENSIONS ---
syringe_10cc_diam     = 18.5; // Standard body of solder paste syringe
turret_outer_diam     = 26.0; // Sturdier to withstand extrusion force
dispenser_pos_radius  = 34.5; // Same compact inward integration
flex_slot_width       = 1.6;
// Exact dimensions of NEMA 8 motor (8E11S0504SC5-100RS)
nema8_side            = 20.2; 
m2_screws_dist        = 16.0; 

module dispenser_head_body() {
    union() {
        // 1. Base cylinder of the 75mm ecosystem
        difference() {
            cylinder(h=housing_height, r=housing_outer_radius, center=false);
            difference() {
                cylinder(h=2.1, r=housing_outer_radius + 1, center=false);
                cylinder(h=2.2, r1=housing_outer_radius - 2, r2=housing_outer_radius, center=false);
            }
        }
        
        // 2. Sturdy turret for syringe and NEMA 8 mount
        rotate([0, 0, nema8_angle]) {
            translate([dispenser_pos_radius, 0, 0]) {
                rotate([0, dispenser_tilt_angle, 0]) {
                    // Lower cylindrical gripping body
                    translate([0, 0, -15])
                        cylinder(h=housing_height + 12.5, r=turret_outer_diam/2, center=false);
                    
                    // Upper square flange for mounting NEMA 8 (20x20mm)
                    translate([-nema8_side/2, -nema8_side/2, housing_height - 2.5])
                        cube([nema8_side, nema8_side, 8]);
                }
            }
        }
    }
}

module screw_mounts() {
    rotate([0, 0, mounting_angle])
    for (x = [-1, 1], y = [-1, 1]) {
        translate([x * insert_dist_x_y / 2, y * insert_dist_x_y / 2, 0]) {
            translate([0, 0, -1]) cylinder(h=housing_height + 10, r=m3_clearance_diam/2);
            translate([0, 0, -0.1]) cylinder(h=head_recess_depth, r=m3_allen_head_diam/2);
        }
    }
}

module electronics_and_vision_pocket() {
    rotate([0, 0, camera_angle]) {
        // Square cutout 38.4x38.4mm to stack camera and CAN Driver PCB
        translate([-modular_pcb_width/2, -modular_pcb_width/2, housing_height - total_pocket_depth + 0.01])
            cube([modular_pcb_width, modular_pcb_width, total_pocket_depth + 10]);
        
        for(x=[-1,1], y=[-1,1]) {
            translate([x*camera_holes_dist/2, y*camera_holes_dist/2, housing_height - total_pocket_depth - 5])
                cylinder(h=15, r=1.2); // Common mounting posts
        }
    }
}

module dispenser_mechanism_cutout() {
    rotate([0, 0, nema8_angle]) {
        translate([dispenser_pos_radius, 0, 0]) {
            rotate([0, dispenser_tilt_angle, 0]) {
                // Housing for 10cc solder paste syringe
                translate([0, 0, -16]) 
                    cylinder(h=30, d=syringe_10cc_diam);
                
                // Clear passage for 3.5mm lead screw
                translate([0, 0, -1])
                    cylinder(h=housing_height + 15, d=5.0); // Clearance to avoid friction
                
                // M2 mounting holes to fasten NEMA 8 to flange
                for(x=[-1,1], y=[-1,1]) {
                    translate([x*m2_screws_dist/2, y*m2_screws_dist/2, housing_height - 2])
                        cylinder(h=12, d=1.8); 
                }
                
                // Flex slot of lower clamp to tighten syringe
                translate([0, -flex_slot_width/2, -17])
                    cube([turret_outer_diam, flex_slot_width, 20]);
                
                // Transverse M3 screw to clamp syringe body
                translate([0, 0, -6]) {
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

module final_can_dispenser_head() {
    difference() {
        dispenser_head_body(); 
        
        screw_mounts();        
        electronics_and_vision_pocket();  
        dispenser_mechanism_cutout(); 
        
        // Keep central optical tunnel intact
        translate([0, 0, -1]) cylinder(h=housing_height + 2, r=optical_tunnel_diam/2);
        
        // Clear light channel for lower LED ring
        translate([0, 0, -0.1]) cylinder(h=4.0, r=32.8/2);
    }
}

// Render smart dispenser node
final_can_dispenser_head();
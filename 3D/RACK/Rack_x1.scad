// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] PAROL6 UNIVERSAL MULTITOOL (JUANENBOT) - V1.0 - PERFECT "U" SUPPORT
// MODIFICATION: NOMENCLATURE UPDATE TO V1.0
// =================================================================

$fn = 100;

// --- BASE BLOCK DIMENSIONS ---
block_width  = 95.0;       
block_length = 95.0;       
block_height = 20.0;        

// --- DIRECT CONCENTRIC CUTOUT DIMENSIONS (With tolerances) ---
slave_max_diam     = 75.0; // Upper and lower covers of the slave part
slave_min_diam     = 70.0; // Bottom of the central groove of the slave part
slave_groove_width = 10.0;

radial_tolerance   = 0.5;   
z_tolerance        = 0.4;        

inner_diam    = slave_min_diam + radial_tolerance; // Ø70.5mm 
outer_diam    = slave_max_diam + radial_tolerance; // Ø75.5mm 
groove_height = slave_groove_width - z_tolerance;  // 9.6mm 

// --- FUNNEL ENTRY CONFIGURATION ---
chamfer_width = 14.0;      
chamfer_depth = 12.0;

// --- 2020 PROFILE FASTENING (M5) ---
screws_2020_dist  = 20.0; 
m5_clearance_diam = 5.2;       
m5_head_diam      = 10.0;       
m5_head_depth     = 11.0;

// --- CONTROL PANEL X, Y, Z ---
recess_offset_x = 0.0;       
recess_pos_y    = -42.0;     
recess_offset_z = 0.0;

module u_support_base_v1_0() {
    difference() {
        // --- POSITIVE BODY ---
        union() {
            difference() {
                cube([block_width, block_length, block_height], center=true);
                cylinder(h=block_height + 2, r=outer_diam/2, center=true);
                translate([0, block_length/4, 0])
                cube([outer_diam, block_length/2 + 2, block_height + 2], center=true);
            }
            difference() {
                cube([block_width, block_length, groove_height], center=true);
                cylinder(h=groove_height + 2, r=inner_diam/2, center=true);
                translate([0, block_length/4, 0])
                cube([inner_diam, block_length/2 + 2, groove_height + 2], center=true);
            }
        }
        
        // --- FINAL SUBTRACTIONS ---
        // Generation of diagonal chamfers to smooth the entrance of the U-part
        translate([-inner_diam/2, block_length/2 - chamfer_depth, 0])
        rotate([0, 0, 45])
        cube([chamfer_width * 2, chamfer_width * 2, block_height + 4], center=true);
        
        translate([inner_diam/2, block_length/2 - chamfer_depth, 0])
        rotate([0, 0, -45])
        cube([chamfer_width * 2, chamfer_width * 2, block_height + 4], center=true);
        
        // Loop to position the two M5 mounting holes for the 2020 profile
        for (i = [-1, 1]) {
            pos_x = (i * screws_2020_dist / 2) + (i * recess_offset_x);
            
            // Clearance hole for the screw body
            translate([pos_x, 0, recess_offset_z])
            rotate([-90, 0, 0])
            cylinder(h=block_length + 10, r=m5_clearance_diam/2, center=true);
            
            // Deep recess for the M5 screw head
            translate([pos_x, recess_pos_y, recess_offset_z])
            rotate([-90, 0, 0])
            cylinder(h=m5_head_depth, r=m5_head_diam/2, center=false);
        }
    }
}

u_support_base_v1_0();
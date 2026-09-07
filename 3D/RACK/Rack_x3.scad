// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] PAROL6 UNIVERSAL MULTITOOL (JUANENBOT) - V1.0 - TRIPLE RACK (27.5cm)
// MODIFICATION: ADJUSTMENT OF TOTAL LENGTH TO 275mm AND PARAMETRIC UPDATE
// =================================================================

$fn = 100;

// --- BASE BLOCK DIMENSIONS (ADJUSTED FOR 275mm TOTAL) ---
block_width  = 275.0 / 3; // Now each section measures ~91.67mm to hit exactly 275mm total
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
        
        // --- CHAMFER SUBTRACTIONS ---
        translate([-inner_diam/2, block_length/2 - chamfer_depth, 0])
        rotate([0, 0, 45])
        cube([chamfer_width * 2, chamfer_width * 2, block_height + 4], center=true);
        translate([inner_diam/2, block_length/2 - chamfer_depth, 0])
        rotate([0, 0, -45])
        cube([chamfer_width * 2, chamfer_width * 2, block_height + 4], center=true);
        
        // Original rear M5 holes of each individual part (Facing towards Y)
        for (i = [-1, 1]) {
            pos_x = (i * screws_2020_dist / 2) + (i * recess_offset_x);
            translate([pos_x, 0, recess_offset_z])
            rotate([-90, 0, 0])
            cylinder(h=block_length + 10, r=m5_clearance_diam/2, center=true);
            translate([pos_x, recess_pos_y, recess_offset_z])
            rotate([-90, 0, 0]) 
            cylinder(h=m5_head_depth, r=m5_head_diam/2, center=false);
        }
    }
}

// --- ASSEMBLY RENDERING WITH SIDE HOLES ---
difference() {
    // 1. Join the 3 base parts to form the continuous 275mm block in X
    union() {
        for (x_offset = [-block_width, 0, block_width]) {
            translate([x_offset, 0, 0]) {
                u_support_base_v1_0();
            }
        }
    }
    
    // 2. Subtract the two holes at the absolute ends of X (Now at 137.5mm from center)
    half_total_width = (block_width * 3) / 2; // 275 / 2 = 137.5 mm
    
    // LEFT END (-X)
    translate([-half_total_width, 0, 0])
    rotate([0, 90, 0]) {
        cylinder(h=40, r=m5_clearance_diam/2, center=true);
        // Recess entering from left exterior towards center
        translate([0, 0, (m5_head_depth - 6.5)])
        cylinder(h=m5_head_depth + 0.2, r=m5_head_diam/2, center=false);
    }
    
    // RIGHT END (+X) - ROTATED 180 DEGREES RESPECT TO DRILLING AXIS
    translate([half_total_width, 0, 0])
    rotate([0, 90, 180]) { // 180° Z rotation applied
        cylinder(h=40, r=m5_clearance_diam/2, center=true);
        // Due to rotation, recess is now oriented towards block interior
        translate([0, 0, (m5_head_depth - 6.5)])
        cylinder(h=m5_head_depth + 0.2, r=m5_head_diam/2, center=false);
    }
}
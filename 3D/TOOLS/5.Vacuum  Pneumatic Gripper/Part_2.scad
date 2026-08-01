// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] UNIVERSAL MULTITOOL PAROL6 (JUANENBOT) - V1.0
// LOWER SLIDING ADAPTER - CORRECTED FOR MAIN PART V1.0
// MODIFICATION: Guide neck raised to 3.9mm to flush with square nut.
//               Geometry and gaps optimized for PH4-M5 fitting.
// =================================================================

$fn = 60; 

// --- BLOCK DIMENSIONS ---
block_width       = 12.0;       
block_length      = 16.0;       
block_body_height = 12.0; // Height of the main body hanging downwards

// --- COUPLING WITH THE T-SLOT OF SUCTION CUP PART V1.9 (Upwards) ---
part_slot_width   = 4.2;  // Width of lower slot
guide_neck_depth  = 3.9;  // CORRECTED: 3.9mm to align perfectly flush with the nut
guide_clearance   = 0.15; // Radial/sliding clearance for PETG 3D printing

// --- "L" PNEUMATIC CIRCUIT (Located at Y = 4) ---
m5_fitting_thread_diam  = 4.2; // Diameter for M5 thread tap (or direct threading)
m5_fitting_thread_depth = 6.0; // Thread depth for PH4-M5 fitting
vacuum_channel_diam     = 2.5; // Internal airflow channel
suction_cup_neck_diam   = 4.5; // Lower socket for suction cup thread/stud
suction_cup_neck_depth  = 4.0; // Depth of suction cup socket

// --- M3 FASTENING (Located at Y = -4) ---
m3_clearance_diam = 3.4;
m3_head_diam      = 6.0; // Well for M3 Allen head from below
m3_head_depth     = 4.0;

module lower_sliding_block_180z_corrected() {
    difference() {
        // --- 1. SOLID BODY (Base body + Calibrated upper guide neck) ---
        union() {
            // Lower block housing the fitting and suction cup
            translate([-block_width/2, -block_length/2, 0])
            cube([block_width, block_length, block_body_height]);

            // Guide neck rising into the V1.9 slot
            translate([-(part_slot_width - guide_clearance)/2, -block_length/2, block_body_height])
            cube([part_slot_width - guide_clearance, block_length, guide_neck_depth]);
        }
        
        // --- 2. M3 FIXING SCREW (At Y = -4) ---
        // Passes through entire block and guide neck to reach square nut above
        translate([0, -4, 0]) {
            translate([0, 0, -1])
            cylinder(h=block_body_height + guide_neck_depth + 2, r=m3_clearance_diam/2);

            // Bottom counterbore to hide M3 Allen head
            translate([0, 0, -0.1])
            cylinder(h=m3_head_depth + 0.1, r=m3_head_diam/2);
        }
        
        // --- 3. 90º PNEUMATIC CIRCUIT (At Y = 4) ---
        // Horizontal inlet from rear outer face for PH4-M5 fitting
        translate([0, block_length/2 + 0.1, block_body_height / 2]) rotate([90, 0, 0]) {
            cylinder(h=m5_fitting_thread_depth + 0.1, r=m5_fitting_thread_diam/2); // M5 Thread
            cylinder(h=m5_fitting_thread_depth + 3.0, r=vacuum_channel_diam/2);    // Central intersection
        }
        
        // Vertical outlet towards suction cup (Exits clean at Z = 0)
        translate([0, 4, 0]) {
            translate([0, 0, -1])
            cylinder(h=block_body_height + 2, r=vacuum_channel_diam/2);
            translate([0, 0, -0.1])
            cylinder(h=suction_cup_neck_depth + 0.1, r=suction_cup_neck_diam/2);
        }
    }
}

// Render of corrected part ready for T-slot
lower_sliding_block_180z_corrected();
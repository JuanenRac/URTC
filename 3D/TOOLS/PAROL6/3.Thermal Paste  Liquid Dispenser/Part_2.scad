// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] UNIVERSAL MULTITOOL PAROL6 (JUANENBOT) - V1.0
// OPTICAL DIFFUSER FOR LED RING - COMPLEMENTARY PART V1.0
// SYSTEM: Concentric lower press-fit.
// RECOMMENDATION: Print in Translucent / Opaline material.
// =================================================================

$fn = 120; // High resolution for a clean circular optical finish

// --- DIMENSIONS EXTRACTED FROM SUCTION CUP PART V1.9 ---
led_housing_diam     = 32.8; // Outer diameter of LED housing
optical_tunnel_diam  = 17.0; // Central clear passage for camera lens
led_housing_depth    = 4.0;  // Height of recess in main part

// --- DIFFUSER PARAMETERS (Optical fit) ---
fit_clearance          = 0.15; // Diametral tolerance for press fit in PETG
diffuser_wall_thickness = 0.4;  // Perimeter thickness for good light diffusion
diffuser_height         = 2.5;  // Slightly lower than recess to fit flush


module led_ring_diffuser() {
    // Calculate actual diameters applying printing tolerances
    actual_ext_diam = led_housing_diam - fit_clearance;
    actual_int_diam = optical_tunnel_diam + fit_clearance;
    
    difference() {
        // --- 1. TOTAL SOLID BODY (Base ring) ---
        cylinder(h=diffuser_height, r=actual_ext_diam/2, center=false);
        
        // --- 2. OPTICAL TUNNEL HOLLOWING (Camera passage) ---
        translate([0, 0, -1])
        cylinder(h=diffuser_height + 2, r=actual_int_diam/2, center=false);
        
        // --- 3. INNER CHANNEL FOR LEDS (Inverted U hollow) ---
        // Leaves a thin top layer (roof) and side walls to guide light
        translate([0, 0, -0.1])
        difference() {
            // Outer boundary of internal channel
            cylinder(h=diffuser_height - diffuser_wall_thickness + 0.1, r=(actual_ext_diam/2) - diffuser_wall_thickness, center=false);
            
            // Inner boundary of internal channel
            translate([0, 0, -0.5])
            cylinder(h=diffuser_height + 1, r=(actual_int_diam/2) + diffuser_wall_thickness, center=false);
        }
    }
}

// Diffuser render
led_ring_diffuser();
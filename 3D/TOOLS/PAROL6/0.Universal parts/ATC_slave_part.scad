// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] UNIVERSAL MULTITOOL PAROL6 (JUANENBOT) - V1.0 - (SLAVE-MALE Part)
// MODIFICATION: Strict isolation of pneumatic lines.
// SYSTEM: The manifold ring ONLY connects the M5 cross with the VACUUM line.
//          The BLOW line passes straight through and remains completely isolated.
// =================================================================

$fn = 100; // Define curve smoothing resolution

// --- BASE GEOMETRIC DIMENSIONS (OPTIMIZED 25.5mm SANDWICH) ---
max_diam = 75;                  // Total outer diameter (Ø75mm)
inner_diam = 70;                // Diameter of the central grip area (Ø70mm)
bottom_cover_thickness = 8.0;   // Thickness of the bottom cover (8mm)
groove_width = 10.5;            // Height of the slot where the fork enters
top_cover_thickness = 7.0;      // Thickness of the top disc (7mm)
total_height = bottom_cover_thickness + groove_width + top_cover_thickness; // Total sum: 25.5mm

// --- COUPLING CONFIGURATION (Z = total_height) ---
magnet_pattern_dist = 30.5; 
magnet_angle_1 = 45; magnet_angle_2 = 135; magnet_angle_3 = 225; magnet_angle_4 = 315;               
magnet_change_diam = 12.3; magnet_change_depth = 5.2; 

pin_pattern_dist = 22.5; 
pin_angle_1 = 60; pin_angle_2 = 180; pin_angle_3 = 300;                
pin_cone_height = 4.8; pin_cone_r_base = 3.4; pin_cone_r_tip = 1.9;

// --- [SLAVE POGO PIN PCB GEOMETRY 5+1] ---
slave_pogo_pcb_diam = 14.2; 
slave_pogo_pcb_depth = 1.5;  

// --- SEPARATED PNEUMATICS ---
vertical_channel_dist = 26.5;
pneumatic_vacuum_angle = 340;   // This channel will connect to the fitting cross
pneumatic_blow_angle = 20;     // This channel will remain ISOLATED
air_channel_diam = 4.0;     

// --- SEALING PARAMETERS: RUBBER RING SEATS (BOTTOM) ---
rubber_ring_diam = 7.5;
rubber_ring_depth = 1.5;       

// --- PARAMETERS: PC4-M5 FITTINGS IN CROSS PATTERN ---
fitting_cross_angle = 0;      
m5_thread_diam = 4.2;         
m5_thread_depth = 7.0;
outer_stud_diam = 8.0; 

// --- M3 HEAT-SET INSERTS ---
heat_inserts_dist_x_y = 46.0;   
m3_heat_insert_diam   = 4.2;
m3_heat_insert_depth  = 5.0;    


module complete_base_body() {
    union() {
        // 1. Bottom cover (Z=0 to Z=8)
        cylinder(h=bottom_cover_thickness, r=max_diam/2, center=false);

        // Outer studs for the cross fittings
        rotate([0, 0, fitting_cross_angle]) {
            for (a = [0, 90, 180, 270]) {
                rotate([0, 0, a])
                translate([max_diam/2 - 2, 0, bottom_cover_thickness/2])
                rotate([0, 90, 0])
                cylinder(h=3, r=outer_stud_diam/2, center=false);
            }
        }
        
        // 2. Central sandwich core (Z=8 to Z=18.5)
        translate([0, 0, bottom_cover_thickness])
        cylinder(h=groove_width, r=inner_diam/2, center=false);

        // 3. Top cover (Z=18.5 to Z=25.5)
        translate([0, 0, bottom_cover_thickness + groove_width])
        cylinder(h=top_cover_thickness, r=max_diam/2, center=false);
    }
}

module complete_slave_male_flange() {
    union() {
        difference() {
            complete_base_body();

            // Housings for neodymium magnets
            for (a = [magnet_angle_1, magnet_angle_2, magnet_angle_3, magnet_angle_4]) {
                rotate([0, 0, a]) translate([magnet_pattern_dist, 0, total_height - magnet_change_depth])
                cylinder(h=magnet_change_depth + 0.1, r=magnet_change_diam/2, center=false);
            }
            
            // LINE 1: VACUUM (Pass-through + Rubber ring seat)
            rotate([0, 0, pneumatic_vacuum_angle]) {
                translate([vertical_channel_dist, 0, -1])
                cylinder(h=total_height + 2, r=air_channel_diam/2, center=false);
                translate([vertical_channel_dist, 0, -0.1])
                cylinder(h=rubber_ring_depth + 0.1, r=rubber_ring_diam/2, center=false);
            }

            // LINE 2: BLOW (Completely isolated, no communication with the manifold ring)
            rotate([0, 0, pneumatic_blow_angle]) {
                translate([vertical_channel_dist, 0, -1])
                cylinder(h=total_height + 2, r=air_channel_diam/2, center=false);
                translate([vertical_channel_dist, 0, -0.1])
                cylinder(h=rubber_ring_depth + 0.1, r=rubber_ring_diam/2, center=false);
            }
            
            // INTERNAL MANIFOLD RECESS (Only connects with Vacuum)
            // To avoid crossovers, the circular ring is NOT a complete 360º loop,
            // or rather, since the blow line is at angle 20 and vacuum at 340, we make the ring
            // not complete the full circle or simply isolate the blow path.
            // To ensure total isolation without compromising the cross pattern, we create the ring recess
            // but EXCLUDE the area where the blow line is located using a difference:
            translate([0, 0, bottom_cover_thickness/2 - (air_channel_diam/2)]) {
                difference() {
                    // Base ring
                    cylinder(h=air_channel_diam, r=vertical_channel_dist + air_channel_diam/2, center=false);
                    // Inner core of the ring
                    translate([0, 0, -0.5])
                    cylinder(h=air_channel_diam + 1, r=vertical_channel_dist - air_channel_diam/2, center=false);
                    // ISOLATION CUT: Solid plug protecting the blow channel (Angle 20)
                    rotate([0, 0, pneumatic_blow_angle])
                    translate([vertical_channel_dist, 0, air_channel_diam/2])
                    cube([air_channel_diam + 4, air_channel_diam + 4, air_channel_diam + 2], center=true);
                }
            }
            
            // Horizontal cross drillings for PC4-M5 fittings (Connect to the manifold ring)
            rotate([0, 0, fitting_cross_angle]) {
                for (a = [0, 90, 180, 270]) {
                    rotate([0, 0, a]) {
                        // Internal air passage channel towards the circular manifold
                        translate([vertical_channel_dist - 1, 0, bottom_cover_thickness/2])
                        rotate([0, 90, 0])
                        cylinder(h=max_diam/2, r=air_channel_diam/2, center=false);
                        
                        // Outer entrance to house the M5 thread
                        translate([max_diam/2 + 5.1, 0, bottom_cover_thickness/2])
                        rotate([0, -90, 0])
                        cylinder(h=m5_thread_depth + 5, r=(m5_thread_diam/2), center=false);
                    }
                }
            }
            
            // PCB housing
            translate([0, 0, total_height - slave_pogo_pcb_depth])
            cylinder(h=slave_pogo_pcb_depth + 0.1, r=slave_pogo_pcb_diam/2, center=false);
            
            // Free central channel for internal wiring
            translate([0, 0, -1]) cylinder(h=total_height + 2, r=4.5, center=false);
            
            // Housings for the 4 M3 heat-set inserts
            for (x = [-1, 1], y = [-1, 1]) {
                translate([x * heat_inserts_dist_x_y / 2, y * heat_inserts_dist_x_y / 2, -0.1])
                cylinder(h=m3_heat_insert_depth + 0.1, r=m3_heat_insert_diam/2, center=false);
            }
        }
        
        // Tapered centering pins (Z=25.5)
        for (a = [pin_angle_1, pin_angle_2, pin_angle_3]) {
            rotate([0, 0, a]) translate([pin_pattern_dist, 0, total_height - 0.01])
            cylinder(h=pin_cone_height, r1=pin_cone_r_base, r2=pin_cone_r_tip, center=false);
        }
    }
}

// Complete flange rendering execution
complete_slave_male_flange();
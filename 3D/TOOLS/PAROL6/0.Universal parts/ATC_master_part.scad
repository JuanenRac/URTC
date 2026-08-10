// =================================================================
// Author: JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// Licensed under the CERN Open Hardware Licence v2 - Strongly Reciprocal
// (CERN-OHL-S v2). Full text at https://cern-ohl.web.cern.ch/.
// =================================================================
// [ATC] UNIVERSAL MULTITOOL PAROL6 (JUANENBOT) - V1.0 - (MASTER-FEMALE Part)
// MODIFICATION: Added chamfered lateral protruding cylindrical studs at PC4-M5 inlets
// SYSTEM: The studs are dynamically positioned according to the vacuum and blow angles.
// =================================================================

$fn = 100; // Global smoothing resolution for all geometries

// --- BASE GEOMETRIC DIMENSIONS ---
max_diam = 75;                // Total outer diameter of the flange
min_diam = 54;                // Diameter of the top coupling face
total_height = 20;            // Total height of the solid body
straight_base_height = 8.0;   // Height of the lower cylindrical section

internal_recess_diam = 60; 
internal_recess_depth = 0;
upper_recess_diam = 32.4; 
upper_recess_depth = 11.0; 

// =================================================================
// [ANGULAR AND RADIAL POSITIONING CONTROL]
// =================================================================

// --- TOP FACE (Ø55mm) ---
m4_screws_dist = 24;
m4_angle_1 = 45; m4_angle_2 = 135; m4_angle_3 = 225; m4_angle_4 = 315;               
m4_through_diam = 3.5;

// Panel M8 Connector: geometry for side entry
m8_connector_angle = 360;    
m8_connector_thread_diam = 8.2; m8_connector_z_height = 6.1; m8_connector_hex_width = 12.3;
m8_connector_hex_depth = 6.0;               
flange_neck_diam = 16.5; base_flange_diam = 32.4; base_flange_depth = 3.2;

// Horizontal Allen Accesses: to fix internal elements using set screws
allen_access_angle_1 = 90; allen_access_angle_2 = 270;   
allen_access_diam = 4.0;
set_screw_height = 7.5; 

// Ø3mm Through Hole (Sensor / Indexing): allows fixing position sensors
through_hole_dist = 19.0; through_hole_angle = 200;
through_hole_diam = 3.0; allen_head_diam = 6.0; allen_head_depth = 3.0;          

// Ø8mm Blind Recess: for housing electronic components or pins
blind_recess_dist = 19.0;
blind_recess_angle = 335;         
blind_recess_diam = 8.0; blind_recess_depth = 4.0;


// --- LOWER COUPLING FACE (Z=0) ---
// Magnets to secure the magnetic closure between master and slave flanges
magnet_pattern_dist = 30.5;
magnet_angle_1 = 45; magnet_angle_2 = 135; magnet_angle_3 = 225; magnet_angle_4 = 315;               
magnet_change_diam = 12.3; magnet_change_depth = 5.2;
magnet_air_gap = 0.2;               

// Tapered pins: guarantee coupling repeatability (kinematics)
pin_pattern_dist = 22.5; 
pin_angle_1 = 60; pin_angle_2 = 180; pin_angle_3 = 300;               
pin_cone_depth = 5.0; pin_cone_r_top = 3.5; pin_cone_r_bottom = 2.0;

// --- [MASTER POGO PIN GEOMETRY 5+1] ---
pogo_body_diam = 13.4;   
pogo_flange_diam = 14.2;   
pogo_body_depth = 3.7;   
flange_thickness = 1.0;

// Pneumatic Lines: vacuum and blow channels
vertical_channel_dist = 26.5;   
pneumatic_vacuum_angle = 340; pneumatic_blow_angle = 20;   
air_channel_diam = 4.0;
oring_diam = 6.0; oring_depth = 0.5;    

// --- OPTIMIZED PARAMETERS: PC4-M5 COUPLING ---
m5_fitting_thread_diam = 4.2;   // Pre-drill diameter for M5 threading
m5_fitting_total_depth = 6.5;   // Depth of the M5 thread housing
outer_stud_diam = 8.0;          // Diameter of the outer cylindrical stud
stud_length = 1.0;              // Extension beyond the base cylindrical body (Ø75mm)
m5_countersink_diam = 5.5;      // Outer diameter of the chamfered entry
countersink_depth = 1.0;        // Depth of the guide chamfer at the tip


module master_female_flange_v1_2() {
    difference() {
        // --- MAIN BODY WITH INTEGRATED STUDS ---
        union() {
            // Lower straight cylindrical base
            cylinder(h=straight_base_height, r=max_diam/2, center=false);

            // Upper conical body
            translate([0, 0, straight_base_height])
            cylinder(h=total_height - straight_base_height, r1=max_diam/2, r2=min_diam/2, center=false);

            // NEW: Outer studs added to the body at the pneumatic angles
            for (ang = [pneumatic_vacuum_angle, pneumatic_blow_angle]) {
                rotate([0, 0, ang])
                translate([max_diam/2 - 2, 0, straight_base_height/2])
                rotate([0, 90, 0])
                cylinder(h=stud_length + 2, r=outer_stud_diam/2, center=false);
            }
        }
        
        // Cutout of the upper recess for the electronic board
        z_upper_recess_floor = total_height - upper_recess_depth;
        translate([0, 0, z_upper_recess_floor])
        cylinder(h=upper_recess_depth + 0.1, r=upper_recess_diam/2, center=false);

        // Holes for M4 mounting hardware
        for (a = [m4_angle_1, m4_angle_2, m4_angle_3, m4_angle_4]) {
            rotate([0, 0, a]) translate([m4_screws_dist/2, 0, -1])
            cylinder(h=total_height + 2, r=m4_through_diam/2, center=false);
        }
        
        // Indexing sensor housing
        rotate([0, 0, through_hole_angle]) translate([through_hole_dist, 0, 0]) {
            translate([0, 0, -1]) cylinder(h=total_height + 2, r=through_hole_diam/2, center=false);
            translate([0, 0, -0.01]) cylinder(h=allen_head_depth + 0.01, r=allen_head_diam/2, center=false);
        }
        
        // Subtractions for component blind recesses
        rotate([0, 0, blind_recess_angle]) translate([blind_recess_dist, 0, total_height - blind_recess_depth])
        cylinder(h=blind_recess_depth + 0.1, r=blind_recess_diam/2, center=false);

        // Cutouts for the internal flange and connector neck
        translate([0, 0, total_height - internal_recess_depth]) cylinder(h=internal_recess_depth + 0.1, r=internal_recess_diam/2, center=false);
        translate([0, 0, total_height - internal_recess_depth]) cylinder(h=base_flange_depth + 0.1, r=base_flange_diam/2, center=false);
        translate([0, 0, m8_connector_z_height - 2]) cylinder(h=total_height, r=flange_neck_diam/2, center=false);

        // Side housing for the M8 connector and its hex nut recess
        translate([0, 0, m8_connector_z_height]) {
            rotate([0, 90, m8_connector_angle]) {
                cylinder(h=max_diam, r=m8_connector_thread_diam/2, center=false);
                hex_radius = (m8_connector_hex_width / 2) / cos(30);
                translate([0, 0, (max_diam/2) - m8_connector_hex_depth]) rotate([0, 0, 90]) 
                cylinder(h=m8_connector_hex_depth + 0.1, r=hex_radius, center=false, $fn=6);
            }
        }
        
        // Access holes for set screws (side Allen screws)
        for (a = [allen_access_angle_1, allen_access_angle_2]) {
            rotate([0, 0, a]) translate([0, 0, z_upper_recess_floor + set_screw_height]) rotate([0, 90, 0])
            cylinder(h=max_diam + 0.2, r=allen_access_diam/2, center=true);
        }
        
        // --- EXACT MASTER POGO PIN HOUSING (Z=0) ---
        translate([0, 0, -0.01]) {
            cylinder(h=flange_thickness + 0.01, r=pogo_flange_diam/2, center=false);
            translate([0, 0, flange_thickness])
            cylinder(h=pogo_body_depth, r=pogo_body_diam/2, center=false);
        }
        // Wire slots for the Pogo Pins
        for (sign = [-1, 1]) {
            translate([-2.2/2, (sign * (13.2/2)) - (sign == -1 ? 0 : 1.5), -0.01])
            cube([2.2, 1.5, flange_thickness + 1.0]);
        }
        translate([0, 0, -1]) cylinder(h=m8_connector_z_height + 0.2, r=4.5, center=false);

        // Positioning of magnets and centering cones on the coupling face
        total_magnet_housing_depth = magnet_change_depth + magnet_air_gap;
        for (a = [magnet_angle_1, magnet_angle_2, magnet_angle_3, magnet_angle_4]) {
            rotate([0, 0, a]) translate([magnet_pattern_dist, 0, -0.01])
            cylinder(h=total_magnet_housing_depth + 0.01, r=magnet_change_diam/2, center=false);
        }
        for (a = [pin_angle_1, pin_angle_2, pin_angle_3]) {
            rotate([0, 0, a]) translate([pin_pattern_dist, 0, -0.01])
            cylinder(h=pin_cone_depth + 0.01, r1=pin_cone_r_top, r2=pin_cone_r_bottom, center=false);
        }

        // --- REFORMED PNEUMATIC CIRCUITS WITH STUD AND PREMIUM FINISH ---
        outer_opening_coord = max_diam/2 + stud_length; // The outer end point of the stud
        
        for (ang = [pneumatic_vacuum_angle, pneumatic_blow_angle]) {
            rotate([0, 0, ang]) {
                // 1. Internal air passage channel connecting to the vertical section
                translate([vertical_channel_dist - 0.5, 0, straight_base_height/2]) rotate([0, 90, 0]) 
                cylinder(h=(max_diam/2 - vertical_channel_dist) + 1, r=air_channel_diam/2, center=false);

                // 2. Cylindrical housing for tapping M5 fitting (Hollowed from the stud tip)
                translate([outer_opening_coord + 0.1, 0, straight_base_height/2]) rotate([0, -90, 0]) 
                cylinder(h=m5_fitting_total_depth + 0.1, r=m5_fitting_thread_diam/2, center=false);

                // 3. 45° guide entry chamfer (Countersunk at the outer tip of the stud)
                translate([outer_opening_coord + 0.1, 0, straight_base_height/2]) rotate([0, -90, 0])
                cylinder(h=countersink_depth, r1=m5_countersink_diam/2, r2=m5_fitting_thread_diam/2, center=false);

                // 4. Vertical pass-through duct to the O-rings
                translate([vertical_channel_dist, 0, -0.1]) cylinder(h=straight_base_height/2 + 0.2, r=air_channel_diam/2, center=false);

                // 5. Lower O-ring seat (Z=0)
                translate([vertical_channel_dist, 0, -0.01]) cylinder(h=oring_depth + 0.01, r=oring_diam/2, center=false);
            }
        }
    }
}

// Final rendering
master_female_flange_v1_2();
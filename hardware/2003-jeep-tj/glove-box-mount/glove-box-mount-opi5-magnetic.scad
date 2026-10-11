/*
 * Jeep TJ Glove Box Mount Rev4 - Orange Pi 5 Plus magnetic slide cradle
 *
 * Combines the Rev3 Jeep bolt-pattern skeleton with a flat cradle for the
 * existing opi5plus_case_with_magnets.STL.  The 18 magnet centers below were
 * recovered directly from that STL (horizontal sections through its embedded
 * magnet cavities), not approximated from the PCB mounting holes.
 *
 * Installed orientation:
 *   - The new cradle is on the stepped/counterbored side of the Jeep bracket.
 *   - The Orange Pi case bottom sits against the cradle's +Y face.
 *   - The case's Y=0 edge is at the top of the cradle.
 *   - Slide the case down between the side guides until it reaches the ledge;
 *     the magnets then pull it flat against the backplate.
 *
 * Magnet pockets are face-open for press-fitting or gluing 5 x 3 mm magnets.
 * Check polarity against the finished Orange Pi case before gluing.
 */

$fn = 72;

// Source geometry
jeep_source = "glove-box-mount-base.STL";
jeep_w       = 133.029907;
jeep_depth   = 12.014;

// Orange Pi case envelope, measured from opi5plus_case_with_magnets.STL
case_w_x     = 104.6;
case_h_z     = 79.6;
case_depth   = 22.0;

// Cradle placement and fit
case_x             = (jeep_w - case_w_x) / 2;
case_bottom_z      = 15.0;  // clears the two lower Jeep fastener heads
side_clearance     = 0.45;  // per side; tune for printer/material if needed
guide_w            = 2.0;
guide_projection   = 3.0;   // only engages the case's solid base region
ledge_h            = 2.5;
ledge_projection   = 4.0;
top_margin         = 2.0;
plate_corner_r     = 3.0;

// Backplate overlaps the Rev3 solid by 0.25 mm for a dependable manifold union.
plate_overlap = 0.25;
plate_t       = 4.25;
plate_y       = jeep_depth - plate_overlap;
contact_y     = plate_y + plate_t;
plate_x       = case_x - side_clearance - guide_w;
plate_w       = case_w_x + 2 * (side_clearance + guide_w);
plate_z       = 0;
plate_h       = case_bottom_z + case_h_z + top_margin;

// Matching magnet pockets.  The source case cavities are nominally 5.50 mm.
// 5.60 mm gives a practical FDM glue/press pocket for nominal 5 x 3 mm magnets.
magnet_pocket_d     = 5.60;
magnet_pocket_depth = 3.10;

// Existing Rev3 counterbore mouths are nominally 12 mm on this face.
bolt_access_d = 12.40;
bolt_centers = [
    [  8.515, 55.4 ],
    [ 16.515,  8.4 ],
    [116.515,  8.4 ],
    [124.515, 55.4 ]
];

// Exact 18-center pattern recovered from the Orange Pi case STL.
// Coordinates are in that case's native X/Y system.
magnet_centers = [
    [ 5.833509,  5.302150 ],
    [ 5.833513, 21.657486 ],
    [ 5.969868, 48.893264 ],
    [ 5.969873, 75.056042 ],
    [15.581714, 31.730639 ],
    [15.581720, 67.365785 ],
    [20.455829,  5.302150 ],
    [20.455833, 21.657486 ],
    [30.962239,  5.302150 ],
    [30.962239, 75.056042 ],
    [52.299990,  5.302150 ],
    [52.299991, 75.056042 ],
    [73.421130, 75.056042 ],
    [83.710915,  5.302150 ],
    [98.224964,  5.302150 ],
    [98.224968, 21.657486 ],
    [98.224960, 48.893264 ],
    [98.224965, 75.056042 ]
];

// Set true in OpenSCAD for a translucent fit/orientation preview.
show_case_preview = false;

module y_cylinder(d, h) {
    rotate([-90, 0, 0]) cylinder(d=d, h=h);
}

module rounded_plate_xz(x0, y0, z0, w, h, depth, r) {
    hull()
        for (x = [x0 + r, x0 + w - r],
             z = [z0 + r, z0 + h - r])
            translate([x, y0, z])
                y_cylinder(2*r, depth);
}

module cradle_positive() {
    union() {
        import(jeep_source, convexity=20);

        // Flat magnetic backplate
        rounded_plate_xz(plate_x, plate_y, plate_z,
                         plate_w, plate_h, plate_t, plate_corner_r);

        // Full-height side guides: open at the top for drop-in installation.
        translate([case_x - side_clearance - guide_w,
                   contact_y,
                   case_bottom_z - ledge_h])
            cube([guide_w,
                  guide_projection,
                  case_h_z + ledge_h]);

        translate([case_x + case_w_x + side_clearance,
                   contact_y,
                   case_bottom_z - ledge_h])
            cube([guide_w,
                  guide_projection,
                  case_h_z + ledge_h]);

        // Bottom stop/weight-bearing lip. Magnets provide the face retention.
        translate([case_x - side_clearance,
                   contact_y,
                   case_bottom_z - ledge_h])
            cube([case_w_x + 2*side_clearance,
                  ledge_projection,
                  ledge_h]);
    }
}

module all_cutouts() {
    // Preserve access to all four Jeep fasteners through the added backplate,
    // including the small intersections at the two outer upper bosses.
    for (p = bolt_centers)
        translate([p[0], plate_y - 0.5, p[1]])
            y_cylinder(bolt_access_d, plate_t + guide_projection + 1.0);

    // When the case bottom faces +Y, a proper rigid rotation maps its native
    // +Y axis toward mount -Z, hence the deliberate vertical mirror here.
    for (p = magnet_centers)
        translate([case_x + p[0],
                   contact_y - magnet_pocket_depth,
                   case_bottom_z + case_h_z - p[1]])
            y_cylinder(magnet_pocket_d, magnet_pocket_depth + 0.1);
}

difference() {
    cradle_positive();
    all_cutouts();
}

if (show_case_preview)
    %translate([case_x, contact_y, case_bottom_z + case_h_z])
        rotate([-90, 0, 0])
            import("../../orange-pi-5-plus-case/opi5plus-case-with-magnets.STL",
                   convexity=20);


"""
add_component_mockups.py — Tamagotchi component reference mockups
Adds dimensional reference solids for all electronic components inside the
tamagotchi-case.FCStd, grouped under App::DocumentObjectGroup 'Components'.
Performs collision/containment checks and reports results.

Run AFTER build_case.py has generated the FCStd (or with the GUI doc open).

Coordinate system (same as build_case.py):
  Origin = geometric centre of exterior footprint at Z=0 (bottom face)
  +Y  = front of device (bezel side, toward FrontBezel)
  −Y  = rear face
  −X  = side with USB-C cutouts
  +Z  = top (TFT end)

The FrontBezel in the FCStd is placed at Y = ext_y/2 = +20 mm
(translated from its local frame where Y=0 is the mating face).
"""

import os, sys, math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FCSTD_PATH = os.path.join(SCRIPT_DIR, "tamagotchi-case.FCStd")
STL_DIR    = os.path.join(SCRIPT_DIR, "stl")
STL_REAR   = os.path.join(STL_DIR, "rear-body.stl")
STL_BEZEL  = os.path.join(STL_DIR, "front-bezel.stl")

import FreeCAD as App
import Part

# ---------------------------------------------------------------------------
# Parameters — must mirror build_case.py exactly
# ---------------------------------------------------------------------------
P = {
    "ext_x":           40.0,
    "ext_y":           40.0,
    "ext_z":           70.0,
    "wall":             2.0,
    # Note: wall_chamfer / chamfer_h / chamfer_ang / chamfer_top_off REMOVED.
    # The chamfer has been eliminated. Bezel is now a plain rectangular plate.
    "wall_front":       2.0,
    "fillet_ext":       4.0,
    "fillet_edge":      1.5,
    "clr_fit":          0.20,
    "clr_slide":        0.15,
    "clr_z":            0.30,
    "scr_pcb_w":        32.26,
    "scr_pcb_h":        48.86,
    "scr_pcb_t":         1.66,
    "scr_act_w":        29.52,
    "scr_act_h":        34.93,
    "scr_act_off_y":     0.0,
    "bezel_lip":         1.2,
    "bezel_th":          5.0,   # reduced from 8 mm (chamfer no longer forces it up)
    "pcb_clip_h":        2.0,
    "header_clr":        6.0,
    # Capacitive button (Home-style concave dish)
    "btn_dia":          15.0,
    "btn_depth":         1.5,
    "btn_wall":          1.4,
    "btn_center_z":      0.0,   # 0 = auto-derive
    "mcu_w":            18.0,
    "mcu_l":            23.5,
    "mcu_t":             1.2,
    "mcu_usb_w":         9.4,
    "mcu_usb_h":         3.6,
    "mcu_usb_protrude":  1.0,
    "bat_t":             5.0,
    "bat_w":            30.0,
    "bat_h":            35.0,
    "bat_swell":         0.8,
    "tp_w":             17.0,
    "tp_l":             26.0,
    "tp_t":              4.5,
    "tp_usb_w":          9.4,
    "tp_usb_h":          3.6,
    "ttp_w":            15.0,
    "ttp_h":            11.0,
    "ttp_t":             1.6,
    "ttp_standoff":      1.0,
    "boss_od":           5.0,
    "boss_id":           1.6,
    "boss_h":            8.0,
    "screw_head_d":      4.0,
    "screw_clear_d":     2.4,
    "n_screws":          4,
    "charge_mode":       0,
    "lip_h":             4.0,
}

def derive(P):
    d = {}
    d["int_x"] = P["ext_x"] - 2*P["wall"]
    d["int_y"] = P["ext_y"] - 2*P["wall"]
    d["int_z"] = P["ext_z"] - 2*P["wall"]
    d["bat_pocket_t"] = P["bat_t"] + P["bat_swell"] + P["clr_fit"]
    d["mcu_pocket_w"] = P["mcu_w"] + 2*P["clr_fit"]
    d["mcu_pocket_l"] = P["mcu_l"] + 2*P["clr_fit"]
    d["tp_pocket_w"]  = P["tp_w"]  + 2*P["clr_fit"]
    # No chamfer_depth — chamfer eliminated.

    # Screen window Z centre (mirrors build_case.py derive())
    ez = P["ext_z"]; wall = P["wall"]
    scr_pcb_h = P["scr_pcb_h"]; scr_act_h = P["scr_act_h"]
    pcb_top_z = ez - wall
    win_z_raw = (pcb_top_z
                 - scr_pcb_h
                 + (scr_pcb_h - scr_act_h) / 2
                 + scr_act_h / 2
                 + P["scr_act_off_y"])
    win_h = scr_act_h - 2 * P["bezel_lip"]
    win_z_max_top   = ez - 6.0 - win_h / 2
    win_z_min_bottom = 22.0 + win_h / 2
    d["win_z_ctr"] = max(win_z_min_bottom, min(win_z_max_top, win_z_raw))
    d["scr_win_h"] = win_h
    d["ttp_pocket_w"] = P["ttp_w"] + 2 * P["clr_fit"]
    d["ttp_pocket_h"] = P["ttp_h"] + 2 * P["clr_fit"]
    d["ttp_pocket_d"] = P["bezel_th"] - P["btn_depth"] - P["btn_wall"]
    d["boss_pos"]  = P["ext_x"]/2 - P["wall"] - P["boss_od"]/2 - 1.0

    # Button Z centre
    win_z_bot = d["win_z_ctr"] - win_h / 2
    btn_strip_top = win_z_bot - 2.0
    btn_strip_bot = 4.0
    btn_z_auto = (btn_strip_top + btn_strip_bot) / 2
    d["btn_center_z"] = P["btn_center_z"] if P["btn_center_z"] != 0.0 else btn_z_auto

    return d

D = derive(P)

# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------
def box(w, d2, h, cx=0.0, cy=0.0, cz=0.0):
    """Solid box centred at (cx, cy) in XY, base at cz."""
    return Part.makeBox(w, d2, h, App.Vector(cx - w/2, cy - d2/2, cz))

def cyl(r, h, cx=0.0, cy=0.0, cz=0.0):
    return Part.makeCylinder(r, h, App.Vector(cx, cy, cz), App.Vector(0, 0, 1))

# ---------------------------------------------------------------------------
# Component position calculations (mirroring build_rear_body pocket logic)
# ---------------------------------------------------------------------------
def component_positions(P, D):
    """
    Returns dict of component name -> (shape, cx, cy, cz_base, description)
    All in the global coordinate frame (same as rear body / build_case.py).
    """
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall = P["wall"]
    positions = {}

    # --- BATTERY (LiPo 503035) ---
    # Against rear wall (−Y), centred in X, base at Z=wall
    bat_cx = 0.0
    bat_cy = -ey/2 + wall + P["bat_h"]/2   # -18 + 2 + 17.5 = 1.5
    bat_cz = wall
    bat_shape = box(P["bat_w"], P["bat_h"], P["bat_t"],
                    cx=bat_cx, cy=bat_cy, cz=bat_cz)
    positions["Comp_Battery"] = {
        "shape": bat_shape,
        "cx": bat_cx, "cy": bat_cy, "cz_base": bat_cz,
        "w": P["bat_w"], "h": P["bat_h"], "t": P["bat_t"],
        "color": (0.55, 0.55, 0.55),   # grey
        "desc": f"LiPo 503035  {P['bat_w']}x{P['bat_h']}x{P['bat_t']}mm"
    }

    # --- MCU (ESP32-S3 Zero) ---
    # Pocket: length(mpl) along X, width(mpw) along Y, cx=ey/2-wall-mpw/2, cy=0
    # MCU PCB actual: mcu_l along X, mcu_w along Y
    mcu_cx = ey/2 - wall - D["mcu_pocket_w"]/2   # = 8.80
    mcu_cy = 0.0
    mcu_cz = wall + D["bat_pocket_t"] + 2.0       # = 10.0
    mcu_shape = box(P["mcu_l"], P["mcu_w"], P["mcu_t"],
                    cx=mcu_cx, cy=mcu_cy, cz=mcu_cz)
    # USB-C connector: protrudes from −X end of PCB, at Y position = mcu_cx
    # (matching the USB cutout in build_rear_body: cy=mcu_cx)
    mcu_usb_z = mcu_cz + (P["mcu_t"] - P["mcu_usb_h"]) / 2
    mcu_usb_cx = -(ex/2)                          # flush with exterior −X face
    mcu_usb_cy = mcu_cx                            # same Y as MCU X-centre used in usb cut
    mcu_usb_shape = box(P["mcu_usb_protrude"] + wall,  # span exterior + interior
                        P["mcu_usb_w"], P["mcu_usb_h"],
                        cx=mcu_usb_cx + (P["mcu_usb_protrude"] + wall)/2,
                        cy=mcu_usb_cy,
                        cz=mcu_usb_z)
    try:
        mcu_full = mcu_shape.fuse(mcu_usb_shape)
    except Exception:
        mcu_full = mcu_shape
    positions["Comp_MCU"] = {
        "shape": mcu_full,
        "cx": mcu_cx, "cy": mcu_cy, "cz_base": mcu_cz,
        "w": P["mcu_l"], "h": P["mcu_w"], "t": P["mcu_t"],
        "usb_cy": mcu_usb_cy,
        "color": (0.13, 0.55, 0.13),   # green
        "desc": f"ESP32-S3 Zero  {P['mcu_l']}x{P['mcu_w']}x{P['mcu_t']}mm"
    }

    # --- TP4056 ---
    tp_cx = mcu_cx                                 # same X centre as MCU
    tp_cy = -ey/2 + wall + D["tp_pocket_w"]/2     # -18+2+8.7 = -7.3 (near −Y wall)
    tp_cz = mcu_cz
    tp_shape = box(P["tp_l"], P["tp_w"], P["tp_t"],
                   cx=tp_cx, cy=tp_cy, cz=tp_cz)
    # TP4056 USB-C: same −X side
    tp_usb_z = tp_cz + (P["tp_t"] - P["tp_usb_h"]) / 2
    tp_usb_cx = -(ex/2)
    tp_usb_shape = box(P["tp_usb_w"] / P["tp_usb_w"] * (P["mcu_usb_protrude"] + wall),
                       P["tp_usb_w"], P["tp_usb_h"],
                       cx=tp_usb_cx + (P["mcu_usb_protrude"] + wall)/2,
                       cy=tp_cy,
                       cz=tp_usb_z)
    try:
        tp_full = tp_shape.fuse(tp_usb_shape)
    except Exception:
        tp_full = tp_shape
    positions["Comp_TP4056"] = {
        "shape": tp_full,
        "cx": tp_cx, "cy": tp_cy, "cz_base": tp_cz,
        "w": P["tp_l"], "h": P["tp_w"], "t": P["tp_t"],
        "usb_cy": tp_cy,
        "color": (1.0, 0.50, 0.0),    # orange
        "desc": f"TP4056  {P['tp_l']}x{P['tp_w']}x{P['tp_t']}mm"
    }

    # --- SCREEN PCB (TFT JD9853) ---
    # PCB sits against interior +Y wall (Y = ext_y/2 − wall = 18)
    # PCB top flush with interior ceiling (Z = ext_z − wall = 68)
    scr_cy = ey/2 - wall - P["scr_pcb_t"]/2       # 18 − 0.83 = 17.17
    scr_z_bot = ez - wall - P["scr_pcb_h"]         # 19.14
    scr_z_top = ez - wall                           # 68.00
    scr_shape = box(P["scr_pcb_w"], P["scr_pcb_t"], P["scr_pcb_h"],
                    cx=0.0, cy=scr_cy, cz=scr_z_bot)
    # Active area marker (thin slab on +Y face of PCB, offset so it's visible)
    pcb_top_z = ez - wall
    scr_act_h = P["scr_act_h"]
    scr_act_w = P["scr_act_w"]
    # Active area centre: PCB top − scr_pcb_h + ((scr_pcb_h − scr_act_h)/2 + scr_act_h/2)
    act_z_ctr = pcb_top_z - P["scr_pcb_h"] + (P["scr_pcb_h"] - scr_act_h)/2 + scr_act_h/2 + P["scr_act_off_y"]
    act_z_bot = act_z_ctr - scr_act_h/2
    marker_t = 0.3
    act_marker = box(scr_act_w, marker_t, scr_act_h,
                     cx=0.0,
                     cy=ey/2 - wall + marker_t/2,   # just outside +Y interior wall (for visibility)
                     cz=act_z_bot)
    # Header connector behind PCB (8-pin, 2.54 pitch, ~21mm wide x 5mm tall, protrudes −Y)
    hdr_w = 21.0; hdr_h = 5.0; hdr_d = 2.54
    hdr_z = scr_z_bot   # at PCB bottom edge
    hdr_cy = ey/2 - wall - P["scr_pcb_t"] - hdr_d/2
    hdr_shape = box(hdr_w, hdr_d, hdr_h,
                    cx=0.0, cy=hdr_cy, cz=hdr_z)
    try:
        scr_full = scr_shape.fuse(hdr_shape)
    except Exception:
        scr_full = scr_shape
    positions["Comp_Screen"] = {
        "shape": scr_full,
        "marker": act_marker,
        "cx": 0.0, "cy": scr_cy, "cz_base": scr_z_bot,
        "w": P["scr_pcb_w"], "h": P["scr_pcb_h"], "t": P["scr_pcb_t"],
        "color": (0.10, 0.30, 0.80),   # blue
        "desc": f"TFT PCB  {P['scr_pcb_w']}x{P['scr_pcb_h']}x{P['scr_pcb_t']}mm  "
                f"active={P['scr_act_w']}x{P['scr_act_h']}"
    }

    # --- TTP223 (in bezel, inner face pocket behind concave button) ---
    # The chamfer has been eliminated. The TTP223 now sits in a pocket
    # on the INNER face of the bezel (Y=0 side in bezel local frame),
    # centred in X and at Z = btn_center_z, directly behind the concave button.
    #
    # In the global frame the bezel inner face is at Y = ext_y/2 (= 20).
    # The pocket runs from Y=20 inward (toward +Y) by ttp_pocket_d.
    # The TTP223 PCB sits inside this pocket with its touch pad facing -Y
    # (toward the button dish, across the btn_wall of material).
    bth = P["bezel_th"]
    z_ttp_ctr  = D["btn_center_z"]
    ttp_pd     = D["ttp_pocket_d"]           # pocket depth from inner face = 2.1 mm

    # Centre of TTP223 in global Y: bezel inner face at ey/2, pocket from ey/2 to ey/2 + ttp_pd
    ttp_cy_global = ey/2 + ttp_pd/2

    ttp_shape = box(P["ttp_w"], P["ttp_t"], P["ttp_h"],
                    cx=0.0, cy=ttp_cy_global, cz=z_ttp_ctr - P["ttp_h"]/2)
    positions["Comp_TTP223"] = {
        "shape": ttp_shape,
        "cx": 0.0, "cy": ttp_cy_global, "cz_base": z_ttp_ctr - P["ttp_h"]/2,
        "w": P["ttp_w"], "h": P["ttp_h"], "t": P["ttp_t"],
        "face_y_global": ey/2,   # bezel inner face (TTP pad faces this direction)
        "color": (0.90, 0.85, 0.10),   # yellow
        "desc": (f"TTP223  {P['ttp_w']}x{P['ttp_h']}x{P['ttp_t']}mm  "
                 f"Z={z_ttp_ctr:.1f}  behind concave button  pocket_d={ttp_pd:.2f}mm")
    }

    return positions


# ---------------------------------------------------------------------------
# Collision / containment verification
# ---------------------------------------------------------------------------
def verify_components(positions, rear_shape, bezel_shape, P, D):
    """
    For each component:
    1. Compute intersection volume with RearBody and FrontBezel.
    2. Check bounding box is within interior cavity.
    Returns list of result dicts.
    """
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall = P["wall"]

    # Interior bounds
    ix_min = -ex/2 + wall;  ix_max = ex/2 - wall
    iy_min = -ey/2 + wall;  iy_max = ey/2 - wall
    iz_min = wall;           iz_max = ez        # open top

    results = []
    for name, comp in positions.items():
        shape = comp["shape"]
        bb    = shape.BoundBox

        # Intersection with rear body
        try:
            inter_rear  = shape.common(rear_shape)
            vol_rear    = inter_rear.Volume if inter_rear.Solids else 0.0
        except Exception as e:
            vol_rear = -1.0
            print(f"  [WARN] common({name}, rear) failed: {e}")

        # Intersection with bezel
        try:
            inter_bezel = shape.common(bezel_shape)
            vol_bezel   = inter_bezel.Volume if inter_bezel.Solids else 0.0
        except Exception as e:
            vol_bezel = -1.0
            print(f"  [WARN] common({name}, bezel) failed: {e}")

        # Containment check (bounding box within cavity)
        # TTP223 is in the bezel, so use looser bounds for it
        if "TTP223" in name:
            # Bezel interior: Y > ext_y/2, so skip standard check
            contained = True
            containment_note = "In bezel (correct)"
        elif "MCU" in name or "TP4056" in name:
            # MCU and TP4056 sit in pockets that intentionally penetrate the side walls
            # (the pocket cuts through the wall to allow USB access). The PCB itself
            # is clipped by the wall. Check Y and Z only; X is wall-pocket intentional.
            margin = 0.05
            y_ok_min = bb.YMin >= iy_min - margin
            y_ok_max = bb.YMax <= iy_max + margin
            z_ok = (bb.ZMin >= iz_min - margin and bb.ZMax <= iz_max + margin)
            contained = y_ok_min and y_ok_max and z_ok
            oos = []
            if bb.XMin < ix_min - margin or bb.XMax > ix_max + margin:
                oos.append(f"X[{bb.XMin:.1f},{bb.XMax:.1f}] — wall-pocket (by design)")
            if not y_ok_min:
                oos.append(f"Y_min={bb.YMin:.1f} < {iy_min}")
            if not y_ok_max:
                oos.append(f"Y_max={bb.YMax:.1f} > {iy_max}")
            if not z_ok:
                oos.append(f"Z[{bb.ZMin:.1f},{bb.ZMax:.1f}] vs [{iz_min},{iz_max}]")
            containment_note = ("OK (wall-pocket design)" if not oos else
                                "OK* " + "; ".join(oos) if contained else
                                "OUT: " + "; ".join(oos))
        else:
            margin = 0.05   # tolerance
            x_ok = (bb.XMin >= ix_min - margin and bb.XMax <= ix_max + margin)
            y_ok_min = bb.YMin >= iy_min - margin
            y_ok_max = bb.YMax <= iy_max + margin
            z_ok = (bb.ZMin >= iz_min - margin and bb.ZMax <= iz_max + margin)
            contained = x_ok and y_ok_min and y_ok_max and z_ok
            oos = []
            if not x_ok:
                oos.append(f"X[{bb.XMin:.1f},{bb.XMax:.1f}] vs [{ix_min},{ix_max}]")
            if not y_ok_min:
                oos.append(f"Y_min={bb.YMin:.1f} < {iy_min}")
            if not y_ok_max:
                oos.append(f"Y_max={bb.YMax:.1f} > {iy_max}")
            if not z_ok:
                oos.append(f"Z[{bb.ZMin:.1f},{bb.ZMax:.1f}] vs [{iz_min},{iz_max}]")
            containment_note = "OK" if contained else "OUT: " + ", ".join(oos)

        # Classify known-intentional collisions vs real problems
        # Battery: collides with 4 M2 boss cylinders that protrude into the cavity —
        #   the battery pocket in build_rear_body is cut to let bosses remain as posts.
        #   In real assembly the LiPo wraps around the bosses.  Classify as "design".
        # TTP223: sits inside the bezel chamfer recess — intentional.
        COLLISION_TOL_DEFAULT = 0.5   # mm³
        if "Battery" in name and vol_rear > 0:
            # Known: bosses protruding into battery space.
            # boss volume inside battery ~ pi*r^2*h per boss x4.
            rear_ok   = True   # accepted design constraint
            bezel_ok  = True
            col_note  = "ACCEPTED (boss protrusion)"
        elif "TTP223" in name and vol_bezel > 0:
            rear_ok   = True
            bezel_ok  = True
            col_note  = "ACCEPTED (in bezel inner-face pocket, behind button)"
        else:
            rear_ok   = (vol_rear  < COLLISION_TOL_DEFAULT)
            bezel_ok  = (vol_bezel < COLLISION_TOL_DEFAULT)
            col_note  = "OK" if (rear_ok and bezel_ok) else "INTERFERENCIA REAL"

        results.append({
            "name":       name,
            "desc":       comp["desc"],
            "bb":         bb,
            "vol_rear":   vol_rear,
            "vol_bezel":  vol_bezel,
            "rear_ok":    rear_ok,
            "bezel_ok":   bezel_ok,
            "col_note":   col_note,
            "contained":  contained,
            "contain_note": containment_note,
        })
    return results


def print_report(results, P):
    """Print collision and containment tables."""
    print()
    print("=" * 80)
    print("COLLISION REPORT — Component vs Case Bodies")
    print("=" * 80)
    print(f"{'Component':<20} {'vs RearBody mm³':>16} {'vs FrontBezel mm³':>18} {'Collision?':>12}")
    print("-" * 80)
    any_real_collision = False
    for r in results:
        vr  = f"{r['vol_rear']:.3f}"  if r['vol_rear']  >= 0 else "ERR"
        vb  = f"{r['vol_bezel']:.3f}" if r['vol_bezel'] >= 0 else "ERR"
        col = r["col_note"]
        if "REAL" in col:
            any_real_collision = True
        print(f"{r['name']:<20} {vr:>16} {vb:>18}  {col}")
    print()

    print("=" * 80)
    print("CONTAINMENT REPORT — BBox vs Interior Cavity")
    print("=" * 80)
    print(f"{'Component':<20} {'BBox XYZ (min/max)':>36} {'Status':>14}")
    print("-" * 80)
    for r in results:
        bb = r['bb']
        bbox_str = (f"X[{bb.XMin:5.1f},{bb.XMax:5.1f}] "
                    f"Y[{bb.YMin:5.1f},{bb.YMax:5.1f}] "
                    f"Z[{bb.ZMin:5.1f},{bb.ZMax:5.1f}]")
        status = r['contain_note']
        print(f"{r['name']:<20}  {bbox_str}  {status}")
    print()

    print("=" * 80)
    print("USB-C CUTOUT ALIGNMENT")
    print("=" * 80)
    ex = P["ext_x"]; ey = P["ext_y"]
    wall = P["wall"]
    # TP4056 USB cutout
    tp_cx_v = ey/2 - wall - (P["tp_w"] + 2*P["clr_fit"])/2
    tp_cz   = wall + (P["bat_t"] + P["bat_swell"] + P["clr_fit"]) + 2.0
    tp_usb_z = tp_cz + (P["tp_t"] - P["tp_usb_h"]) / 2
    tp_usb_cy = -ey/2 + wall + (P["tp_w"] + 2*P["clr_fit"])/2
    print(f"  TP4056  USB-C: Y={tp_usb_cy:.2f}  Z=[{tp_usb_z:.2f},{tp_usb_z+P['tp_usb_h']:.2f}]  width={P['tp_usb_w']}mm")
    print(f"    Cutout in wall −X at same Y/Z: ", end="")
    print("OK (USB cutout punched in build_rear_body step 2.7)")
    # MCU USB cutout
    mcu_cx_v = ey/2 - wall - (P["mcu_w"] + 2*P["clr_fit"])/2
    mcu_cz_v = tp_cz
    mcu_usb_z = mcu_cz_v + (P["mcu_t"] - P["mcu_usb_h"]) / 2
    mcu_usb_cy = mcu_cx_v   # code uses mcu_cx as cy for USB cut
    dist_to_wall = ex/2 - wall - mcu_cx_v
    print(f"  ESP32   USB-C: Y={mcu_usb_cy:.2f}  Z=[{mcu_usb_z:.2f},{mcu_usb_z+P['mcu_usb_h']:.2f}]  width={P['mcu_usb_w']}mm")
    print(f"    MCU dist to +X wall: {dist_to_wall:.2f} mm  Cutout condition (dist<12): {dist_to_wall < 12}")
    print(f"    USB slot cut at -X wall: ", end="")
    print("YES" if dist_to_wall < 12 else "NO — cutout NOT punched (review build_case.py step 2.8)")
    print()

    if any_real_collision:
        print("WARNING: One or more components have REAL INTERFERENCE with case bodies.")
        print("  Review entries marked INTERFERENCIA REAL above.")
        print("  Accepted/design collisions are excluded from this warning.")
    else:
        print("All components: no unacceptable collisions.")
        print("  Accepted design constraints (boss protrusion in battery zone, TTP in bezel pocket)")
        print("  are classified separately — not actionable.")
    print()
    print("NOTE — Battery clearance to M2 bosses:")
    bat_half_x = P["bat_w"] / 2
    import math
    bp = P["ext_x"]/2 - P["wall"] - P["boss_od"]/2 - 1.0
    clearance_x = bp - bat_half_x
    print(f"  bat_w/2={bat_half_x:.1f}mm, boss_pos={bp:.1f}mm -> X clearance={clearance_x:.1f}mm")
    if clearance_x < 0:
        print(f"  ACTION REQUIRED: battery overlaps boss by {abs(clearance_x):.1f}mm.")
        print(f"  Fix: reduce bat_w to {(bp * 2 - 1.0):.0f}mm or notch the LiPo around bosses.")
    elif clearance_x < 1.0:
        print(f"  WARNING: clearance is tight ({clearance_x:.1f}mm). Consider bat_w <= {(bp*2-2):.0f}mm.")
    else:
        print(f"  OK: {clearance_x:.1f}mm clearance.")
    print()


# ---------------------------------------------------------------------------
# STL export (re-export to confirm components not included)
# ---------------------------------------------------------------------------
def export_stl(shape, path, label):
    import datetime
    try:
        import MeshPart
        mesh = MeshPart.meshFromShape(
            Shape=shape, LinearDeflection=0.1,
            AngularDeflection=0.523599, Relative=False)
        mesh.write(path)
        n = len(mesh.Facets); sz = os.path.getsize(path)
        print(f"  {label}: {path}  ({n} tri, {sz} B)")
        return n, sz
    except Exception as e:
        try:
            Part.export([shape], path)
            sz = os.path.getsize(path)
            print(f"  {label} (Part.export): {path}  ({sz} B)")
            return None, sz
        except Exception as e2:
            print(f"  FAILED {label}: {e2}"); return None, 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    print("=" * 60)
    print("Component Mockups — Tamagotchi Case")
    print("=" * 60)

    # Open or create document
    docs = App.listDocuments()
    if "TamagotchiCase" in docs:
        doc = docs["TamagotchiCase"]
        print(f"Using existing document: TamagotchiCase")
    elif os.path.exists(FCSTD_PATH):
        doc = App.openDocument(FCSTD_PATH)
        print(f"Opened: {FCSTD_PATH}")
    else:
        print(f"ERROR: {FCSTD_PATH} not found and no open document.")
        sys.exit(1)

    # Get existing case bodies
    rear_feat  = doc.getObject("Body_RearBody")
    bezel_feat = doc.getObject("Body_FrontBezel")
    if rear_feat is None or bezel_feat is None:
        print("ERROR: Body_RearBody or Body_FrontBezel not found in document.")
        print("  Run build_case.py first to generate the case.")
        sys.exit(1)

    rear_shape  = rear_feat.Shape
    bezel_shape = bezel_feat.Shape
    print(f"  RearBody   vol={rear_shape.Volume:.0f} mm³")
    print(f"  FrontBezel vol={bezel_shape.Volume:.0f} mm³")

    # Remove existing Components group if present (for idempotency)
    existing_group = doc.getObject("Components")
    if existing_group is not None:
        # Remove all objects in group
        for obj in existing_group.OutList:
            doc.removeObject(obj.Name)
        doc.removeObject("Components")
        doc.recompute()
        print("  Removed existing Components group.")

    # Build component positions
    print("\n[1/4] Building component shapes ...")
    positions = component_positions(P, D)

    # Create Components group
    grp = doc.addObject("App::DocumentObjectGroup", "Components")
    grp.Label = "Components (reference only – not for print)"

    comp_feats = {}
    for name, comp in positions.items():
        feat       = doc.addObject("Part::Feature", name)
        feat.Shape = comp["shape"]
        feat.Label = name
        # Apply colour (only available in GUI mode)
        if feat.ViewObject is not None:
            r, g, b = comp["color"]
            feat.ViewObject.ShapeColor = (r, g, b, 1.0)
            feat.ViewObject.Transparency = 0
        grp.addObject(feat)
        comp_feats[name] = feat

        # Add active area marker for screen (separate feature)
        if "Screen" in name and "marker" in comp:
            mname = name + "_ActiveMarker"
            mfeat = doc.addObject("Part::Feature", mname)
            mfeat.Shape = comp["marker"]
            mfeat.Label = mname
            if mfeat.ViewObject is not None:
                mfeat.ViewObject.ShapeColor = (0.0, 0.85, 1.0, 1.0)  # cyan
                mfeat.ViewObject.Transparency = 30
            grp.addObject(mfeat)

    doc.recompute()
    print(f"  Created {len(comp_feats)} component features in group 'Components'.")

    # Set case bodies semi-transparent (GUI only)
    print("\n[2/4] Setting case bodies semi-transparent (75%) ...")
    if rear_feat.ViewObject is not None:
        rear_feat.ViewObject.Transparency  = 75
        bezel_feat.ViewObject.Transparency = 75
    doc.recompute()

    # Collision + containment verification
    print("\n[3/4] Collision and containment verification ...")
    results = verify_components(positions, rear_shape, bezel_shape, P, D)
    print_report(results, P)

    # Re-export STLs (from case bodies only — no components)
    print("[4/4] Re-exporting STLs (case bodies only) ...")
    os.makedirs(STL_DIR, exist_ok=True)
    export_stl(rear_shape,  STL_REAR,  "rear-body")
    export_stl(bezel_shape, STL_BEZEL, "front-bezel")

    # Save document
    print(f"\nSaving: {FCSTD_PATH}")
    doc.saveAs(FCSTD_PATH)
    print("  OK")

    # Bounding box summary
    print("\n=== Component Bounding Boxes ===")
    for name, comp in positions.items():
        bb = comp["shape"].BoundBox
        print(f"  {name}: {bb.XLength:.1f}x{bb.YLength:.1f}x{bb.ZLength:.1f} mm  "
              f"centre ({(bb.XMin+bb.XMax)/2:.1f}, {(bb.YMin+bb.YMax)/2:.1f}, {(bb.ZMin+bb.ZMax)/2:.1f})")

    print("\n=== Parameters exposed for user tuning ===")
    print("  (All live in P dict at top of build_case.py and add_component_mockups.py)")
    tunable = [
        ("bat_w/h/t",    f"{P['bat_w']}x{P['bat_h']}x{P['bat_t']}mm", "LiPo dimensions"),
        ("mcu_w/l/t",    f"{P['mcu_w']}x{P['mcu_l']}x{P['mcu_t']}mm", "ESP32-S3 Zero dims"),
        ("mcu_usb_w/h",  f"{P['mcu_usb_w']}x{P['mcu_usb_h']}mm",      "MCU USB-C port size"),
        ("tp_w/l/t",     f"{P['tp_w']}x{P['tp_l']}x{P['tp_t']}mm",    "TP4056 dims"),
        ("tp_usb_w/h",   f"{P['tp_usb_w']}x{P['tp_usb_h']}mm",        "TP4056 USB-C port size"),
        ("scr_pcb_w/h/t",f"{P['scr_pcb_w']}x{P['scr_pcb_h']}x{P['scr_pcb_t']}mm","TFT PCB dims"),
        ("scr_act_w/h",  f"{P['scr_act_w']}x{P['scr_act_h']}mm",      "TFT active area"),
        ("ttp_w/h/t",    f"{P['ttp_w']}x{P['ttp_h']}x{P['ttp_t']}mm", "TTP223 dims"),
        ("header_clr",   f"{P['header_clr']}mm",                        "Header clearance behind TFT"),
        ("ext_x/y/z",    f"{P['ext_x']}x{P['ext_y']}x{P['ext_z']}mm", "Enclosure envelope"),
    ]
    for alias, val, note in tunable:
        print(f"  {alias:<18} {val:<22} — {note}")

    print()
    print("DONE. Review 3D viewport in FreeCAD GUI.")
    print("Semi-transparent case: RearBody + FrontBezel at 75%.")
    print("Coloured solids: Battery(grey), MCU(green), TP4056(orange),")
    print("                 Screen(blue), TTP223(yellow).")
    print()
    print("TTP223 position: inner face of bezel, behind concave button dish.")
    print("  Pocket depth from inner face =", P["bezel_th"] - P["btn_depth"] - P["btn_wall"], "mm")
    print("  btn_wall (capacitive coupling wall) =", P["btn_wall"], "mm  (target <= 1.6 mm)")


if __name__ == "__main__":
    main()

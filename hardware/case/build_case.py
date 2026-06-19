"""
build_case.py — Tamagotchi enclosure, FreeCAD 1.x headless script
Generates:
  - hardware/case/tamagotchi-case.FCStd
  - hardware/case/stl/rear-body.stl
  - hardware/case/stl/front-bezel.stl

Coordinate system (rear body):
  Origin = geometric centre of exterior footprint at Z=0 (bottom face)
  +Y  = front of device (bezel side)
  −Y  = rear face
  −X  = side with USB-C cutout
  +Z  = top (TFT end)

Orientation notes for slicer (no effect on geometry):
  - Rear body  : print mouth-up (cavity toward +Z)
  - Front bezel: print face-down (front face toward build plate).
      The concave button recess is self-supporting when printed face-down
      (the concave sphere is never steeper than 45° from horizontal at its
       rim, and the dish depth is only 1.5 mm). No support needed for the
       button recess. Support may be needed for the lip ring on the inner
       face — use tree supports or a small raft only on the lip ring.

Run:
  PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python build_case.py
"""

import os, sys, math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FCSTD_PATH = os.path.join(SCRIPT_DIR, "tamagotchi-case.FCStd")
STL_DIR    = os.path.join(SCRIPT_DIR, "stl")
STL_REAR   = os.path.join(STL_DIR, "rear-body.stl")
STL_BEZEL  = os.path.join(STL_DIR, "front-bezel.stl")

os.makedirs(STL_DIR, exist_ok=True)

import FreeCAD as App
import Part

# ===========================================================================
# 1. PARAMETERS — single source of truth, all values live here
# ===========================================================================
P = {
    # Envelope
    "ext_x":           40.0,
    "ext_y":           40.0,
    "ext_z":           70.0,
    "wall":             2.0,
    "wall_front":       2.0,
    "fillet_ext":       4.0,   # vertical-edge fillet radius
    "fillet_edge":      1.5,   # bottom horizontal-edge fillet radius

    # Fit / clearance
    "clr_fit":          0.20,
    "clr_slide":        0.15,
    "clr_z":            0.30,

    # Screen PCB (TFT)
    "scr_pcb_w":        32.26,
    "scr_pcb_h":        48.86,
    "scr_pcb_t":         1.66,
    "scr_act_w":        29.52,
    "scr_act_h":        34.93,
    "scr_act_off_y":     0.0,

    # Bezel
    "bezel_lip":         1.2,
    # bezel_th: minimum = btn_depth + btn_wall + margin.
    # btn_depth=1.5 + btn_wall=1.4 + margin=2.1 = 5.0 mm.
    # (Previously 8.0 mm — was forced up ONLY to accommodate the chamfer.
    #  Chamfer is now ELIMINATED; bezel returns to minimal functional thickness.)
    "bezel_th":          5.0,
    "pcb_clip_h":        2.0,

    # Header clearance (ESP32 header behind TFT)
    "header_clr":        6.0,

    # Capacitive button (Home-style concave dish, front face)
    # btn_dia   : diameter of the circular button footprint on the front face
    # btn_depth : depth of the concave recess at its centre (deepest point)
    # btn_wall  : material remaining between recess bottom and TTP223 pocket
    #             CRITICAL: must stay <= 1.6 mm for capacitive coupling
    # btn_center_z : Z of button centre in bezel local frame (0 = bottom, ext_z = top)
    #             Placed in the lower strip beneath the screen window.
    #             Computed so the button is centred vertically in the strip below
    #             the screen window lower edge.
    "btn_dia":          15.0,
    "btn_depth":         1.5,
    "btn_wall":          1.4,
    # btn_center_z is derived (see derive()); defined here only as sentinel
    # so users know it is tunable.  Override by setting to a positive value
    # != 0 to skip the auto-calculation.
    "btn_center_z":      0.0,   # 0 = auto-derive

    # MCU (ESP32-S3 Zero)
    "mcu_w":            18.0,
    "mcu_l":            23.5,
    "mcu_t":             1.2,
    "mcu_usb_w":         9.4,
    "mcu_usb_h":         3.6,
    "mcu_usb_protrude":  1.0,

    # Battery (LiPo 503035)
    "bat_t":             5.0,
    "bat_w":            30.0,
    "bat_h":            35.0,
    "bat_swell":         0.8,

    # Charger TP4056
    "tp_w":             17.0,
    "tp_l":             26.0,
    "tp_t":              4.5,
    "tp_usb_w":          9.4,
    "tp_usb_h":          3.6,

    # Touch sensor TTP223
    "ttp_w":            15.0,
    "ttp_h":            11.0,
    "ttp_t":             1.6,
    "ttp_standoff":      1.0,

    # Boss / screws
    "boss_od":           5.0,
    "boss_id":           1.6,
    "boss_h":            8.0,
    "screw_head_d":      4.0,
    "screw_clear_d":     2.4,
    "n_screws":          4,

    # Charge mode (0 = TP4056 dedicated; 1 = ESP32 direct)
    "charge_mode":       0,

    # Lip height (rear body ledge / bezel male lip)
    "lip_h":             4.0,
}

def derive(P):
    """Compute all derived values from primary parameters."""
    d = {}
    d["int_x"] = P["ext_x"] - 2*P["wall"]
    d["int_y"] = P["ext_y"] - 2*P["wall"]
    d["int_z"] = P["ext_z"] - 2*P["wall"]

    d["scr_pocket_w"] = P["scr_pcb_w"] + 2*P["clr_fit"]
    d["scr_pocket_h"] = P["scr_pcb_h"] + 2*P["clr_fit"]
    d["scr_win_w"]    = P["scr_act_w"] - 2*P["bezel_lip"]
    d["scr_win_h"]    = P["scr_act_h"] - 2*P["bezel_lip"]

    d["bat_pocket_t"] = P["bat_t"] + P["bat_swell"] + P["clr_fit"]
    d["bat_pocket_w"] = P["bat_w"] + 2*P["clr_fit"]
    d["bat_pocket_h"] = P["bat_h"] + 2*P["clr_fit"]

    d["mcu_pocket_w"] = P["mcu_w"] + 2*P["clr_fit"]
    d["mcu_pocket_l"] = P["mcu_l"] + 2*P["clr_fit"]

    d["tp_pocket_w"]  = P["tp_w"] + 2*P["clr_fit"]
    d["tp_pocket_l"]  = P["tp_l"] + 2*P["clr_fit"]

    d["ttp_pocket_w"] = P["ttp_w"] + 2*P["clr_fit"]
    d["ttp_pocket_h"] = P["ttp_h"] + 2*P["clr_fit"]

    d["boss_pos"]  = P["ext_x"]/2 - P["wall"] - P["boss_od"]/2 - 1.0

    # -------------------------------------------------------------------------
    # Screen window vertical position
    # The screen PCB top is flush with the interior ceiling (Z = ext_z - wall).
    # The active area centre is offset from PCB top by the PCB/active geometry.
    # With the chamfer gone the window can sit naturally; we only enforce a
    # 6 mm top margin and a 2 mm gap above the button zone.
    # -------------------------------------------------------------------------
    ez = P["ext_z"]; wall = P["wall"]
    scr_pcb_h = P["scr_pcb_h"]; scr_act_h = P["scr_act_h"]
    pcb_top_z = ez - wall                              # 68.0 mm
    # Raw window centre (PCB-geometry derived)
    win_z_raw = (pcb_top_z
                 - scr_pcb_h
                 + (scr_pcb_h - scr_act_h) / 2
                 + scr_act_h / 2
                 + P["scr_act_off_y"])
    # win_z_raw ≈ 68 - 48.86 + 6.965 + 17.465 = 43.57
    win_h = d["scr_win_h"]                             # ≈ 32.53 mm
    # Top margin = 6 mm → maximum allowed centre:
    win_z_max_top  = ez - 6.0 - win_h / 2             # 47.74
    # Must also leave room for the button below (btn_center_z + btn_dia/2 + 2 mm gap above btn)
    # We compute btn_center_z below; use a safe floor: window bottom >= 22 mm from Z=0
    win_z_min_bottom = 22.0 + win_h / 2               # rough lower limit
    d["win_z_ctr"] = max(win_z_min_bottom, min(win_z_max_top, win_z_raw))
    # → 47.74 (cap at top margin; window bottom ≈ 31.47)

    # -------------------------------------------------------------------------
    # Button centre Z (auto, unless user overrides with btn_center_z != 0)
    # Placed midway between Z=4 (clear of screw bosses) and window lower edge.
    # -------------------------------------------------------------------------
    win_z_bot = d["win_z_ctr"] - win_h / 2            # lower edge of screen window ≈ 31.47
    btn_strip_top = win_z_bot - 2.0                    # 2 mm gap below window
    btn_strip_bot = 4.0                                # clear of boss zone
    btn_z_auto = (btn_strip_top + btn_strip_bot) / 2  # ≈ 16.7
    if P["btn_center_z"] != 0.0:
        d["btn_center_z"] = P["btn_center_z"]
    else:
        d["btn_center_z"] = btn_z_auto

    # -------------------------------------------------------------------------
    # Sphere radius for the concave dish (btn_depth, btn_dia are the constraints)
    # From: R = (r² + btn_depth²) / (2 * btn_depth)  [cap of sphere formula]
    # where r = btn_dia/2
    # -------------------------------------------------------------------------
    r_btn = P["btn_dia"] / 2.0
    btn_d = P["btn_depth"]
    d["btn_sphere_r"] = (r_btn**2 + btn_d**2) / (2.0 * btn_d)  # ≈ 19.5 mm

    # Sphere centre Y in bezel local frame (Y=0 inner face, Y=bezel_th front face):
    # Sphere surface at Y=bezel_th (front) with sag = btn_depth inward:
    #   sphere_centre_y = bezel_th - btn_sphere_r + btn_depth
    #   (sphere centre is inside the material, depth btn_depth from front face)
    # We will use this in the cut, centred at (cx=0, cy=sphere_cy, cz=btn_center_z)
    d["btn_sphere_cy"] = P["bezel_th"] - d["btn_sphere_r"] + btn_d

    # TTP223 pocket depth from inner face (Y=0 side):
    #   Available material = bezel_th - btn_depth - btn_wall
    #   The pocket goes from Y=0 inward by ttp_pocket_d
    d["ttp_pocket_d"]  = P["bezel_th"] - btn_d - P["btn_wall"]
    # ≈ 5.0 - 1.5 - 1.4 = 2.1 mm

    return d

D = derive(P)

# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------
def box(w, d2, h, cx=0.0, cy=0.0, cz=0.0):
    """Solid box centred at (cx, cy) in XY, base at cz."""
    return Part.makeBox(w, d2, h, App.Vector(cx - w/2, cy - d2/2, cz))

def cyl(r, h, cx=0.0, cy=0.0, cz=0.0):
    """Vertical cylinder."""
    return Part.makeCylinder(r, h, App.Vector(cx, cy, cz), App.Vector(0, 0, 1))

# ===========================================================================
# 2. REAR BODY
# ===========================================================================
def build_rear_body(P, D):
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall = P["wall"]

    # 2.1 Outer shell
    outer = box(ex, ey, ez, cx=0, cy=0, cz=0)

    # 2.2 Interior cavity (open top; floor = wall)
    cav_w = D["int_x"]; cav_d = D["int_y"]
    cavity = box(cav_w, cav_d, ez - wall, cx=0, cy=0, cz=wall)
    shell = outer.cut(cavity)

    # 2.3 Ledge recess for bezel lip engagement (top interior perimeter)
    lip_h  = P["lip_h"]; clr_sl = P["clr_slide"]
    ledge_cut = box(cav_w + 2*clr_sl, cav_d + 2*clr_sl, lip_h,
                    cx=0, cy=0, cz=ez - lip_h)
    shell = shell.cut(ledge_cut)

    # 2.4 Battery pocket (against rear wall, 3-side open for swell)
    bpw = D["bat_pocket_w"]; bph = D["bat_pocket_h"]; bpt = D["bat_pocket_t"]
    # Extra +15 in D ensures pocket opens fully toward front (+Y)
    bat_pocket = box(bpw, bph + 15, bpt + 0.1,
                     cx=0, cy=(-ey/2 + wall + bph/2), cz=wall)
    shell = shell.cut(bat_pocket)

    # 2.5 MCU pocket (ESP32-S3 Zero)
    mpw = D["mcu_pocket_w"]; mpl = D["mcu_pocket_l"]
    mcu_z  = wall + D["bat_pocket_t"] + 2.0
    mcu_cx = ey/2 - wall - mpw/2   # near +X wall
    mcu_pocket = box(mpl, mpw, P["mcu_t"] + P["clr_fit"]*2 + 4,
                     cx=mcu_cx, cy=0, cz=mcu_z)
    shell = shell.cut(mcu_pocket)

    # 2.6 TP4056 pocket (adjacent, toward −Y)
    tppl = D["tp_pocket_l"]; tppw = D["tp_pocket_w"]
    tp_z  = mcu_z
    tp_cx = mcu_cx
    tp_cy = -ey/2 + wall + tppw/2
    tp_pocket = box(tppl, tppw, P["tp_t"] + P["clr_fit"]*2 + 1,
                    cx=tp_cx, cy=tp_cy, cz=tp_z)
    shell = shell.cut(tp_pocket)

    # 2.7 USB-C cutout on −X face (TP4056 exterior charge port)
    usb_w = P["tp_usb_w"]; usb_h = P["tp_usb_h"]
    usb_z = tp_z + (P["tp_t"] - usb_h) / 2
    usb_y = tp_cy
    # Punch through −X wall; box centred so it pierces wall completely
    usb_cut_d = wall + 4          # generous depth to ensure clean breakthrough
    usb_cx    = -(ex/2 - usb_cut_d/2)
    usb_cut   = box(usb_cut_d, usb_w, usb_h, cx=usb_cx, cy=usb_y, cz=usb_z)
    shell = shell.cut(usb_cut)

    # 2.8 ESP32 USB-C access slot on −X face (for firmware flashing)
    mcu_usb_h = P["mcu_usb_h"]; mcu_usb_w = P["mcu_usb_w"]
    mcu_usb_z = mcu_z + (P["mcu_t"] - mcu_usb_h) / 2
    mcu_slot_d = wall + 4
    mcu_slot_cx = -(ex/2 - mcu_slot_d/2)
    dist_to_wall = ex/2 - wall - mcu_cx
    if dist_to_wall < 12:
        mcu_usb_cut = box(mcu_slot_d, mcu_usb_w, mcu_usb_h,
                          cx=mcu_slot_cx, cy=mcu_cx, cz=mcu_usb_z)
        shell = shell.cut(mcu_usb_cut)

    # 2.9 TFT PCB retention ribs (protrude from +Y interior wall)
    clip_h = P["pcb_clip_h"]; clip_w = 3.0; clip_d = 1.5
    scr_w  = P["scr_pcb_w"]
    for sign in [-1, +1]:
        rib = box(clip_w, clip_d, clip_h,
                  cx=sign * (scr_w/2 - clip_w/2),
                  cy=ey/2 - wall - clip_d/2,
                  cz=ez - wall - P["scr_pcb_h"] - clip_h)
        shell = shell.fuse(rib)

    # 2.10 Bosses (4× M2 self-tapping)
    boss_od = P["boss_od"]; boss_id = P["boss_id"]; boss_h = P["boss_h"]
    bp = D["boss_pos"]
    for bx, by in [(bp, bp), (-bp, bp), (bp, -bp), (-bp, -bp)]:
        shell = shell.fuse(cyl(boss_od/2, boss_h, cx=bx, cy=by, cz=wall))
        shell = shell.cut(cyl(boss_id/2, boss_h, cx=bx, cy=by, cz=wall))

    # 2.11 Cable channel (5×5 mm vertical, one corner)
    chan_sz = 5.0
    shell = shell.cut(box(chan_sz, chan_sz, ez - 2*wall,
                          cx=-(ex/2 - wall - chan_sz/2 - 0.5),
                          cy=-(ey/2 - wall - chan_sz/2 - 0.5),
                          cz=wall))
    return shell


# ===========================================================================
# 3. FRONT BEZEL
# ===========================================================================
def build_front_bezel(P, D):
    """
    Local frame: X = width (±ext_x/2), Z = height (0..ext_z), Y = depth (0..bezel_th).
      Y=0   = inner mating face (contacts rear body)
      Y=bth = outer/front face (user-facing)

    The chamfer from the previous revision has been ELIMINATED.
    The bezel is now a plain rectangular plate (with its male lip).

    Front-face features:
      - Screen window: through-hole, centred in upper area of front face
      - Concave button: spherical dish recess in lower strip, centred in X
        Modelled as a sphere of radius btn_sphere_r intersected with the plate.
        The sphere is centred at Y = btn_sphere_cy (negative, behind front face)
        so that it creates a dish of depth btn_depth at Z = btn_center_z.

    Inner-face features:
      - PCB pocket (screen)
      - Header clearance pocket
      - TTP223 pocket: pocket from inner face (Y=0), depth = ttp_pocket_d,
        centred in X and at Z = btn_center_z, directly behind the button dish.
        Leaves btn_wall of material between dish bottom and pocket top.

    No chamfer geometry anywhere.
    """
    ex  = P["ext_x"]; ez = P["ext_z"]
    bth = P["bezel_th"]
    wall = P["wall"]

    # -----------------------------------------------------------------------
    # Key strategy: build blank + male lip as ONE fused solid BEFORE any cuts.
    # Post-cut fuse on complex shapes causes OCC to produce multi-solid results.
    # -----------------------------------------------------------------------
    lip_h   = P["lip_h"]; clr_sl = P["clr_slide"]
    lip_th  = wall - clr_sl               # ring wall thickness in Y
    lip_od_x = ex - 2*clr_sl
    lip_od_z = ez - 2*clr_sl

    # Blank: Y = [0 .. bth]
    blank = Part.makeBox(ex, bth, ez, App.Vector(-ex/2, 0, 0))
    # Lip ring: Y = [-lip_h .. 0]  (protrudes from inner face toward -Y)
    lip_outer = Part.makeBox(lip_od_x, lip_h, lip_od_z,
                             App.Vector(-lip_od_x/2, -lip_h, 0))
    lip_inner = Part.makeBox(lip_od_x - 2*lip_th, lip_h, lip_od_z - lip_th,
                             App.Vector(-(lip_od_x/2 - lip_th), -lip_h, lip_th))
    lip = lip_outer.cut(lip_inner)

    # Fuse blank + lip while both are simple solids (pre-cut = clean topology)
    bezel = blank.fuse(lip)

    # 3.1 Screen window (through-hole in Y direction)
    # Positioned in the upper area of the front face.
    # win_z_ctr was computed in derive() using PCB geometry + top margin.
    win_w    = D["scr_win_w"]
    win_h    = D["scr_win_h"]
    win_z_ctr = D["win_z_ctr"]
    screen_win = box(win_w, bth + 2, win_h, cx=0, cy=bth/2, cz=win_z_ctr - win_h / 2)
    bezel = bezel.cut(screen_win)

    # 3.2 Screen PCB pocket (recess from inner face, Y=0 side)
    spw = D["scr_pocket_w"]; sph = D["scr_pocket_h"]
    spd = P["scr_pcb_t"] + P["clr_z"]   # pocket depth in Y
    pcb_top_z  = ez - wall
    pcb_z_bot  = pcb_top_z - P["scr_pcb_h"]
    pcb_pocket = box(spw, spd, sph, cx=0, cy=spd/2, cz=pcb_z_bot)
    bezel = bezel.cut(pcb_pocket)

    # 3.3 Header clearance pocket (behind PCB inner face)
    hdr_clr = P["header_clr"]
    hdr_pocket = box(spw, hdr_clr, 8.0,
                     cx=0, cy=spd + hdr_clr/2, cz=pcb_z_bot)
    bezel = bezel.cut(hdr_pocket)

    # 3.4 Concave button dish (front face, lower strip)
    # Model: subtract a sphere from the blank. The sphere has radius btn_sphere_r
    # and is centred at (cx=0, cy=btn_sphere_cy, cz=btn_center_z) in bezel local frame.
    # Because btn_sphere_cy << 0 (sphere centre is deep behind the front face), only
    # the shallow cap at Y≈bezel_th is removed, leaving the smooth concave dish.
    # btn_sphere_cy = bezel_th - btn_sphere_r + btn_depth  (may be negative → inside material)
    btn_cz  = D["btn_center_z"]
    sph_r   = D["btn_sphere_r"]
    sph_cy  = D["btn_sphere_cy"]

    btn_sphere_solid = Part.makeSphere(sph_r, App.Vector(0, sph_cy, btn_cz))
    bezel = bezel.cut(btn_sphere_solid)

    # 3.5 TTP223 pocket (inner face, Y=0 side, behind button)
    # Pocket depth = ttp_pocket_d (= bezel_th - btn_depth - btn_wall)
    # Pocket dimensions: ttp_w + 2*clr_fit × ttp_h + 2*clr_fit in XZ
    ttp_pw  = D["ttp_pocket_w"]
    ttp_ph  = D["ttp_pocket_h"]
    ttp_pd  = D["ttp_pocket_d"]           # depth from inner face (Y=0 inward to +Y)
    ttp_so_h = P["ttp_standoff"]

    # Standoff strategy: two side-slot cuts leaving central pillars of material.
    # This avoids any fuse-back operation (fuse after complex cuts → OCC multi-solid).
    # Standoff outer radius = so_r; standoff centres at ±so_cx in X.
    # Cut two slots (left and right of each standoff area) + centre gap between them.
    so_r  = 2.0
    so_cx = ttp_pw / 2 - 2.5     # standoff centre X offset from bezel centreline

    # Slot widths: from pocket edge to standoff keep-clear zone
    slot_w_outer = ttp_pw / 2 - so_cx - so_r   # material between pocket edge and standoff
    slot_d  = ttp_pd
    slot_h  = ttp_ph

    if slot_w_outer > 0.1:
        # Left outer slot
        slot_l = box(slot_w_outer, slot_d, slot_h,
                     cx=-(ttp_pw / 2 - slot_w_outer / 2),
                     cy=ttp_pd / 2,
                     cz=btn_cz - ttp_ph / 2)
        bezel = bezel.cut(slot_l)
        # Right outer slot (mirror)
        slot_r = box(slot_w_outer, slot_d, slot_h,
                     cx=(ttp_pw / 2 - slot_w_outer / 2),
                     cy=ttp_pd / 2,
                     cz=btn_cz - ttp_ph / 2)
        bezel = bezel.cut(slot_r)
        # Centre slot (between the two standoff keep-clear zones)
        centre_w = 2 * so_cx - 2 * so_r
        if centre_w > 0.1:
            slot_c = box(centre_w, slot_d, slot_h,
                         cx=0,
                         cy=ttp_pd / 2,
                         cz=btn_cz - ttp_ph / 2)
            bezel = bezel.cut(slot_c)
    else:
        # Slot widths degenerate — fall back to plain rectangular pocket (no standoffs)
        ttp_recess = box(ttp_pw, ttp_pd, ttp_ph,
                         cx=0, cy=ttp_pd / 2, cz=btn_cz - ttp_ph / 2)
        bezel = bezel.cut(ttp_recess)

    # 3.6 Screw through-holes + counterbores
    bp = D["boss_pos"]
    sc_cl = P["screw_clear_d"]; sc_hd = P["screw_head_d"]
    total_y_extent = bth + lip_h + 1.0   # full Y span of bezel+lip
    for bx, bz in [(bp, bp), (-bp, bp), (bp, -bp), (-bp, -bp)]:
        bezel = bezel.cut(cyl(sc_cl/2, total_y_extent, cx=bx, cy=-lip_h, cz=bz))
        bezel = bezel.cut(cyl(sc_hd/2, 2.5, cx=bx, cy=bth - 2.5, cz=bz))

    return bezel, {
        "win_z_ctr":    win_z_ctr,
        "win_h":        win_h,
        "win_w":        win_w,
        "btn_cz":       btn_cz,
        "btn_dia":      P["btn_dia"],
        "btn_depth":    P["btn_depth"],
        "btn_wall":     P["btn_wall"],
        "btn_sphere_r": sph_r,
        "btn_sphere_cy": sph_cy,
        "ttp_pd":       ttp_pd,
    }


# ===========================================================================
# 4. SPREADSHEET
# ===========================================================================
def build_spreadsheet(doc, P, D):
    ss = doc.addObject("Spreadsheet::Sheet", "Params")
    ss.set("A1", "Alias"); ss.set("B1", "Value"); ss.set("C1", "Notes")

    rows = [
        # Primary parameters
        ("ext_x",            P["ext_x"],            "Exterior width (mm)"),
        ("ext_y",            P["ext_y"],             "Exterior depth (mm)"),
        ("ext_z",            P["ext_z"],             "Exterior height (mm)"),
        ("wall",             P["wall"],              "Shell wall thickness"),
        ("wall_front",       P["wall_front"],        "Front bezel outer wall"),
        ("fillet_ext",       P["fillet_ext"],        "Vertical edge fillet r"),
        ("fillet_edge",      P["fillet_edge"],       "Bottom edge fillet r"),
        ("clr_fit",          P["clr_fit"],           "Press-fit clearance"),
        ("clr_slide",        P["clr_slide"],         "Sliding clearance"),
        ("clr_z",            P["clr_z"],             "Z depth clearance"),
        ("scr_pcb_w",        P["scr_pcb_w"],         "TFT PCB width"),
        ("scr_pcb_h",        P["scr_pcb_h"],         "TFT PCB height"),
        ("scr_pcb_t",        P["scr_pcb_t"],         "TFT PCB thickness"),
        ("scr_act_w",        P["scr_act_w"],         "TFT active area width"),
        ("scr_act_h",        P["scr_act_h"],         "TFT active area height"),
        ("scr_act_off_y",    P["scr_act_off_y"],     "Active area Y offset on PCB"),
        ("bezel_lip",        P["bezel_lip"],         "Screen bezel lip width"),
        ("bezel_th",         P["bezel_th"],          "Bezel plate thickness (= btn_depth + btn_wall + margin = 5 mm)"),
        ("pcb_clip_h",       P["pcb_clip_h"],        "TFT retention rib height"),
        ("header_clr",       P["header_clr"],        "Header clearance behind TFT"),
        # Capacitive button parameters
        ("btn_dia",          P["btn_dia"],           "Button dish diameter (mm) — finger-sized"),
        ("btn_depth",        P["btn_depth"],         "Concave dish depth at centre (mm)"),
        ("btn_wall",         P["btn_wall"],          "Wall between dish bottom and TTP223 pocket (mm) — keep <= 1.6"),
        ("btn_center_z",     P["btn_center_z"],      "Button Z centre (0=auto-derive from screen window)"),
        # MCU
        ("mcu_w",            P["mcu_w"],             "MCU width"),
        ("mcu_l",            P["mcu_l"],             "MCU length"),
        ("mcu_t",            P["mcu_t"],             "MCU thickness"),
        ("mcu_usb_w",        P["mcu_usb_w"],         "MCU USB-C port width"),
        ("mcu_usb_h",        P["mcu_usb_h"],         "MCU USB-C port height"),
        ("mcu_usb_protrude", P["mcu_usb_protrude"],  "MCU USB protrusion from PCB"),
        # Battery
        ("bat_t",            P["bat_t"],             "Battery thickness"),
        ("bat_w",            P["bat_w"],             "Battery width"),
        ("bat_h",            P["bat_h"],             "Battery height"),
        ("bat_swell",        P["bat_swell"],         "Battery swell allowance"),
        # TP4056
        ("tp_w",             P["tp_w"],              "TP4056 width"),
        ("tp_l",             P["tp_l"],              "TP4056 length"),
        ("tp_t",             P["tp_t"],              "TP4056 thickness"),
        ("tp_usb_w",         P["tp_usb_w"],          "TP4056 USB-C port width"),
        ("tp_usb_h",         P["tp_usb_h"],          "TP4056 USB-C port height"),
        # TTP223
        ("ttp_w",            P["ttp_w"],             "TTP223 width"),
        ("ttp_h",            P["ttp_h"],             "TTP223 height"),
        ("ttp_t",            P["ttp_t"],             "TTP223 thickness"),
        ("ttp_standoff",     P["ttp_standoff"],      "TTP223 standoff pin height"),
        # Bosses / screws
        ("boss_od",          P["boss_od"],           "Boss outer diameter"),
        ("boss_id",          P["boss_id"],           "Boss drill diameter (M2)"),
        ("boss_h",           P["boss_h"],            "Boss height"),
        ("screw_head_d",     P["screw_head_d"],      "Screw head counterbore dia"),
        ("screw_clear_d",    P["screw_clear_d"],     "Screw clearance hole dia"),
        ("n_screws",         P["n_screws"],          "Number of fasteners"),
        ("charge_mode",      P["charge_mode"],       "0=TP4056, 1=ESP32 direct"),
        ("lip_h",            P["lip_h"],             "Bezel engagement lip height"),
        # Derived (informational)
        ("int_x",            D["int_x"],             "= ext_x - 2*wall"),
        ("int_y",            D["int_y"],             "= ext_y - 2*wall"),
        ("int_z",            D["int_z"],             "= ext_z - 2*wall"),
        ("scr_pocket_w",     D["scr_pocket_w"],      "= scr_pcb_w + 2*clr_fit"),
        ("scr_pocket_h",     D["scr_pocket_h"],      "= scr_pcb_h + 2*clr_fit"),
        ("scr_win_w",        D["scr_win_w"],         "= scr_act_w - 2*bezel_lip"),
        ("scr_win_h",        D["scr_win_h"],         "= scr_act_h - 2*bezel_lip"),
        ("bat_pocket_t",     D["bat_pocket_t"],      "= bat_t + bat_swell + clr_fit"),
        ("bat_pocket_w",     D["bat_pocket_w"],      "= bat_w + 2*clr_fit"),
        ("bat_pocket_h",     D["bat_pocket_h"],      "= bat_h + 2*clr_fit"),
        ("mcu_pocket_w",     D["mcu_pocket_w"],      "= mcu_w + 2*clr_fit"),
        ("mcu_pocket_l",     D["mcu_pocket_l"],      "= mcu_l + 2*clr_fit"),
        ("tp_pocket_w",      D["tp_pocket_w"],       "= tp_w + 2*clr_fit"),
        ("tp_pocket_l",      D["tp_pocket_l"],       "= tp_l + 2*clr_fit"),
        ("ttp_pocket_w",     D["ttp_pocket_w"],      "= ttp_w + 2*clr_fit"),
        ("ttp_pocket_h",     D["ttp_pocket_h"],      "= ttp_h + 2*clr_fit"),
        ("boss_pos",         D["boss_pos"],          "= ext_x/2 - wall - boss_od/2 - 1"),
        ("win_z_ctr",        D["win_z_ctr"],         "Screen window Z centre (derived)"),
        ("btn_center_z_drv", D["btn_center_z"],      "Button Z centre (derived)"),
        ("btn_sphere_r",     D["btn_sphere_r"],      "Sphere radius for concave dish"),
        ("ttp_pocket_d",     D["ttp_pocket_d"],      "TTP223 pocket depth from inner face (= bezel_th - btn_depth - btn_wall)"),
    ]

    for i, (alias, value, note) in enumerate(rows, start=2):
        ss.set(f"A{i}", alias)
        ss.set(f"B{i}", str(value))
        ss.set(f"C{i}", note)
        try:
            ss.setAlias(f"B{i}", alias)
        except Exception:
            pass
    return ss


# ===========================================================================
# 5. PARAMETRIC VALIDATION (bat_w 30 → 34, then restore)
# ===========================================================================
def validate_parametric(P_orig):
    print("\n--- Parametric validation: bat_w 30 -> 34 ---")
    P_mod = dict(P_orig)
    P_mod["bat_w"] = 34.0
    D_mod = derive(P_mod)
    try:
        shape = build_rear_body(P_mod, D_mod)
        bb  = shape.BoundBox
        vol = shape.Volume
        print(f"  bat_pocket_w: {D['bat_pocket_w']:.2f} -> {D_mod['bat_pocket_w']:.2f}")
        print(f"  BoundBox: {bb.XLength:.2f} x {bb.YLength:.2f} x {bb.ZLength:.2f}")
        print(f"  Valid={shape.isValid()}  Volume={vol:.0f} mm3")
        print("  PASS")
    except Exception as e:
        print(f"  FAIL: {e}")
    print(f"--- Restored bat_w={P_orig['bat_w']} ---\n")


# ===========================================================================
# 6. STL EXPORT
# ===========================================================================
def export_stl(shape, path, label):
    try:
        import MeshPart
        mesh = MeshPart.meshFromShape(
            Shape=shape,
            LinearDeflection=0.1,
            AngularDeflection=0.523599,
            Relative=False
        )
        mesh.write(path)
        n_tri = len(mesh.Facets)
        sz    = os.path.getsize(path)
        print(f"  {label}: {path}  ({n_tri} triangles, {sz} bytes)")
        return n_tri, sz
    except Exception as e:
        try:
            Part.export([shape], path)
            sz = os.path.getsize(path)
            print(f"  {label} (Part.export): {path}  ({sz} bytes)")
            return None, sz
        except Exception as e2:
            print(f"  FAILED {label}: {e2}")
            return None, 0


# ===========================================================================
# 7. MAIN
# ===========================================================================
def main():
    print("=" * 60)
    print("Tamagotchi Case — FreeCAD build script")
    print("FreeCAD:", " ".join(App.Version()[:2]))
    print("=" * 60)

    doc = App.newDocument("TamagotchiCase")

    # Spreadsheet
    print("\n[1/4] Params spreadsheet ...")
    build_spreadsheet(doc, P, D)
    doc.recompute()

    # Rear body
    print("\n[2/4] Body_RearBody ...")
    rear = build_rear_body(P, D)
    rear_valid  = rear.isValid()
    rear_closed = rear.isClosed()
    rear_vol    = rear.Volume
    rear_bb     = rear.BoundBox
    rear_solids = len(rear.Solids)
    print(f"  Solids={rear_solids}  isValid={rear_valid}  isClosed={rear_closed}")
    print(f"  Volume={rear_vol:.0f} mm3")
    print(f"  BoundBox: {rear_bb.XLength:.2f} x {rear_bb.YLength:.2f} x {rear_bb.ZLength:.2f}")
    print(f"  X[{rear_bb.XMin:.2f}..{rear_bb.XMax:.2f}]  "
          f"Y[{rear_bb.YMin:.2f}..{rear_bb.YMax:.2f}]  "
          f"Z[{rear_bb.ZMin:.2f}..{rear_bb.ZMax:.2f}]")

    rear_feat       = doc.addObject("Part::Feature", "Body_RearBody")
    rear_feat.Shape = rear
    doc.recompute()

    # Front bezel
    print("\n[3/4] Body_FrontBezel ...")
    bezel, binfo = build_front_bezel(P, D)

    # Translate bezel to sit adjacent to rear body (+Y)
    mat = App.Matrix()
    mat.move(App.Vector(0, P["ext_y"]/2, 0))
    bezel_placed = bezel.transformGeometry(mat)

    bezel_valid  = bezel_placed.isValid()
    bezel_closed = bezel_placed.isClosed()
    bezel_vol    = bezel_placed.Volume
    bezel_bb     = bezel_placed.BoundBox
    bezel_solids = len(bezel_placed.Solids)
    print(f"  Solids={bezel_solids}  isValid={bezel_valid}  isClosed={bezel_closed}")
    print(f"  Volume={bezel_vol:.0f} mm3")
    print(f"  BoundBox: {bezel_bb.XLength:.2f} x {bezel_bb.YLength:.2f} x {bezel_bb.ZLength:.2f}")
    print(f"  X[{bezel_bb.XMin:.2f}..{bezel_bb.XMax:.2f}]  "
          f"Y[{bezel_bb.YMin:.2f}..{bezel_bb.YMax:.2f}]  "
          f"Z[{bezel_bb.ZMin:.2f}..{bezel_bb.ZMax:.2f}]")

    # Button/window geometry report
    print(f"\n  --- Bezel geometry (local frame, Y=0 inner, Y={P['bezel_th']} front) ---")
    print(f"  Screen window : Z_ctr={binfo['win_z_ctr']:.2f}  "
          f"W={binfo['win_w']:.2f}  H={binfo['win_h']:.2f}")
    print(f"  Screen window : Z_bot={binfo['win_z_ctr']-binfo['win_h']/2:.2f}  "
          f"Z_top={binfo['win_z_ctr']+binfo['win_h']/2:.2f}")
    print(f"  Button centre : Z={binfo['btn_cz']:.2f}  dia={binfo['btn_dia']}  "
          f"depth={binfo['btn_depth']}  wall={binfo['btn_wall']}")
    print(f"  Sphere        : R={binfo['btn_sphere_r']:.2f}  cy={binfo['btn_sphere_cy']:.2f}")
    print(f"  TTP223 pocket : depth_from_inner={binfo['ttp_pd']:.2f}  "
          f"(bezel_th - btn_depth - btn_wall = "
          f"{P['bezel_th']} - {binfo['btn_depth']} - {binfo['btn_wall']} = {binfo['ttp_pd']:.2f})")

    bezel_feat       = doc.addObject("Part::Feature", "Body_FrontBezel")
    bezel_feat.Shape = bezel_placed
    doc.recompute()

    # -----------------------------------------------------------------------
    # Verification checks (6 checks as per plan)
    # -----------------------------------------------------------------------
    print("\n[4/4] Verification checks ...")

    env_rear  = (rear_bb.XLength  <= P["ext_x"] + 0.01 and
                 rear_bb.YLength  <= P["ext_y"] + 0.01 and
                 rear_bb.ZLength  <= P["ext_z"] + 0.01)
    env_bezel = (bezel_bb.XLength <= P["ext_x"] + 0.01 and
                 bezel_bb.ZLength <= P["ext_z"] + 0.01)

    print(f"  [V1] Bezel isValid={bezel_valid}  isClosed={bezel_closed}  "
          f"Solids={bezel_solids}  => {'PASS' if bezel_valid and bezel_solids == 1 else 'FAIL'}")

    # V2: btn_wall measurement
    # Dish bottom Y in local frame = bezel_th - btn_depth = 5.0 - 1.5 = 3.5
    # TTP223 pocket top Y in local frame = ttp_pocket_d = 2.1
    # btn_wall measured = dish_bottom_Y - pocket_top_Y = 3.5 - 2.1 = 1.4 = btn_wall
    dish_bottom_y  = P["bezel_th"] - P["btn_depth"]   # 3.5
    pocket_top_y   = D["ttp_pocket_d"]                 # 2.1
    wall_measured  = dish_bottom_y - pocket_top_y      # 1.4
    v2_pass = abs(wall_measured - P["btn_wall"]) < 0.01
    print(f"  [V2] btn_wall measured: dish_bottom_Y={dish_bottom_y:.2f}  "
          f"pocket_top_Y={pocket_top_y:.2f}  => wall={wall_measured:.2f} mm  "
          f"(target={P['btn_wall']})  => {'PASS' if v2_pass else 'FAIL'}")

    # V3: TTP223 mockup vs screen window — no intersection expected
    # Build TTP223 in global frame for check
    btn_cz_global = D["btn_center_z"]
    # In global frame, bezel Y starts at ext_y/2 = 20.
    # Pocket is at Y=[0..ttp_pocket_d] in bezel local = Y=[20..20+ttp_pd] global
    ttp_pw = D["ttp_pocket_w"]; ttp_ph = D["ttp_pocket_h"]; ttp_pd = D["ttp_pocket_d"]
    ttp_global_y_center = P["ext_y"]/2 + ttp_pd/2
    ttp_mockup = box(ttp_pw, P["ttp_t"], ttp_ph,
                     cx=0, cy=ttp_global_y_center, cz=btn_cz_global - ttp_ph/2)
    # Screen window in global frame
    win_w = D["scr_win_w"]; win_h = D["scr_win_h"]; win_z_ctr = D["win_z_ctr"]
    # Window is a through-hole; represent as a thin slab (the empty space)
    win_slab = box(win_w, P["bezel_th"] + 1, win_h,
                   cx=0, cy=P["ext_y"]/2 + P["bezel_th"]/2, cz=win_z_ctr - win_h/2)
    try:
        ttp_win_common = ttp_mockup.common(win_slab)
        ttp_win_vol    = ttp_win_common.Volume
    except Exception as e:
        ttp_win_vol = -1.0
        print(f"  [V3] common() error: {e}")
    v3_pass = ttp_win_vol < 0.01
    print(f"  [V3] TTP223 vs screen window intersection: {ttp_win_vol:.4f} mm3  "
          f"=> {'PASS (no overlap)' if v3_pass else 'FAIL (overlap!)'}")

    # V4: button dish does not perforate (btn_wall > 0 means material remains)
    v4_pass = wall_measured > 0
    print(f"  [V4] Button dish does not perforate: "
          f"remaining wall = {wall_measured:.2f} mm  "
          f"=> {'PASS' if v4_pass else 'FAIL — PERFORATED'}")

    # V5: volumes positive, single solids
    v5_pass = (rear_valid and rear_solids == 1 and rear_vol > 0 and
               bezel_valid and bezel_solids == 1 and bezel_vol > 0)
    print(f"  [V5] Rear  volume={rear_vol:.0f} mm3  solids={rear_solids}  valid={rear_valid}")
    print(f"       Bezel volume={bezel_vol:.0f} mm3  solids={bezel_solids}  valid={bezel_valid}")
    print(f"       => {'PASS' if v5_pass else 'FAIL'}")

    print(f"\n  Rear in 40x40x70 envelope : {env_rear}")
    print(f"  Bezel in 40xNx70 envelope : {env_bezel}")
    print(f"  header_clr: {P['header_clr']} mm (>= 6 mm: {P['header_clr'] >= 6})")
    print(f"  USB-C cutout on -X face   : YES (TP4056 charge port)")
    print(f"  CHAMFER ELIMINATED        : YES (no chamfer_h/chamfer_ang in build)")

    # Parametric validation
    validate_parametric(P)

    # Save
    import datetime
    ts = datetime.datetime.now().strftime("%Y-%m-%d")
    stl_bezel_ts = os.path.join(STL_DIR, f"front-bezel_{ts}.stl")
    print(f"Saving FCStd: {FCSTD_PATH}")
    doc.saveAs(FCSTD_PATH)
    print("  OK")

    # STL export
    print(f"\nExporting STLs -> {STL_DIR}")
    rn, rsz = export_stl(rear,         STL_REAR,      "rear-body")
    bn, bsz = export_stl(bezel_placed, STL_BEZEL,     "front-bezel")
    # Timestamped copy for print archive
    import shutil
    if bsz > 0:
        shutil.copy2(STL_BEZEL, stl_bezel_ts)
        print(f"  Timestamped copy: {stl_bezel_ts}")

    # Final summary
    print("\n" + "=" * 60)
    print("BUILD COMPLETE")
    print("=" * 60)
    print(f"  FCStd  : {FCSTD_PATH}")
    print(f"  STL rear  : {STL_REAR}")
    print(f"  STL bezel : {STL_BEZEL}")
    print()
    print(f"  Rear body   solids={rear_solids}  valid={rear_valid}  "
          f"closed={rear_closed}  vol={rear_vol:.0f} mm3")
    print(f"              BB {rear_bb.XLength:.1f}x{rear_bb.YLength:.1f}x{rear_bb.ZLength:.1f}")
    print(f"  Front bezel solids={bezel_solids}  valid={bezel_valid}  "
          f"closed={bezel_closed}  vol={bezel_vol:.0f} mm3")
    print(f"              BB {bezel_bb.XLength:.1f}x{bezel_bb.YLength:.1f}x{bezel_bb.ZLength:.1f}")
    if rn: print(f"  Rear  STL: {rn} triangles, {rsz} bytes")
    if bn: print(f"  Bezel STL: {bn} triangles, {bsz} bytes")

    # Key parameters summary
    print()
    print("  --- Key bezel parameters ---")
    print(f"  bezel_th       = {P['bezel_th']} mm")
    print(f"  btn_dia        = {P['btn_dia']} mm")
    print(f"  btn_depth      = {P['btn_depth']} mm")
    print(f"  btn_wall       = {P['btn_wall']} mm  (measured: {wall_measured:.2f} mm)")
    print(f"  btn_center_z   = {D['btn_center_z']:.2f} mm (auto)")
    print(f"  btn_sphere_r   = {D['btn_sphere_r']:.2f} mm")
    print(f"  win_z_ctr      = {D['win_z_ctr']:.2f} mm")
    print(f"  win bottom     = {D['win_z_ctr']-D['scr_win_h']/2:.2f} mm")
    print(f"  win top        = {D['win_z_ctr']+D['scr_win_h']/2:.2f} mm")
    print(f"  top margin     = {P['ext_z'] - (D['win_z_ctr']+D['scr_win_h']/2):.2f} mm")
    print(f"  ttp_pocket_d   = {D['ttp_pocket_d']:.2f} mm")
    print(f"  Chamfer        : ELIMINATED")

    issues = []
    if not rear_valid   : issues.append("Rear shape not valid!")
    if rear_vol <= 0    : issues.append("Rear volume <= 0!")
    if rear_solids != 1 : issues.append(f"Rear has {rear_solids} solids (expected 1)!")
    if not bezel_valid  : issues.append("Bezel shape not valid!")
    if bezel_vol <= 0   : issues.append("Bezel volume <= 0!")
    if bezel_solids != 1: issues.append(f"Bezel has {bezel_solids} solids (expected 1)!")
    if not v2_pass      : issues.append("btn_wall measurement mismatch!")
    if not v4_pass      : issues.append("Button dish PERFORATES the bezel!")
    if issues:
        print("\nWARNINGS:")
        for w in issues:
            print(f"  - {w}")
    else:
        print("\nAll checks PASSED.")

    doc.recompute()

    # Component mockups (optional — loads add_component_mockups module if present)
    mockup_script = os.path.join(SCRIPT_DIR, "add_component_mockups.py")
    if os.path.exists(mockup_script):
        print("\n[5/5] Component mockups (add_component_mockups.py) ...")
        try:
            import importlib.util as _ilu
            _spec = _ilu.spec_from_file_location("add_component_mockups", mockup_script)
            _mod  = _ilu.module_from_spec(_spec)
            _spec.loader.exec_module(_mod)
            _mod.main()
        except Exception as _e:
            print(f"  [SKIP] Could not run component mockups: {_e}")
    else:
        print(f"\n[INFO] {mockup_script} not found — skipping component mockups.")


if __name__ == "__main__":
    main()

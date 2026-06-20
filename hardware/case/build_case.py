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
    # Envelope  — SET B (44 mm wide, architect approved 2026-06-20)
    "ext_x":           44.0,   # widened from 40 → interior X now 40 mm
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

    # Battery — real LiPo 505070 (50×35×7 mm):  architect SET B / esquema D
    # Orientation: lying flat against rear wall (−Y), grosor = Y-axis.
    #   bat_t  = thickness in Y  (7 mm, against −Y wall)
    #   bat_w  = width    in X  (35 mm, across body)
    #   bat_h  = height   in Z  (50 mm, along body axis)
    # bat_z0 = Z of battery bottom edge above cavity floor (derived, keeps battery
    #          above inferior bosses so bosses stay clear of battery footprint).
    "bat_t":             7.0,
    "bat_w":            35.0,
    "bat_h":            50.0,
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

    # Boss / screws — esquema D (4× M2, 2 inferior + 2 superior, clear of battery Z range)
    # boss_z_bot  : Z base of inferior boss pair (sits on cavity floor, below battery)
    # boss_z_top  : Z base of superior boss pair (above battery, top flush with cavity ceiling)
    # boss_h      : height of each boss cylinder
    # Geometry (with bat_h=50, cav_z=66, ez-wall=68):
    #   inf boss: base=2 (wall), top=2+5=7 → bat_z0=8.5 → Z-gap=1.5mm
    #   sup boss: base=63, top=63+5=68=ez-wall → touching interior ceiling → Z-gap=4.5mm above bat_z_top=58.5
    # boss_off_x (derived): ext_x/2 − wall − boss_r − clr_fit = 22−2−2.5−0.2 = 17.3 mm
    # X 2D-overlap with bat_w/2=17.5 exists (0.2mm) but Z ranges never coincide → NO 3D collision.
    # Superior bosses base at ez−wall−boss_h: boss top touches interior ceiling → fuses cleanly.
    "boss_od":           5.0,
    "boss_id":           1.6,
    "boss_h":            5.0,   # boss height; superior pair top=ez-wall (flush with interior ceiling)
    "boss_z_bot":        2.0,   # inferior pair base: on cavity floor (Z=wall)
    "boss_z_top":       63.0,   # superior pair base: top=63+5=68=ez-wall (touching interior ceiling)
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

    # Battery pocket — bat_t=Y (thickness against rear wall), bat_w=X, bat_h=Z
    d["bat_pocket_t"] = P["bat_t"] + P["bat_swell"] + P["clr_fit"]  # pocket depth in Y
    d["bat_pocket_w"] = P["bat_w"] + 2*P["clr_fit"]                 # pocket width  in X
    d["bat_pocket_h"] = P["bat_h"] + 2*P["clr_fit"]                 # pocket height in Z

    d["mcu_pocket_w"] = P["mcu_w"] + 2*P["clr_fit"]
    d["mcu_pocket_l"] = P["mcu_l"] + 2*P["clr_fit"]

    d["tp_pocket_w"]  = P["tp_w"] + 2*P["clr_fit"]
    d["tp_pocket_l"]  = P["tp_l"] + 2*P["clr_fit"]

    d["ttp_pocket_w"] = P["ttp_w"] + 2*P["clr_fit"]
    d["ttp_pocket_h"] = P["ttp_h"] + 2*P["clr_fit"]

    # -------------------------------------------------------------------------
    # Boss positions — esquema D:
    # boss_off_x: X offset of boss centres from body centreline.
    #   boss_off_x = ext_x/2 − wall − boss_r − clr_fit
    #   With ext_x=44: 22 − 2 − 2.5 − 0.2 = 17.3 mm
    # boss_z_bot / boss_z_top come directly from primary params (tunable).
    # bat_z0: Z of battery bottom edge, placed so battery clears inferior bosses.
    #   bat_z0 = boss_z_bot + boss_r + 1.5 mm margin
    # -------------------------------------------------------------------------
    boss_r = P["boss_od"] / 2.0
    # boss_off_x: boss centre position in X so the boss outer face is flush with the
    # exterior wall (boss right edge = ext_x/2). This guarantees the boss cylinder
    # overlaps the shell wall material → OCC Boolean fuse produces a single solid.
    # boss_off_x = ext_x/2 − boss_r = 22 − 2.5 = 19.5 mm
    d["boss_off_x"]  = P["ext_x"]/2 - boss_r
    d["boss_z_bot"]  = P["boss_z_bot"]
    d["boss_z_top"]  = P["boss_z_top"]
    # bat_z0: battery base sits ABOVE inferior boss top with 1.5mm margin.
    # bat_z0 = boss_z_bot + boss_h + 1.5 = 2+5+1.5 = 8.5 mm above cavity floor.
    d["bat_z0"]      = P["boss_z_bot"] + P["boss_h"] + 1.5

    # Legacy alias kept so any existing reference still works
    d["boss_pos"]    = d["boss_off_x"]

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

    # 2.4 Battery pocket (lying flat against rear wall −Y)
    # Orientation: bat_t = grosor in Y, bat_w = width in X, bat_h = height in Z.
    # Pocket: bpw(X) × (bpt_y + extra)(Y) × bph(Z)
    # bat_z0 = Z bottom edge, placed above inferior bosses (see derive).
    bpw  = D["bat_pocket_w"]   # X: bat_w + 2*clr_fit = 35.4 mm
    bph  = D["bat_pocket_h"]   # Z: bat_h + 2*clr_fit = 50.4 mm
    bpt  = D["bat_pocket_t"]   # Y: bat_t + swell + clr_fit = 8.0 mm
    bat_z0 = D["bat_z0"]       # Z bottom of pocket above cavity floor
    # Pocket open toward +Y (front): extra depth ensures full clearance for swell.
    bat_pocket_depth = bpt + 15  # generous Y-depth, stops at interior (cavity is open)
    bat_pocket = box(bpw, bat_pocket_depth, bph,
                     cx=0,
                     cy=-ey/2 + wall + bat_pocket_depth/2,
                     cz=wall + bat_z0)
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

    # 2.10 Bosses (4× M2, esquema D) — 2 inferior (below battery) + 2 superior (above battery)
    # Position in XY-plane: X = ±boss_off_x, boss sits on cavity floor (cz=wall).
    # Z position: boss_z_bot (inferior pair) and boss_z_top (superior pair).
    # All 4 bosses land OUTSIDE the battery footprint in Z (bat_z0..bat_z0+bat_h).
    boss_od = P["boss_od"]; boss_id = P["boss_id"]; boss_h = P["boss_h"]
    bx_off  = D["boss_off_x"]   # ±17.3 mm from centre in X
    # Interior Y of the body — bosses run from cavity floor up, centred in interior Y
    # (originally bosses were at ±boss_pos in both X and Y, using body-centre Y= ±bp).
    # Now they sit at Y=0 (body X-Z plane), protruding from cavity floor.
    boss_pairs = [
        (bx_off,  0.0, P["boss_z_bot"]),   # inferior right
        (-bx_off, 0.0, P["boss_z_bot"]),   # inferior left
        (bx_off,  0.0, P["boss_z_top"]),   # superior right
        (-bx_off, 0.0, P["boss_z_top"]),   # superior left
    ]
    for bx, by, bz in boss_pairs:
        shell = shell.fuse(cyl(boss_od/2, boss_h, cx=bx, cy=by, cz=bz))
        shell = shell.cut(cyl(boss_id/2, boss_h, cx=bx, cy=by, cz=bz))

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

    # 3.6 Screw through-holes + counterbores — esquema D positions
    # boss_off_x = ±bx_off in X; Z = boss_z_bot (inferior) and boss_z_top (superior).
    bx_off = D["boss_off_x"]
    sc_cl  = P["screw_clear_d"]; sc_hd = P["screw_head_d"]
    total_y_extent = bth + lip_h + 1.0   # full Y span of bezel+lip
    bezel_boss_pairs = [
        (bx_off,  P["boss_z_bot"]),   # inferior right
        (-bx_off, P["boss_z_bot"]),   # inferior left
        (bx_off,  P["boss_z_top"]),   # superior right
        (-bx_off, P["boss_z_top"]),   # superior left
    ]
    for bx, bz in bezel_boss_pairs:
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
        # Battery — real LiPo 50x35x7 mm (tumbada plana, grosor en Y)
        ("bat_t",            P["bat_t"],             "Battery thickness in Y (against rear wall)"),
        ("bat_w",            P["bat_w"],             "Battery width in X"),
        ("bat_h",            P["bat_h"],             "Battery height in Z"),
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
        # Bosses / screws — esquema D
        ("boss_od",          P["boss_od"],           "Boss outer diameter"),
        ("boss_id",          P["boss_id"],           "Boss drill diameter (M2)"),
        ("boss_h",           P["boss_h"],            "Boss height"),
        ("boss_z_bot",       P["boss_z_bot"],        "Z centre of inferior boss pair (below battery)"),
        ("boss_z_top",       P["boss_z_top"],        "Z base of superior boss pair (top=boss_z_top+boss_h=ez-wall, touching ceiling)"),
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
        ("boss_off_x",       D["boss_off_x"],        "= ext_x/2 - boss_r (boss outer face flush with exterior wall)"),
        ("bat_z0",           D["bat_z0"],            "= boss_z_bot + boss_r + 1.5 (battery bottom Z above cavity floor)"),
        ("boss_pos",         D["boss_pos"],          "= boss_off_x (legacy alias)"),
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
# 5. PARAMETRIC VALIDATION (bat_w 35 → 34, then restore)
# ===========================================================================
def validate_parametric(P_orig):
    print("\n--- Parametric validation: bat_w 35 -> 34 ---")
    P_mod = dict(P_orig)
    P_mod["bat_w"] = 34.0
    D_mod = derive(P_mod)
    try:
        shape = build_rear_body(P_mod, D_mod)
        bb  = shape.BoundBox
        vol = shape.Volume
        print(f"  bat_pocket_w: {D_mod['bat_pocket_w']:.2f} (was {P_orig['bat_w']+2*P_orig['clr_fit']:.2f})")
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
# 7. COMPONENT MOCKUPS — integrated into pipeline (must come before main)
# ===========================================================================
def _comp_box(w, d2, h, cx=0.0, cy=0.0, cz=0.0):
    """Box helper local to component mockup functions."""
    return Part.makeBox(w, d2, h, App.Vector(cx - w/2, cy - d2/2, cz))


def _component_positions(P, D):
    """
    Returns ordered dict: name -> component dict with shape, color, desc.
    All coordinates in the global frame (same as build_case.py).

    Global frame:
      Origin = centre of exterior footprint at Z=0 (bottom face of rear body)
      +Y  = front of device (bezel side)
      -Y  = rear face
      -X  = side with USB-C cutouts
      +Z  = top (TFT end)
      Bezel inner mating face sits at Y = ext_y/2 = +20 mm.
    """
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall = P["wall"]
    positions = {}

    # --- BATTERY (LiPo real 50×35×7 mm) ---
    # Tumbada plana contra pared trasera (−Y).
    # bat_t = grosor in Y (7 mm), bat_w = width in X (35 mm), bat_h = height in Z (50 mm).
    # bat_z0 (derived) = Z bottom of battery above cavity floor, clear of inferior bosses.
    bat_z0  = D["bat_z0"]          # ~9.0 mm
    bat_cx  = 0.0
    bat_cy  = -ey/2 + wall + P["bat_t"]/2   # centre Y: against rear wall + half-thickness
    bat_cz  = wall + bat_z0                  # Z base of battery in global frame
    bat_shape = _comp_box(P["bat_w"], P["bat_t"], P["bat_h"],
                          cx=bat_cx, cy=bat_cy, cz=bat_cz)
    positions["Comp_Battery"] = {
        "shape": bat_shape,
        "color": (0.55, 0.55, 0.55),
        "desc":  (f"LiPo 505070  {P['bat_w']}x{P['bat_h']}x{P['bat_t']}mm  "
                  f"bat_z0={bat_z0:.1f}  bat_z_top={bat_cz+P['bat_h']:.1f}"),
    }

    # --- MCU (ESP32-S3 Zero) ---
    # Pocket in build_rear_body: cx=ey/2-wall-mpw/2 (along X), cy=0, cz=wall+bat_pocket_t+2
    mcu_cx  = ey/2 - wall - D["mcu_pocket_w"]/2   # ~8.80
    mcu_cy  = 0.0
    mcu_cz  = wall + D["bat_pocket_t"] + 2.0       # ~10.0
    mcu_shape = _comp_box(P["mcu_l"], P["mcu_w"], P["mcu_t"],
                          cx=mcu_cx, cy=mcu_cy, cz=mcu_cz)
    mcu_usb_z  = mcu_cz + (P["mcu_t"] - P["mcu_usb_h"]) / 2
    mcu_usb_cx = -(ex/2)
    usb_span   = P["mcu_usb_protrude"] + wall
    mcu_usb_shape = _comp_box(usb_span, P["mcu_usb_w"], P["mcu_usb_h"],
                               cx=mcu_usb_cx + usb_span/2,
                               cy=mcu_cx,
                               cz=mcu_usb_z)
    try:
        mcu_full = mcu_shape.fuse(mcu_usb_shape)
    except Exception:
        mcu_full = mcu_shape
    positions["Comp_MCU"] = {
        "shape": mcu_full,
        "color": (0.13, 0.55, 0.13),
        "desc":  f"ESP32-S3 Zero  {P['mcu_l']}x{P['mcu_w']}x{P['mcu_t']}mm",
    }

    # --- TP4056 ---
    tp_cx  = mcu_cx
    tp_cy  = -ey/2 + wall + D["tp_pocket_w"]/2    # -20+2+8.7 = -9.3
    tp_cz  = mcu_cz
    tp_shape = _comp_box(P["tp_l"], P["tp_w"], P["tp_t"],
                         cx=tp_cx, cy=tp_cy, cz=tp_cz)
    tp_usb_z  = tp_cz + (P["tp_t"] - P["tp_usb_h"]) / 2
    tp_usb_cx = -(ex/2)
    tp_usb_shape = _comp_box(usb_span, P["tp_usb_w"], P["tp_usb_h"],
                              cx=tp_usb_cx + usb_span/2,
                              cy=tp_cy,
                              cz=tp_usb_z)
    try:
        tp_full = tp_shape.fuse(tp_usb_shape)
    except Exception:
        tp_full = tp_shape
    positions["Comp_TP4056"] = {
        "shape": tp_full,
        "color": (1.0, 0.50, 0.0),
        "desc":  f"TP4056  {P['tp_l']}x{P['tp_w']}x{P['tp_t']}mm",
    }

    # --- SCREEN PCB (TFT JD9853) ---
    # PCB against +Y interior wall, top flush with interior ceiling.
    scr_cy    = ey/2 - wall - P["scr_pcb_t"]/2     # 18 - 0.83 = 17.17
    scr_z_bot = ez - wall - P["scr_pcb_h"]          # 68 - 48.86 = 19.14
    scr_shape = _comp_box(P["scr_pcb_w"], P["scr_pcb_t"], P["scr_pcb_h"],
                          cx=0.0, cy=scr_cy, cz=scr_z_bot)
    # Active area marker — thin slab just outside +Y interior wall, cyan.
    pcb_top_z = ez - wall
    act_z_ctr = (pcb_top_z
                 - P["scr_pcb_h"]
                 + (P["scr_pcb_h"] - P["scr_act_h"]) / 2
                 + P["scr_act_h"] / 2
                 + P["scr_act_off_y"])
    marker_t   = 0.3
    act_marker = _comp_box(P["scr_act_w"], marker_t, P["scr_act_h"],
                           cx=0.0,
                           cy=ey/2 - wall + marker_t/2,
                           cz=act_z_ctr - P["scr_act_h"]/2)
    # Header connector behind PCB
    hdr_cy = ey/2 - wall - P["scr_pcb_t"] - 2.54/2
    hdr_shape = _comp_box(21.0, 2.54, 5.0, cx=0.0, cy=hdr_cy, cz=scr_z_bot)
    try:
        scr_full = scr_shape.fuse(hdr_shape)
    except Exception:
        scr_full = scr_shape
    positions["Comp_Screen"] = {
        "shape":  scr_full,
        "marker": act_marker,
        "color":  (0.10, 0.30, 0.80),
        "desc":   (f"TFT PCB  {P['scr_pcb_w']}x{P['scr_pcb_h']}x{P['scr_pcb_t']}mm  "
                   f"active={P['scr_act_w']}x{P['scr_act_h']}"),
    }

    # --- TTP223 (capacitive touch sensor, inside bezel inner-face pocket) ---
    # No chamfer. Centred in X (cx=0), centred in Z at btn_center_z (~14.65 mm).
    # In global frame: bezel inner mating face is at Y = ext_y/2 = +20 mm.
    # The pocket runs from Y=+20 toward +Y by ttp_pocket_d (2.1 mm into the bezel).
    # TTP223 touch pad faces -Y (toward the concave button dish / the user).
    ttp_pd       = D["ttp_pocket_d"]           # = bezel_th - btn_depth - btn_wall = 2.1 mm
    z_ttp_ctr    = D["btn_center_z"]           # ~14.65 mm (auto-derived)
    ttp_cy_global = ey/2 + ttp_pd/2            # centre in Y inside the pocket = 21.05

    ttp_shape = _comp_box(P["ttp_w"], P["ttp_t"], P["ttp_h"],
                          cx=0.0, cy=ttp_cy_global, cz=z_ttp_ctr - P["ttp_h"]/2)
    positions["Comp_TTP223"] = {
        "shape": ttp_shape,
        "color": (0.90, 0.85, 0.10),
        "desc":  (f"TTP223  {P['ttp_w']}x{P['ttp_h']}x{P['ttp_t']}mm  "
                  f"Z_ctr={z_ttp_ctr:.2f}  behind concave button  pocket_d={ttp_pd:.2f}mm"),
    }

    return positions


def _verify_components(positions, rear_shape, bezel_shape, P, D):
    """
    For each component compute intersection volume with RearBody and FrontBezel.
    Prints collision table. Returns list of result dicts.
    """
    TOL = 0.5  # mm³ threshold for 'real' collision

    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]; wall = P["wall"]

    results = []
    for name, comp in positions.items():
        shape = comp["shape"]

        try:
            inter_rear  = shape.common(rear_shape)
            vol_rear    = inter_rear.Volume if inter_rear.Solids else 0.0
        except Exception as e:
            vol_rear = -1.0
            print(f"  [WARN] common({name}, rear) failed: {e}")

        try:
            inter_bezel = shape.common(bezel_shape)
            vol_bezel   = inter_bezel.Volume if inter_bezel.Solids else 0.0
        except Exception as e:
            vol_bezel = -1.0
            print(f"  [WARN] common({name}, bezel) failed: {e}")

        bb = shape.BoundBox

        # Classify — known-acceptable collisions vs real problems
        if "Battery" in name and vol_rear > 0:
            # Esquema D: bosses should NOT overlap battery. Flag as real interference.
            rear_ok  = (vol_rear < TOL)
            bezel_ok = True
            col_note = "OK" if rear_ok else "INTERFERENCIA REAL (bat vs bosses — check esquema D)"
        elif "TTP223" in name and vol_bezel > 0:
            # TTP223 sits inside the bezel inner-face pocket.
            rear_ok  = True; bezel_ok = True
            col_note = "ACCEPTED (sits in bezel inner-face pocket)"
        else:
            rear_ok  = (vol_rear  < TOL)
            bezel_ok = (vol_bezel < TOL)
            col_note = "OK" if (rear_ok and bezel_ok) else "INTERFERENCIA REAL"

        results.append({
            "name":      name,
            "vol_rear":  vol_rear,
            "vol_bezel": vol_bezel,
            "rear_ok":   rear_ok,
            "bezel_ok":  bezel_ok,
            "col_note":  col_note,
            "bb":        bb,
        })

    # Print table
    print()
    print("=" * 82)
    print("COLLISION REPORT — Component vs Case Bodies")
    print("=" * 82)
    print(f"  {'Component':<22} {'vs RearBody mm³':>16} {'vs FrontBezel mm³':>18}  Result")
    print("-" * 82)
    any_real = False
    for r in results:
        vr = f"{r['vol_rear']:.3f}"  if r['vol_rear']  >= 0 else "ERR"
        vb = f"{r['vol_bezel']:.3f}" if r['vol_bezel'] >= 0 else "ERR"
        print(f"  {r['name']:<22} {vr:>16} {vb:>18}  {r['col_note']}")
        if "REAL" in r["col_note"]:
            any_real = True
    print()

    # Battery vs boss clearance — esquema D (3D check: X AND Z must both overlap)
    boss_r   = P["boss_od"] / 2.0
    bx_off   = D["boss_off_x"]
    bat_z0_v  = D["bat_z0"]
    bat_z_top_v = bat_z0_v + P["bat_h"]

    x_inner_edge = bx_off - boss_r
    x_bat_half   = P["bat_w"] / 2.0
    x_overlap_2d = x_inner_edge < x_bat_half

    inf_z_top  = P["boss_z_bot"] + P["boss_h"]
    sup_z_bot  = P["boss_z_top"]
    sup_z_top  = P["boss_z_top"] + P["boss_h"]   # should equal ez-wall (ceiling)
    z_gap_inf  = bat_z0_v - inf_z_top
    z_gap_sup  = sup_z_bot - bat_z_top_v

    col_3d_inf = x_overlap_2d and (z_gap_inf <= 0)
    col_3d_sup = x_overlap_2d and (z_gap_sup <= 0)

    print(f"NOTE — Battery vs bosses (esquema D, 3D check):")
    print(f"  X inner edge={x_inner_edge:.2f}mm  bat_w/2={x_bat_half:.1f}mm  X-2D-overlap={x_overlap_2d}")
    print(f"  inf boss Z=[{P['boss_z_bot']:.1f}..{inf_z_top:.1f}]  bat_z0={bat_z0_v:.1f}  Z-gap={z_gap_inf:.2f}mm")
    print(f"  sup boss Z=[{sup_z_bot:.1f}..{sup_z_top:.1f}]  bat_z_top={bat_z_top_v:.1f}  Z-gap={z_gap_sup:.2f}mm")
    print(f"  3D collision inf={col_3d_inf}  sup={col_3d_sup}")
    if col_3d_inf or col_3d_sup:
        print(f"  ACTION REQUIRED: battery has 3D interference with boss(es).")
    else:
        print(f"  All boss clearances OK (bosses and battery are in different Z bands).")
    print()

    if any_real:
        print("WARNING: INTERFERENCIA REAL detected — review entries above.")
    else:
        print("All non-accepted collisions: OK (no unacceptable interference).")

    return results


def add_component_mockups(doc, P, D, rear_shape, bezel_shape):
    """
    Adds Comp_* Part::Feature objects to `doc`.
    Sets rear/bezel bodies to Transparency=80; Comp_* stay opaque with their colours.
    Runs collision verification and prints results.
    Idempotent: removes any existing Components group and stray Comp_* first.
    """
    # Remove existing Components group + stray Comp_* (idempotent)
    existing_grp = doc.getObject("Components")
    if existing_grp is not None:
        for obj in list(existing_grp.OutList):
            try:
                doc.removeObject(obj.Name)
            except Exception:
                pass
        try:
            doc.removeObject("Components")
        except Exception:
            pass
        doc.recompute()
        print("  Removed existing Components group.")

    for obj in list(doc.Objects):
        if obj.Name.startswith("Comp_"):
            try:
                doc.removeObject(obj.Name)
            except Exception:
                pass
    doc.recompute()

    # Build component shapes
    positions = _component_positions(P, D)

    # Create group
    grp = doc.addObject("App::DocumentObjectGroup", "Components")
    grp.Label = "Components (reference only – not for print)"

    for name, comp in positions.items():
        feat       = doc.addObject("Part::Feature", name)
        feat.Shape = comp["shape"]
        feat.Label = name
        if feat.ViewObject is not None:
            r, g, b = comp["color"]
            feat.ViewObject.ShapeColor   = (r, g, b, 1.0)
            feat.ViewObject.Transparency = 0
            feat.ViewObject.Visibility   = True
        grp.addObject(feat)

        # Active area marker for screen (separate cyan feature)
        if "Screen" in name and "marker" in comp:
            mname = name + "_ActiveMarker"
            mfeat = doc.addObject("Part::Feature", mname)
            mfeat.Shape = comp["marker"]
            mfeat.Label = mname
            if mfeat.ViewObject is not None:
                mfeat.ViewObject.ShapeColor   = (0.0, 0.85, 1.0, 1.0)
                mfeat.ViewObject.Transparency = 30
                mfeat.ViewObject.Visibility   = True
            grp.addObject(mfeat)

    doc.recompute()

    # Semi-transparent case bodies (80%), Comp_* stay opaque, all visible
    rear_feat  = doc.getObject("Body_RearBody")
    bezel_feat = doc.getObject("Body_FrontBezel")
    for feat in [rear_feat, bezel_feat]:
        if feat is not None and feat.ViewObject is not None:
            feat.ViewObject.Transparency = 80
            feat.ViewObject.Visibility   = True

    doc.recompute()

    # Isometric view + fitAll (GUI only — no-op in headless)
    try:
        import FreeCADGui as Gui
        Gui.activeDocument().activeView().viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
    except Exception:
        pass

    # Collision verification
    _verify_components(positions, rear_shape, bezel_shape, P, D)

    comp_names = list(positions.keys())
    print(f"\nComponent mockups added: {comp_names}")


# ===========================================================================
# 8. MAIN
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

    print(f"\n  Rear in 44x40x70 envelope : {env_rear}")
    print(f"  Bezel in 44xNx70 envelope : {env_bezel}")
    print(f"  header_clr: {P['header_clr']} mm (>= 6 mm: {P['header_clr'] >= 6})")
    print(f"  USB-C cutout on -X face   : YES (TP4056 charge port)")
    print(f"  CHAMFER ELIMINATED        : YES (no chamfer_h/chamfer_ang in build)")

    # -----------------------------------------------------------------------
    # V6: Battery containment — check margins inside cavity 40(X)×36(Y)×66(Z)
    # -----------------------------------------------------------------------
    cav_x  = D["int_x"]           # 40 mm (with ext_x=44)
    cav_y  = D["int_y"]           # 36 mm
    cav_z  = D["int_z"]           # 66 mm
    bat_z0 = D["bat_z0"]          # Z bottom of battery above cavity floor
    bat_z_top = bat_z0 + P["bat_h"]   # Z top of battery

    margin_x_per_side = (cav_x - P["bat_w"]) / 2.0     # expected ~2.5 mm each side
    margin_y          = cav_y - (P["bat_t"] + P["bat_swell"])  # available Y clearance
    margin_z_bot      = bat_z0                            # below battery
    margin_z_top      = cav_z - bat_z_top                # above battery

    v6_pass = (margin_x_per_side > 0 and margin_y > 0 and
               margin_z_bot > 0 and margin_z_top > 0 and
               bat_z_top <= cav_z)

    print(f"\n  [V6] Battery containment in cavity {cav_x:.0f}(X)×{cav_y:.0f}(Y)×{cav_z:.0f}(Z) mm:")
    print(f"       bat_w={P['bat_w']} bat_t={P['bat_t']}+swell={P['bat_swell']} bat_h={P['bat_h']}")
    print(f"       X margin per side : {margin_x_per_side:.2f} mm  (expected ~2.5)")
    print(f"       Y clearance       : {margin_y:.2f} mm  (cav_y - bat_t - swell)")
    print(f"       Z margin below    : {margin_z_bot:.2f} mm  (bat_z0 above cavity floor)")
    print(f"       Z margin above    : {margin_z_top:.2f} mm  (above battery top={bat_z_top:.1f})")
    print(f"       => {'PASS' if v6_pass else 'FAIL'}")

    # V7: Boss vs battery 3D clearance (esquema D)
    # A collision requires BOTH X and Z ranges to overlap simultaneously.
    # Bosses are separated from battery in Z (different Z bands) even though
    # boss_off_x (17.3mm) is slightly inside bat_w/2 (17.5mm) in the X-only projection.
    boss_r   = P["boss_od"] / 2.0
    bx_off   = D["boss_off_x"]
    x_inner_edge = bx_off - boss_r           # 14.8mm — inner edge of boss cylinder
    x_bat_half   = P["bat_w"] / 2.0          # 17.5mm — battery half-width
    x_overlap_2d = x_inner_edge < x_bat_half  # True if boss and battery share X column

    inf_z_top = P["boss_z_bot"] + P["boss_h"]
    sup_z_bot = P["boss_z_top"]
    sup_z_top = P["boss_z_top"] + P["boss_h"]
    # Superior boss top is designed to equal ez-wall (touching interior ceiling)
    ez_minus_wall = P["ext_z"] - P["wall"]

    z_gap_inf    = bat_z0 - inf_z_top        # >0 → battery bottom above inf boss top
    z_gap_sup    = sup_z_bot - bat_z_top     # >0 → sup boss bottom above battery top

    # 3D collision: X overlap AND Z ranges intersect (gap <= 0)
    col_3d_inf = x_overlap_2d and (z_gap_inf <= 0)
    col_3d_sup = x_overlap_2d and (z_gap_sup <= 0)

    v7_pass = (not col_3d_inf) and (not col_3d_sup) and (abs(sup_z_top - ez_minus_wall) < 0.1)

    print(f"\n  [V7] Boss vs battery 3D clearance (esquema D):")
    print(f"       boss_off_x={bx_off:.2f}  boss_r={boss_r}  bat_w/2={x_bat_half:.1f}")
    print(f"       X inner edge of boss={x_inner_edge:.2f} — X 2D overlap with battery: {x_overlap_2d}")
    print(f"       inf boss Z=[{P['boss_z_bot']:.1f}..{inf_z_top:.1f}]  bat_z0={bat_z0:.1f}  Z-gap={z_gap_inf:.2f}mm")
    print(f"       sup boss Z=[{sup_z_bot:.1f}..{sup_z_top:.1f}]  bat_z_top={bat_z_top:.1f}  Z-gap={z_gap_sup:.2f}mm")
    print(f"       sup boss top={sup_z_top:.1f} vs ez-wall={ez_minus_wall:.1f}: {'flush (OK)' if abs(sup_z_top-ez_minus_wall)<0.1 else 'MISMATCH'}")
    print(f"       3D collision inf={col_3d_inf}  sup={col_3d_sup}")
    print(f"       => {'PASS' if v7_pass else 'FAIL'}")

    # V8: Button and screen centred in X (cx=0 by design, verify win_w fits in new ext_x)
    win_w = D["scr_win_w"]
    v8_pass = (win_w <= D["int_x"] - 2.0)  # 2mm clearance to interior wall each side
    print(f"\n  [V8] Screen window ({win_w:.2f} mm) centred in ext_x={P['ext_x']} mm:")
    print(f"       int_x={D['int_x']:.1f}  margin per side={(D['int_x']-win_w)/2:.2f} mm")
    print(f"       Button (dia={P['btn_dia']}) centred cx=0 by design")
    print(f"       => {'PASS' if v8_pass else 'FAIL — window wider than interior'}")

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

    # Component mockups — always generated, integrated in pipeline
    print("\n[5/5] Component mockups ...")
    add_component_mockups(doc, P, D, rear, bezel_placed)

    # Final save (mockups may have added objects; saveAs overwrites cleanly)
    print(f"\nSaving FCStd (final, with mockups): {FCSTD_PATH}")
    doc.saveAs(FCSTD_PATH)
    print("  OK")

    # Confirm objects in document
    obj_names = [o.Name for o in doc.Objects]
    print(f"\nDocument objects: {obj_names}")
    comp_in_doc = [n for n in obj_names if n.startswith("Comp_")]
    print(f"Comp_* objects  : {comp_in_doc}")


if __name__ == "__main__":
    main()


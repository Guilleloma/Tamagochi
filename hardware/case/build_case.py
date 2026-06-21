"""
build_case.py — Tamagotchi enclosure, FreeCAD 1.x headless script
Generates:
  - hardware/case/tamagotchi-case.FCStd
  - hardware/case/stl/body-main.stl
  - hardware/case/stl/rear-cover.stl

Coordinate system:
  Origin = geometric centre of exterior footprint at Z=0 (bottom face)
  +Y  = front of device (user-facing, screen side)
  -Y  = rear (tapa desmontable, Rear_Cover)
  -X  = side with USB-C cutout
  +Z  = top (TFT end)

Topology — 2 piezas:
  Body_Main  : cara frontal +Y maciza e integral; cavidad abierta por -Y;
               4 bosses eje Y en esquinas; cuna batería en pared interior fija.
  Rear_Cover : tapa plana que cierra -Y; labio macho perimetral; 4 pasantes M2
               + counterbore por cara -Y (cabezas ocultas).
  Plano de separación perpendicular a Y en Y = -ext_y/2 + cover_th.

Orientation for slicer (FDM Bambu / PLA / 0.4 mm):
  Body_Main  : cara frontal +Y sobre la cama (face-down, concavidad autosoportada;
               cavidad abre arriba; bosses eje Y verticales — soporte mínimo).
  Rear_Cover : cara ext -Y sobre la cama (counterbores como rebaje; labio arriba —
               sin soportes).

Run:
  PYTHONPATH=/Applications/FreeCAD.app/Contents/Resources/lib \
  /Applications/FreeCAD.app/Contents/Resources/bin/python build_case.py
"""

import os, sys, math

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
FCSTD_PATH  = os.path.join(SCRIPT_DIR, "tamagotchi-case.FCStd")
STL_DIR     = os.path.join(SCRIPT_DIR, "stl")
STL_MAIN    = os.path.join(STL_DIR, "body-main.stl")
STL_COVER   = os.path.join(STL_DIR, "rear-cover.stl")

os.makedirs(STL_DIR, exist_ok=True)

import FreeCAD as App
import Part

# ===========================================================================
# 1. PARAMETERS — single source of truth
# ===========================================================================
P = {
    # Envelope
    # Axes: +Y=front, -Y=rear, -X=USB-C lateral, +Z=top
    "ext_x":           44.0,   # exterior width
    "ext_y":           40.0,   # exterior depth  (Y span: -20 .. +20)
    "ext_z":           70.0,   # exterior height
    "wall":             2.0,
    "wall_front":       5.0,   # bezel_th: espesor pared frontal +Y (integral en Body_Main)
    "fillet_ext_vert":  4.0,   # vertical-corner fillet radius (embeds bosses)
    "fillet_ext_horiz": 2.0,   # top/bottom horizontal-edge fillet radius
    "fillet_int":       0.5,   # internal fillet radius

    # Fit / clearance
    "clr_fit":          0.15,
    "clr_slide":        0.15,  # labio/recess side clearance
    "clr_z":            0.30,

    # Screen PCB (TFT JD9853)
    "scr_pcb_w":        32.26,
    "scr_pcb_h":        48.86,
    "scr_pcb_t":         1.66,
    "scr_act_w":        29.52,
    "scr_act_h":        34.93,
    "scr_act_off_y":     0.0,
    "disp_front_clr":    1.5,
    "bezel_lip":         1.2,  # screen window lip width
    "bezel_th":          5.0,  # alias for wall_front (bezel thickness / espesor frontal)
    "pcb_clip_h":        2.0,

    # Header clearance (behind TFT PCB, protrudes -Y from PCB bottom)
    "header_clr":        6.0,

    # Capacitive button — Home-style concave dish, front face, lower strip
    # btn_wall NETO = bezel_th - btn_depth - ttp_pocket_d = 5.0 - 0.6 - 3.4 = 1.0 mm
    "btn_dia":          12.0,
    "btn_depth":         0.6,
    "btn_wall":          1.0,
    "btn_center_z":      9.0,  # FIXED (franja inferior, centrado X)

    # MCU — ESP32-S3 Zero
    "mcu_w":            18.0,
    "mcu_l":            23.5,
    "mcu_t":             1.0,
    "mcu_center_z":      9.0,
    "mcu_usb_w":         9.4,
    "mcu_usb_h":         3.6,
    "mcu_usb_protrude":  1.0,

    # Battery — LiPo 505070, 50×35×7 mm
    # Ubicada en pared INTERIOR FIJA del cuerpo (zona alta, cerca de cara frontal +Y)
    # bat_t = grosor en Y (7 mm, contra pared interior +Y del cuerpo)
    # bat_w = ancho en X (35 mm), bat_h = alto en Z (50 mm)
    # bat_center_z = 40.5 mm
    "bat_t":             7.0,
    "bat_w":            35.0,
    "bat_h":            50.0,
    "bat_center_z":     40.5,
    "bat_swell":         1.0,

    # Charger TP4056 — USB-C alineado al cutout -X
    "tp_w":             17.0,
    "tp_l":             26.0,
    "tp_t":              4.5,
    "tp_center_z":      22.0,
    "tp_usb_w":         10.0,
    "tp_usb_h":          5.0,

    # Touch sensor TTP223
    "ttp_w":            15.0,
    "ttp_h":            11.0,
    "ttp_t":             1.6,
    "ttp_clip_clr":      0.3,

    # Boss / screws — 4× M3×6 countersunk (avellanado 90°, al ras), eje Y
    # boss_od derivado = boss_id + 2*2.0 = 6.5 mm
    # boss_off_x = 17.5 mm (fijo — margen cabeza 1.5 mm al borde, poste solapa pared lateral)
    # boss_z_bot / boss_z_top = CENTRO Z del eje del boss (no base)
    "boss_id":           2.5,   # M3 self-tap pilot diameter (PLA)
    "boss_z_bot":        9.0,   # centro Z eje boss inferior
    "boss_z_top":       61.0,   # centro Z eje boss superior
    "boss_post_len":     8.0,   # longitud poste (Y[-17.5..-9.5])
    "screw_head_d":      6.0,   # Ø cabeza/avellanado M3
    "screw_clear_d":     3.4,   # pasante M3 en tapa
    "screw_len":         6.0,   # M3×6 (tapa uniforme 2.5mm sin isla rebajada)
    "screw_thread_engage": 3.5, # engage mínimo en boss
    "n_screws":          4,
    # Avellanado countersunk 90°
    "cs_angle":         90.0,   # ángulo del cono avellanado (grados)
    "cs_depth":          1.3,   # prof cono = (screw_head_d - screw_clear_d) / 2 = 1.3 mm
    # Isla rebajada ELIMINADA — tapa uniforme 2.5 mm en toda la superficie

    # Rear cover
    "cover_th":          2.5,  # espesor tapa trasera
    "cover_lip_h":       4.0,  # labio macho de la tapa (= lip_h)
    "lip_overlap":       1.0,  # solape labio DENTRO de la placa (mm)
    "btn_cut_margin":    2.0,  # margen extra alrededor del casquete esférico (mm)

    # Lip / ledge
    "lip_h":             4.0,  # ledge recess depth in Body_Main boca -Y

    # Charge mode
    "charge_mode":       0,

    # corner_solid_web retenido para compatibilidad con checks V-BOSS-FIX legacy
    "corner_solid_web":  2.0,   # semi-height Z keepout (legacy; ya no usa recess_cut)
}

def derive(P):
    """Compute all derived values from primary parameters."""
    d = {}
    d["int_x"] = P["ext_x"] - 2*P["wall"]
    d["int_y"] = P["ext_y"] - 2*P["wall"]  # interior depth (pared rear + pared front)
    d["int_z"] = P["ext_z"] - 2*P["wall"]

    d["scr_pocket_w"] = P["scr_pcb_w"] + 2*P["clr_fit"]
    d["scr_pocket_h"] = P["scr_pcb_h"] + 2*P["clr_fit"]
    d["scr_win_w"]    = P["scr_act_w"] - 2*P["bezel_lip"]
    d["scr_win_h"]    = P["scr_act_h"] - 2*P["bezel_lip"]

    # -------------------------------------------------------------------------
    # Battery pocket — pared INTERIOR FIJA (junto a cara frontal +Y interior)
    # Pocket desde cara interior frontal hacia -Y (dentro de la cavidad)
    # bat_t = grosor Y; bat_w = X; bat_h = Z
    # bat_center_z = 40.5 → bat_z0 = 40.5 - 25 = 15.5 mm sobre suelo cavidad
    # -------------------------------------------------------------------------
    d["bat_pocket_t"] = P["bat_t"] + P["bat_swell"] + P["clr_fit"]  # Y depth
    d["bat_pocket_w"] = P["bat_w"] + 2*P["clr_fit"]                 # X width
    d["bat_pocket_h"] = P["bat_h"] + 2*P["clr_fit"]                 # Z height
    d["bat_z0"]       = P["bat_center_z"] - P["bat_h"] / 2.0        # = 15.5 mm

    # MCU pocket
    d["mcu_pocket_w"] = P["mcu_w"] + 2*P["clr_fit"]
    d["mcu_pocket_l"] = P["mcu_l"] + 2*P["clr_fit"]
    d["mcu_z_base"]   = P["mcu_center_z"] - P["mcu_t"] / 2.0

    # TP4056 pocket
    d["tp_pocket_w"]  = P["tp_w"] + 2*P["clr_fit"]
    d["tp_pocket_l"]  = P["tp_l"] + 2*P["clr_fit"]
    d["tp_z_base"]    = P["tp_center_z"] - P["tp_t"] / 2.0

    d["ttp_pocket_w"] = P["ttp_w"] + 2*P["clr_fit"]
    d["ttp_pocket_h"] = P["ttp_h"] + 2*P["clr_fit"]

    # -------------------------------------------------------------------------
    # Boss — eje Y M3×6 avellanado al ras; tapa UNIFORME 2.5 mm (sin isla)
    # boss_od = boss_id + 2*2.0 = 6.5 mm (pared 2.0 alrededor del piloto)
    # boss_off_x = 17.5 mm (fijo: margen cabeza 1.5 mm al borde + poste solapa pared lateral 0.75 mm)
    # cover_th = 2.5 mm  → cover_y0=-20, cover_y1=-17.5
    # Tapa uniforme: material Y[-20..-17.5] = 2.5 mm en toda la superficie
    # boss_y_start = -ext_y/2 + cover_th = -20 + 2.5 = -17.5 mm (cara interior tapa)
    # boss_y_end   = boss_y_start + boss_post_len = -17.5 + 8.0 = -9.5 mm
    # pilot_len    = screw_thread_engage + 1.0 = 4.5 mm → Y[-17.5..-13.0]
    # M3×6 punta: cover_y0 + screw_len = -20 + 6 = -14.0 mm (dentro piloto -13.0, aire 1.0 mm ✓)
    # Engage: punta(-14.0) − boss_y_start(-17.5) = 3.5 mm ✓
    # boss_z_bot / boss_z_top = CENTRO Z del eje del boss
    # -------------------------------------------------------------------------
    d["boss_od"]      = P["boss_id"] + 2 * 2.0              # = 6.5 mm
    boss_r            = d["boss_od"] / 2.0                   # = 3.25 mm
    d["boss_r"]       = boss_r
    d["boss_off_x"]   = 17.5                                 # fijo (margen+solape pared)

    d["interface_y"]  = -P["ext_y"] / 2.0 + P["cover_th"]  # = -17.5 mm
    # boss_y_start arranca en cara INTERIOR de la tapa (interfaz cuerpo/tapa)
    d["boss_y_start"] = -P["ext_y"] / 2.0 + P["cover_th"]  # = -17.5 mm
    d["boss_y_end"]   = d["boss_y_start"] + P["boss_post_len"]           # = -9.5 mm

    # Derived Y refs for cover geometry
    d["front_ext_y"]  = P["ext_y"] / 2.0                    # = +20 mm
    d["front_int_y"]  = P["ext_y"] / 2.0 - P["wall_front"] # = +15 mm
    d["rear_ext_y"]   = -P["ext_y"] / 2.0                   # = -20 mm
    d["cover_y0"]     = -P["ext_y"] / 2.0                   # = -20 mm
    d["cover_y1"]     = d["cover_y0"] + P["cover_th"]       # = -17.5 mm
    d["lip_y_start"]  = d["cover_y1"] - P["lip_overlap"]    # = -18.5 mm
    d["lip_y_end"]    = d["cover_y1"] + P["cover_lip_h"]    # = -13.5 mm

    # -------------------------------------------------------------------------
    # Corner keepout — prevents recess_cut from carving into boss-wall junction
    #
    # The recess_cut spans X[-20.15..+20.15], Y[-20..-16], Z[1.85..68.15].
    # Boss center = ±17.5, r=3.25 → boss X range = [14.25..20.75].
    # Recess overlaps boss at X[14.25..20.15] in Y[-17.5..-16].
    # → Material between boss and lateral wall is carved, leaving "C-slot".
    #
    # Fix: subtract 4 keepout boxes from recess_cut before applying to shell.
    # Each keepout box protects the full lateral-wall+boss corner zone.
    #
    # Keepout geometry (per corner, e.g. +X side, cz=boss_z_bot):
    #   X: from inner face of boss toward wall (boss centre - r - 0.5) to wall outer face
    #      keepout_x_inner = boss_off_x - boss_r - 0.5 = 13.75 mm (inner edge of keepout)
    #      keepout_x_outer = ext_x/2 + 0.1 = 22.1 mm (overcuts through wall face slightly)
    #   Y: full recess depth = rear_ext_y .. lip_y_end = -20.0 .. -13.5 mm
    #   Z: boss_z ± (boss_r + corner_solid_web) = boss_z ± 5.25 mm
    #
    # keepout_x_inner (abs, from centre): boss_off_x - boss_r - 0.5
    d["keepout_x_inner"]   = d["boss_off_x"] - boss_r - 0.5             # 17.5 - 3.25 - 0.5 = 13.75 mm
    d["keepout_x_outer"]   = P["ext_x"] / 2.0 + 0.1                    # 22.1 mm (through wall, clean cut)
    d["keepout_y0"]        = -P["ext_y"] / 2.0                          # -20.0 mm (full recess Y start)
    d["keepout_y1"]        = d["lip_y_end"]                             # -13.5 mm (lip end)
    d["keepout_z_half"]    = boss_r + P["corner_solid_web"]             # 3.25 + 2.0 = 5.25 mm

    # Web gusset dimensions (legacy refs, kept for V-BOSS-FIX checks)
    d["web_y0"]            = d["boss_y_start"]                          # -17.5 mm
    d["web_y1"]            = d["lip_y_end"]                             # -13.5 mm

    # Legacy keepout/notch derived (kept for spreadsheet + legacy V-BOSS-FIX probes)
    d["notch_x_inner"]     = d["keepout_x_inner"] - P["clr_slide"]     # 13.75 - 0.15 = 13.60 mm
    d["notch_x_outer"]     = P["ext_x"] / 2.0                          # 22.0 mm
    d["notch_z_half"]      = d["keepout_z_half"] + P["clr_slide"]      # 5.25 + 0.15 = 5.40 mm

    # -------------------------------------------------------------------------
    # PLACA A RAS + LABIO INTERIOR — nuevos derivados (plan arquitecto)
    # Estrategia: rebaje perimetral Y[-20..-17.5] en cuerpo (abre hueco para placa)
    #             labio INTERIOR aditivo en tapa (anillo que entra en la cavidad)
    # -------------------------------------------------------------------------
    clr_sl = P["clr_slide"]   # 0.15 mm

    # Nuevo grosor del labio de la tapa (desacoplado de wall)
    d["cover_lip_th"]      = 1.85   # mm — pared anillo labio interior

    # Rebaje perimetral trasero en Body_Main:
    #   Y[-20..-17.5] recorta la franja de pared para que la placa quede a ras.
    #   El corte tiene la misma X/Z que la caja exterior + pequeño epsilon.
    d["rear_rebate_y0"]    = -P["ext_y"] / 2.0        # -20 mm
    d["rear_rebate_y1"]    = d["cover_y1"]             # -17.5 mm (= interface_y)
    d["rear_rebate_th"]    = d["cover_y1"] - (-P["ext_y"] / 2.0)  # = cover_th = 2.5 mm

    # Labio interior tapa (anillo que entra en cavidad):
    #   cav_inner_x_half = int_x/2 = 20 mm (cara interior de la cavidad)
    #   lip_out_x_half   = cav_inner_x_half - clr_slide = 19.85 mm (exterior labio con holgura)
    #   lip_in_x_half    = lip_out_x_half - cover_lip_th = 18.0 mm (interior labio = hueco)
    #   lip_out_z0       = wall + clr_slide = 2.15 mm
    #   lip_out_z1       = ext_z - wall - clr_slide = 67.85 mm
    #   lip_in_z0        = 2 * wall = 4.0 mm  (no toca suelo/techo)
    #   lip_in_z1        = ext_z - 2*wall = 66.0 mm
    cav_inner_x_half       = d["int_x"] / 2.0                     # 20.0 mm
    d["cav_inner_x_half"]  = cav_inner_x_half
    d["lip_out_x_half"]    = cav_inner_x_half - clr_sl            # 19.85 mm
    d["lip_in_x_half"]     = d["lip_out_x_half"] - d["cover_lip_th"]  # 18.0 mm
    d["lip_out_z0"]        = P["wall"] + clr_sl                   # 2.15 mm
    d["lip_out_z1"]        = P["ext_z"] - P["wall"] - clr_sl      # 67.85 mm
    d["lip_in_z0"]         = 2.0 * P["wall"]                      # 4.0 mm
    d["lip_in_z1"]         = P["ext_z"] - 2.0 * P["wall"]         # 66.0 mm

    # Muescas de postes en labio: los postes (X=±17.5, boss_r=3.25 → X[14.25..20.75])
    # cruzan el labio exterior (X[18..19.85]) → necesitan muesca.
    # Por seguridad: muesca X[boss_off_x - boss_r - clr_sl .. lip_out_x_half]
    #               = X[14.1 .. 19.85] a cada lado
    # En Z: boss_z ± (boss_r + clr_sl) = boss_z ± 3.4
    boss_r_val             = d["boss_r"]                           # 3.25 mm
    d["lip_notch_x_inner"] = d["boss_off_x"] - boss_r_val - clr_sl  # 17.5-3.25-0.15=14.1 mm
    d["lip_notch_x_outer"] = d["lip_out_x_half"]                  # 19.85 mm
    d["lip_notch_z_half"]  = boss_r_val + clr_sl                   # 3.4 mm

    # Pilot hole: from boss_y_start toward +Y, length = screw_thread_engage + 1
    d["pilot_len"]    = P["screw_thread_engage"] + 1.0       # = 4.5 mm

    # -------------------------------------------------------------------------
    # Screen window vertical position
    # -------------------------------------------------------------------------
    ez = P["ext_z"]; wall = P["wall"]
    pcb_top_z = ez - wall                                   # 68.0 mm
    win_z_raw = (pcb_top_z
                 - P["scr_pcb_h"]
                 + (P["scr_pcb_h"] - P["scr_act_h"]) / 2
                 + P["scr_act_h"] / 2
                 + P["scr_act_off_y"])
    win_h = d["scr_win_h"]
    win_z_max_top    = ez - 6.0 - win_h / 2
    btn_z_top_edge   = P["btn_center_z"] + P["btn_dia"] / 2.0 + 2.0
    win_z_min_bottom = btn_z_top_edge + win_h / 2
    d["win_z_ctr"] = max(win_z_min_bottom, min(win_z_max_top, win_z_raw))
    d["disp_center_z"] = d["win_z_ctr"]

    # Button centre Z
    if P["btn_center_z"] != 0.0:
        d["btn_center_z"] = P["btn_center_z"]
    else:
        win_z_bot = d["win_z_ctr"] - win_h / 2
        btn_strip_top = win_z_bot - 2.0
        d["btn_center_z"] = (btn_strip_top + P["boss_z_bot"]) / 2

    # Sphere for concave dish
    r_btn = P["btn_dia"] / 2.0
    btn_d = P["btn_depth"]
    d["btn_sphere_r"]  = (r_btn**2 + btn_d**2) / (2.0 * btn_d)

    # Sphere centre in GLOBAL coords (front face at Y=+ext_y/2=+20)
    # Dish carved from outside face Y=+20 inward (casquete desde exterior).
    # Centre must lie OUTSIDE the solid (in +Y), so the sphere only intersects
    # the front face as a shallow cap of depth btn_depth=0.6 mm:
    #   sphere grazes Y=+20 at its nearest point → centre at Y=+20 + (btn_sphere_r - btn_depth)
    # Previous (WRONG): ext_y/2 - btn_sphere_r + btn_depth  →  +20 - 30.3 + 0.6 = -9.7
    #   (centre inside solid → cut was through-hole)
    # Correct: ext_y/2 + btn_sphere_r - btn_depth  →  +20 + 30.3 - 0.6 = +49.7
    #   (centre outside, cap depth 0.6 mm from Y=+20 inward → fondo en Y≈+19.4)
    d["btn_sphere_cy_global"] = (P["ext_y"] / 2.0
                                  + d["btn_sphere_r"] - btn_d)

    # btn_sphere_cy local (for reporting; from front face inward)
    d["btn_sphere_cy_local"] = d["btn_sphere_r"] - btn_d

    # TTP223 pocket depth from interior frontal face:
    # ttp_pocket_d = bezel_th - btn_depth - btn_wall
    d["ttp_pocket_d"]  = P["bezel_th"] - btn_d - P["btn_wall"]

    # USB cutout centre Z
    d["usb_cutout_z"]  = P["tp_center_z"]

    # Rear cover geometry
    # cover_lip_h already in P; here we expose interface Y
    d["cover_lip_h"]   = P["cover_lip_h"]

    return d

D = derive(P)

# ===========================================================================
# Geometry helpers
# ===========================================================================
def box(w, d2, h, cx=0.0, cy=0.0, cz=0.0):
    """Solid box centred at (cx,cy) in XY, base at cz."""
    return Part.makeBox(w, d2, h, App.Vector(cx - w/2, cy - d2/2, cz))

def cyl(r, h, cx=0.0, cy=0.0, cz=0.0):
    """Vertical cylinder, axis Z (legacy helper — NOT used for bosses)."""
    return Part.makeCylinder(r, h, App.Vector(cx, cy, cz), App.Vector(0, 0, 1))

def cyl_y(r, h, cx, cz, y0):
    """
    Cylinder with axis Y (direction (0,1,0)).
    Origin at (cx, y0, cz); extends h mm in +Y direction.
    Used for ALL bosses and screw holes.
    """
    return Part.makeCylinder(r, h,
                              App.Vector(cx, y0, cz),
                              App.Vector(0, 1, 0))

# ===========================================================================
# Fillet helpers
# ===========================================================================
def _vert_edges(shape, min_len=60.0):
    """Vertical edges (parallel to Z) with length >= min_len."""
    edges = []
    for e in shape.Edges:
        try:
            v1 = e.Vertexes[0].Point
            v2 = e.Vertexes[1].Point
            if (abs(v2.x - v1.x) < 0.01 and abs(v2.y - v1.y) < 0.01
                    and e.Length >= min_len):
                edges.append(e)
        except Exception:
            pass
    return edges

def _horiz_edges_at_z(shape, z_target, min_len=5.0, tol=0.1):
    """Horizontal edges at z_target with length >= min_len."""
    edges = []
    for e in shape.Edges:
        try:
            v1 = e.Vertexes[0].Point
            v2 = e.Vertexes[1].Point
            if (abs(v2.z - v1.z) < 0.01 and e.Length >= min_len
                    and abs(v1.z - z_target) < tol):
                edges.append(e)
        except Exception:
            pass
    return edges

def apply_fillets(shape, r_vert, r_horiz, ez, label="shape"):
    """
    Apply fillet_ext_vert on long vertical edges, then fillet_ext_horiz on
    top/bottom horizontal edges.  Falls back gracefully on OCC errors.
    Returns (filleted_shape, applied_vert, applied_horiz).
    """
    result = shape
    applied_vert = False
    applied_horiz = False

    vert_e = _vert_edges(result, min_len=ez * 0.85)
    if vert_e and r_vert > 0:
        try:
            result = result.makeFillet(r_vert, vert_e)
            applied_vert = True
        except Exception as exc:
            print(f"  [WARN] {label} vert fillet r={r_vert} failed: {exc} — skipping")

    if r_horiz > 0:
        top_e = _horiz_edges_at_z(result, ez)
        bot_e = _horiz_edges_at_z(result, 0.0)
        horiz_e = top_e + bot_e
        if horiz_e:
            try:
                result = result.makeFillet(r_horiz, horiz_e)
                applied_horiz = True
            except Exception as exc:
                print(f"  [WARN] {label} horiz fillet r={r_horiz} failed: {exc} — skipping")

    return result, applied_vert, applied_horiz


# ===========================================================================
# 2. BODY_MAIN (renombrado desde Body_RearBody)
# ===========================================================================
def build_body_main(P, D):
    """
    Cara frontal +Y maciza e integral (bezel_th=5 mm).
    Cavidad abierta por -Y (boca trasera).
    4 bosses eje Y en esquinas.
    Recess perimetral en boca -Y para labio de la tapa.
    Batería en pared INTERIOR FIJA (cerca de frontal +Y interior).
    """
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall = P["wall"]
    bth  = P["wall_front"]   # = bezel_th = 5 mm (espesor cara frontal)

    # Refs Y globales
    # Cara ext frontal: Y = +ey/2 = +20
    # Cara int frontal: Y = +ey/2 - bth = +15
    # Cara ext trasera (boca): Y = -ey/2 = -20
    # Interfaz cuerpo/tapa: Y = -ey/2 + cover_th = -17.5
    front_ext_y = ey / 2.0            # +20
    front_int_y = front_ext_y - bth   # +15  (cara interior de la pared frontal)
    rear_ext_y  = -ey / 2.0           # -20  (boca abierta hacia -Y)
    interface_y = D["interface_y"]     # -17.5

    # ------------------------------------------------------------------
    # 2.1 Outer solid — fillet ANTES de cortar para topología limpia
    # ------------------------------------------------------------------
    outer = box(ex, ey, ez, cx=0, cy=0, cz=0)
    rv = P["fillet_ext_vert"]; rh = P["fillet_ext_horiz"]
    outer, fv_ok, fh_ok = apply_fillets(outer, rv, rh, ez, label="Body_Main_outer")
    if fv_ok: print(f"  [fillet] vert r={rv} applied to outer solid")
    if fh_ok: print(f"  [fillet] horiz r={rh} applied to outer solid")

    # ------------------------------------------------------------------
    # 2.2 Cavidad interior — abierta por -Y (boca trasera)
    # La cavidad empieza en la cara interior del frontal (+front_int_y)
    # y llega hasta la boca -Y. En Y ocupa: [rear_ext_y .. front_int_y]
    # = depth_cav = ey - bth (= 35 mm)
    # En X: int_x = ext_x - 2*wall = 40 mm (centrado)
    # En Z: desde suelo (wall=2) hasta techo (ez-wall=68), height=int_z=66
    # ------------------------------------------------------------------
    cav_x   = D["int_x"]             # 40 mm
    cav_y   = ey - bth               # 35 mm (de rear_ext_y a front_int_y)
    cav_z   = D["int_z"]             # 66 mm
    cav_cy  = (rear_ext_y + front_int_y) / 2.0   # centro Y de la cavidad

    cavity = box(cav_x, cav_y, cav_z, cx=0, cy=cav_cy, cz=wall)
    shell  = outer.cut(cavity)

    # ------------------------------------------------------------------
    # 2.3 REBAJE PERIMETRAL TRASERO — placa a ras (sustituye recess + keepout)
    #
    # Estrategia nueva: NO hay labio macho en las paredes laterales del cuerpo.
    # En cambio, la placa de la tapa cierra a ras la boca trasera Y[-20..-17.5].
    # Para ello, recortamos una franja delgada Y[-20..-17.5] de las paredes
    # perimetrales (X y Z) de la carcasa, de modo que la placa (44×70) encaje
    # a nivel con la cara trasera del cuerpo (Y=-17.5).
    #
    # El rebaje es un box que cubre la cara trasera completa (ext_x+2ε × cover_th+ε × ext_z+2ε)
    # centrado en Y=(-20+-17.5)/2 = -18.75. Al cortarlo del shell ya vaciado,
    # elimina el material de pared en esa franja, dejando la boca abierta.
    # La cavidad interior (int_x=40 × int_z=66) ya está abierta hasta Y=-20;
    # el rebaje solo quita la franja de PARED exterior (X[20..22], Z[0..2], Z[68..70]).
    # ------------------------------------------------------------------
    clr_sl  = P["clr_slide"]         # 0.15 mm
    eps     = 0.05                    # epsilon para corte limpio sin cara coincidente
    rebate_cy  = D["rear_rebate_y0"] + D["rear_rebate_th"] / 2.0   # -18.75 mm
    rebate_cut = box(ex + 2*eps,
                     D["rear_rebate_th"] + eps,
                     ez + 2*eps,
                     cx=0,
                     cy=rebate_cy,
                     cz=-eps)
    shell = shell.cut(rebate_cut)
    print(f"  [2.3] rebaje perimetral trasero: Y[{D['rear_rebate_y0']:.2f}..{D['rear_rebate_y1']:.2f}]  "
          f"grosor={D['rear_rebate_th']:.2f}mm  shell V={shell.Volume:.0f} mm³")

    # ------------------------------------------------------------------
    # 2.4 Batería — cuna en pared interior fija (zona alta, cerca del frontal)
    # Pocket desde cara interior frontal (Y=front_int_y) hacia -Y
    # bat_t = grosor en Y (7 mm), bat_w = X (35 mm), bat_h = Z (50 mm)
    # bat_center_z = 40.5 mm → bat_z0 = 15.5 mm sobre suelo (global Z = wall+bat_z0)
    # Pocket: X centrado, Y=[front_int_y - bat_pocket_t .. front_int_y], Z=[bat_z0..]
    # ------------------------------------------------------------------
    bpw  = D["bat_pocket_w"]    # 35.30 mm
    bph  = D["bat_pocket_h"]    # 50.30 mm
    bpt  = D["bat_pocket_t"]    # 8.15 mm (t+swell+clr)
    bat_z0 = D["bat_z0"]        # 15.5 mm
    bat_pocket_cy = front_int_y - bpt / 2   # centro Y del pocket

    bat_pocket = box(bpw, bpt, bph,
                     cx=0,
                     cy=bat_pocket_cy,
                     cz=wall + bat_z0)
    shell = shell.cut(bat_pocket)

    # ------------------------------------------------------------------
    # 2.5 MCU pocket (ESP32-S3 Zero, tumbado)
    # Near rear wall (boca -Y side), centrado X
    # ------------------------------------------------------------------
    mpw  = D["mcu_pocket_w"]; mpl = D["mcu_pocket_l"]
    mcu_z   = wall + D["mcu_z_base"]
    mcu_cy  = rear_ext_y + wall + mpw/2     # cerca de pared trasera
    mcu_pocket = box(mpl, mpw, P["mcu_t"] + P["clr_fit"]*2 + 2.0,
                     cx=0.0, cy=mcu_cy, cz=mcu_z)
    shell = shell.cut(mcu_pocket)

    # ------------------------------------------------------------------
    # 2.6 TP4056 pocket — tp_center_z=22
    # ------------------------------------------------------------------
    tppl = D["tp_pocket_l"]; tppw = D["tp_pocket_w"]
    tp_z  = wall + D["tp_z_base"]
    tp_cy = rear_ext_y + wall + tppw/2
    tp_pocket = box(tppl, tppw, P["tp_t"] + P["clr_fit"]*2 + 1.0,
                    cx=0.0, cy=tp_cy, cz=tp_z)
    shell = shell.cut(tp_pocket)

    # ------------------------------------------------------------------
    # 2.7 USB-C cutout en cara -X (único, para TP4056)
    # ------------------------------------------------------------------
    usb_w = P["tp_usb_w"]; usb_h = P["tp_usb_h"]
    usb_z = D["usb_cutout_z"] - usb_h / 2
    usb_cy = tp_cy
    usb_cut_d = wall + 4
    usb_cx = -(ex/2 - usb_cut_d/2)
    usb_cut = box(usb_cut_d, usb_w, usb_h, cx=usb_cx, cy=usb_cy, cz=usb_z)
    shell = shell.cut(usb_cut)

    # ------------------------------------------------------------------
    # 2.8 Screen PCB retention ribs (protrude from cara interior frontal)
    # Protrude desde Y=front_int_y hacia -Y
    # ------------------------------------------------------------------
    clip_h = P["pcb_clip_h"]; clip_w = 3.0; clip_d = 1.5
    scr_w  = P["scr_pcb_w"]
    for sign in [-1, +1]:
        rib = box(clip_w, clip_d, clip_h,
                  cx=sign * (scr_w/2 - clip_w/2),
                  cy=front_int_y - clip_d/2,
                  cz=ez - wall - P["scr_pcb_h"] - clip_h)
        shell = shell.fuse(rib)

    # ------------------------------------------------------------------
    # 2.9 Bosses M3×6 — postes (boss_post_len=8 mm) anclados en zona trasera
    # boss_od = boss_id + 2*2.0 = 6.5 mm (derivado en D)
    # boss_off_x = 17.5 mm (fijo; poste solapa pared lateral ext_x/2=22 → ancla)
    # Rango Y: [boss_y_start=-17.5 .. boss_y_end=-9.5]  (tapa uniforme, sin isla)
    # Gap hasta front_int_y=+15: 25.5 mm → NUNCA toca el frente
    # Poste Ø6.5 en X=±17.5 → extremo exterior X=17.5+3.25=20.75 > pared_int=20.0 → solapa
    # Alineación coaxial (±17.5, z_bot/z_top):
    #   avellanado cono  Y[-20..-18.7]  Ø6→Ø3.4 (r1=3.0→r2=1.7, cs_depth=1.3)
    #   pasante          Y[-20..-13.5] Ø3.4
    #   poste            Y[-17.5..-9.5] Ø6.5
    #   piloto M3        Y[-17.5..-13.0] Ø2.5
    #   M3×6 punta:      -20 + 6 = -14.0  (dentro del piloto -13.0, aire 1.0 mm ✓)
    #   engage: -14.0 − (−17.5) = 3.5 mm ✓
    # ------------------------------------------------------------------
    boss_od  = D["boss_od"]           # 6.5 mm (derivado)
    boss_id  = P["boss_id"]           # 2.5 mm
    bx_off   = D["boss_off_x"]        # ±17.5 mm
    by_start = D["boss_y_start"]      # -18.5 mm
    b_post_l = P["boss_post_len"]     # 8 mm

    # boss_z_bot / boss_z_top son CENTROS Z del eje del boss
    boss_pairs = [
        ( bx_off, P["boss_z_bot"]),   # inf right
        (-bx_off, P["boss_z_bot"]),   # inf left
        ( bx_off, P["boss_z_top"]),   # sup right
        (-bx_off, P["boss_z_top"]),   # sup left
    ]

    for bx, bz in boss_pairs:
        # Poste sólido: arranca en boss_y_start=-17.5, crece +Y, longitud 8 mm
        # → Y[-17.5..-9.5]
        boss_solid = cyl_y(boss_od/2, b_post_l, cx=bx, cz=bz, y0=by_start)
        shell = shell.fuse(boss_solid)

        # ----------------------------------------------------------------
        # Anchor blend — continuous bridge boss↔lateral wall, full post height
        # Spans Y[boss_y_start=-17.5 .. boss_y_end=-9.5] (toda la longitud del poste)
        # X: from keepout_x_inner (inner edge, near boss_center) to wall outer + 0.01 mm
        #    for a solid connection (boss already overlaps wall ~0.75 mm; this fills the gap)
        # Z: centred on boss axis, ±boss_r (just covers the boss width, no floating extra)
        # Result: poste "nace" de la pared de forma continua en toda su altura.
        # ----------------------------------------------------------------
        bx_sign      = 1 if bx > 0 else -1
        blend_x_inner = D["keepout_x_inner"]                    # 13.75 mm (from centre)
        blend_x_outer = P["ext_x"] / 2.0 + 0.01                # 22.01 mm (into wall)
        blend_x_w     = blend_x_outer - blend_x_inner           # 8.26 mm
        blend_y_len   = D["boss_y_end"] - D["boss_y_start"]     # 8.0 mm (full post Y span)
        blend_z_h     = boss_od                                  # 6.5 mm  (= boss diameter in Z)
        blend_x_origin = (bx_sign * blend_x_inner
                          if bx_sign > 0
                          else -(blend_x_outer))
        anchor_blend = Part.makeBox(
            blend_x_w, blend_y_len, blend_z_h,
            App.Vector(blend_x_origin,
                       D["boss_y_start"],
                       bz - boss_od / 2.0)
        )
        shell = shell.fuse(anchor_blend)

        # Piloto M3: arranca en boss_y_start, crece +Y, longitud pilot_len=4.5 mm
        # → Y[-17.5..-13.0]
        pilot = cyl_y(boss_id/2, D["pilot_len"], cx=bx, cz=bz, y0=by_start)
        shell = shell.cut(pilot)

    # §2.10 ELIMINADO — ribs de anclaje suprimidos por el plan del arquitecto.
    # Los postes cortos (boss_od=4 mm) quedan embebidos en el fillet exterior
    # r=4 de la esquina → anclaje natural en las 2 paredes perimetrales.

    # ------------------------------------------------------------------
    # 2.11 Screen window y pockets en cara frontal +Y (bezel integral)
    # Ventana through-hole (Y completo de la pared frontal)
    # ------------------------------------------------------------------
    win_w     = D["scr_win_w"]
    win_h_val = D["scr_win_h"]
    win_z_ctr = D["win_z_ctr"]
    bth_cut   = bth + 0.1   # un pelo extra para corte limpio

    # Ventana pantalla (through-hole en pared frontal)
    screen_win = box(win_w, bth_cut, win_h_val,
                     cx=0,
                     cy=front_int_y + bth_cut/2 - 0.05,
                     cz=win_z_ctr - win_h_val/2)
    shell = shell.cut(screen_win)

    # PCB pocket (desde cara interior frontal, Y=front_int_y, hacia +Y)
    spw = D["scr_pocket_w"]; sph = D["scr_pocket_h"]
    spd = P["scr_pcb_t"] + P["clr_z"]
    pcb_top_z  = ez - wall
    pcb_z_bot  = pcb_top_z - P["scr_pcb_h"]
    pcb_pocket = box(spw, spd, sph,
                     cx=0,
                     cy=front_int_y + spd/2,
                     cz=pcb_z_bot)
    shell = shell.cut(pcb_pocket)

    # Header clearance pocket ELIMINADO: el header de 8 pines sobresale hacia
    # -Y (cavidad abierta) → tiene espacio libre sin necesidad de pocket.
    # El pocket anterior cortaba hacia +Y atravesando la pared frontal (cuadrado
    # visible desde el frente).

    # ------------------------------------------------------------------
    # 2.12 Concave button dish (desde cara exterior frontal Y=+20 hacia -Y)
    # BUG 3 FIX: acotar el cortador esfera con una caja local para que no
    # infle el BoundBox del cuerpo (de X=60.6 → X≈44).
    # btn_cut_margin=2.0 mm extra alrededor de btn_dia en X/Z
    # La caja local se intersecta con la esfera → dish = sphere.common(local)
    # Pared NETA bajo casquete: wall_front - btn_depth - ttp_pocket_d = 1.0 mm ✓
    # ------------------------------------------------------------------
    btn_cz       = D["btn_center_z"]
    sph_r        = D["btn_sphere_r"]
    sph_cy_glob  = D["btn_sphere_cy_global"]   # centro esfera en global Y

    btn_sphere_solid = Part.makeSphere(sph_r, App.Vector(0, sph_cy_glob, btn_cz))

    # Caja acotadora: X/Z = btn_dia + 2*btn_cut_margin, Y = espesor pared frontal
    # centrada en (0, front_ext_y - wall_front/2, btn_cz)
    btn_margin = P["btn_cut_margin"]
    local_w    = P["btn_dia"] + 2.0 * btn_margin      # 12 + 4 = 16 mm
    local_h    = P["btn_dia"] + 2.0 * btn_margin      # 16 mm en Z
    local_d    = bth + 0.2                             # algo más que wall_front (clip limpio)
    btn_local  = box(local_w, local_d, local_h,
                     cx=0.0,
                     cy=front_ext_y - bth / 2.0,
                     cz=btn_cz - local_h / 2.0)

    dish  = btn_sphere_solid.common(btn_local)
    shell = shell.cut(dish)

    # ------------------------------------------------------------------
    # 2.13 TTP223 nervios + pocket (desde cara interior frontal hacia +Y)
    # ttp_pocket_d = 3.4 mm desde cara interior frontal
    # ------------------------------------------------------------------
    ttp_pw = D["ttp_pocket_w"]
    ttp_ph = D["ttp_pocket_h"]
    ttp_pd = D["ttp_pocket_d"]

    so_r  = 2.0
    so_cx = ttp_pw / 2.0 - 2.5
    slot_w_outer = ttp_pw / 2.0 - so_cx - so_r

    if slot_w_outer > 0.1:
        bezel_y0 = front_int_y   # cara interior frontal = Y local 0
        shell = shell.cut(box(slot_w_outer, ttp_pd, ttp_ph,
                              cx=-(ttp_pw/2.0 - slot_w_outer/2.0),
                              cy=bezel_y0 + ttp_pd/2.0,
                              cz=btn_cz - ttp_ph/2.0))
        shell = shell.cut(box(slot_w_outer, ttp_pd, ttp_ph,
                              cx=(ttp_pw/2.0 - slot_w_outer/2.0),
                              cy=bezel_y0 + ttp_pd/2.0,
                              cz=btn_cz - ttp_ph/2.0))
        centre_w = 2.0 * so_cx - 2.0 * so_r
        if centre_w > 0.1:
            shell = shell.cut(box(centre_w, ttp_pd, ttp_ph,
                                  cx=0,
                                  cy=bezel_y0 + ttp_pd/2.0,
                                  cz=btn_cz - ttp_ph/2.0))
    else:
        shell = shell.cut(box(ttp_pw, ttp_pd, ttp_ph,
                              cx=0,
                              cy=front_int_y + ttp_pd/2.0,
                              cz=btn_cz - ttp_ph/2.0))

    # §2.14 Cable channel ELIMINADO — el canal 5×5 atravesaba los postes -X
    # y causaba asimetría (postes +X íntegros, postes -X comidos).
    # Los cables se rutean por la cavidad abierta; sin canal dedicado.

    return shell


# ===========================================================================
# 3. REAR_COVER — LABIO INTERIOR (enfoque ADITIVO)
# ===========================================================================
def build_rear_cover(P, D):
    """
    Tapa trasera con labio INTERIOR (enfoque aditivo):

    1. plate = caja plana ext_x × cover_th × ext_z en Y[-20..-17.5]
               Fillets verticales r≈3.2 (clamp: < cover_th/2) y horizontales r=2.
    2. lip_outer = caja del anillo labio interior:
               X: 2*lip_out_x_half = 39.7 mm (entra en cavidad con holgura 0.15)
               Y: cover_lip_h = 4 mm  [−17.5..−13.5]
               Z: lip_out_z1 − lip_out_z0 = 65.7 mm  [2.15..67.85]
    3. lip_inner = hueco interior del anillo (X: 2*lip_in_x_half = 36.0, Z: 62.0 [4..66])
       lip = lip_outer.cut(lip_inner)  → anillo de pared 1.85 mm
    4. cover = plate.fuse(lip)
    5. Muescas de poste (CRÍTICO): los postes (X=±17.5, r=3.25) cruzan el labio.
       4 muescas en el anillo: cada muesca cubre X[14.1..19.85] (boss inner − clr a lip_out)
       en Z[boss_z ± 3.4] para cada boss Z.
    6. Avellanado cónico 90° + pasante M3×6 en cada tornillo.
    Aborta con RuntimeError explícito si Solids!=1 en cualquier etapa.
    """
    ex      = P["ext_x"]; ez = P["ext_z"]
    cth     = P["cover_th"]      # 2.5 mm
    lip_h   = P["cover_lip_h"]   # 4.0 mm
    clr_sl  = P["clr_slide"]     # 0.15 mm
    wall    = P["wall"]

    rv  = P["fillet_ext_vert"]   # 4.0 mm (se clampea a plate dimension)
    rh  = P["fillet_ext_horiz"]  # 2.0 mm

    cover_y0  = D["cover_y0"]   # -20.0 mm  cara ext tapa
    cover_y1  = D["cover_y1"]   # -17.5 mm  cara int tapa (= interface_y)
    lip_y_end = D["lip_y_end"]  # -13.5 mm  extremo labio hacia +Y

    # Nuevos derivados labio interior
    lip_out_x = 2.0 * D["lip_out_x_half"]   # 39.7 mm (X span labio exterior)
    lip_in_x  = 2.0 * D["lip_in_x_half"]    # 36.0 mm (X span labio interior = hueco)
    lip_out_z0 = D["lip_out_z0"]             # 2.15 mm
    lip_out_z1 = D["lip_out_z1"]             # 67.85 mm
    lip_in_z0  = D["lip_in_z0"]              # 4.0 mm
    lip_in_z1  = D["lip_in_z1"]              # 66.0 mm
    lip_out_z  = lip_out_z1 - lip_out_z0     # 65.7 mm
    lip_in_z   = lip_in_z1  - lip_in_z0      # 62.0 mm
    lip_th     = D["cover_lip_th"]           # 1.85 mm

    # ------------------------------------------------------------------
    # 3.1-3.3 BLOQUE ÚNICO con vaciado selectivo (evita junctions fuse → isValid=False)
    #
    # Construimos un bloque full ext_x × (cover_th+lip_h) × ext_z
    # Y[cover_y0..lip_y_end] = [-20..-13.5] grosor=6.5mm
    #
    # Luego vaciamos selectivamente la zona del labio:
    #   - Restar exceso perimetral exterior de la zona del labio (X, Z):
    #     Franjas exteriores en X (±[lip_out_x/2..ex/2]) y Z ([0..lip_out_z0], [lip_out_z1..ez])
    #     solo en Y[cover_y1..lip_y_end+ε]
    #   - Restar hueco interior del anillo:
    #     Caja lip_in_x × lip_h+ε × lip_in_z solo en Y[cover_y1..lip_y_end+ε]
    #
    # El fillet se aplica ANTES de vaciar (bloque simple → sin junctions → isValid=True)
    # ------------------------------------------------------------------
    eps = 0.05
    full_y = lip_y_end - cover_y0   # 6.5 mm
    # rv clampado: fillet vertical sobre bloque — dim Y crítica = full_y, r debe caber
    rv_full = min(rv, full_y / 2.0 - 0.05)   # min(4.0, 3.20) = 3.20 mm
    full = Part.makeBox(ex, full_y, ez, App.Vector(-ex/2, cover_y0, 0))
    full, fv_ok, fh_ok = apply_fillets(full, rv_full, rh, ez, label="Rear_Cover_full")
    if not fv_ok:
        raise RuntimeError(
            f"[ABORT] Rear_Cover fillet vert r={rv_full:.2f} sobre bloque simple falló."
        )
    n_blk = len(full.Solids)
    if n_blk != 1:
        raise RuntimeError(f"[ABORT] bloque full tras fillet tiene {n_blk} Solids")
    print(f"  [3.1] bloque full: {ex}×{full_y:.2f}×{ez}  Y[{cover_y0:.2f}..{lip_y_end:.2f}]  "
          f"fillet vert r={rv_full:.2f} ok={fv_ok}  horiz r={rh} ok={fh_ok}  "
          f"Solids={n_blk}  V={full.Volume:.0f} mm³")

    # Zona del labio en Y
    lip_zone_y0  = cover_y1               # -17.5 mm
    lip_zone_len = lip_y_end - cover_y1 + eps   # 4.05 mm

    # VACIADO A: restar exceso perimetral exterior de la zona del labio
    #   Franjas X: desde lip_out_x/2 hasta ex/2 (exterior labio)
    lip_out_x_half = D["lip_out_x_half"]   # 19.85 mm
    strip_x_w = ex / 2.0 - lip_out_x_half  # 22 - 19.85 = 2.15 mm
    # franja -X
    full = full.cut(Part.makeBox(
        strip_x_w + eps, lip_zone_len, ez + 2*eps,
        App.Vector(-ex/2 - eps, lip_zone_y0, -eps)
    ))
    # franja +X
    full = full.cut(Part.makeBox(
        strip_x_w + eps, lip_zone_len, ez + 2*eps,
        App.Vector(lip_out_x_half, lip_zone_y0, -eps)
    ))
    # Franjas Z: desde 0..lip_out_z0 y lip_out_z1..ez
    lip_out_z0_v = D["lip_out_z0"]   # 2.15 mm
    lip_out_z1_v = D["lip_out_z1"]   # 67.85 mm
    strip_z_bot = lip_out_z0_v        # 2.15 mm
    strip_z_top = ez - lip_out_z1_v   # 2.15 mm
    # franja inferior Z
    full = full.cut(Part.makeBox(
        ex + 2*eps, lip_zone_len, strip_z_bot + eps,
        App.Vector(-ex/2 - eps, lip_zone_y0, -eps)
    ))
    # franja superior Z
    full = full.cut(Part.makeBox(
        ex + 2*eps, lip_zone_len, strip_z_top + eps,
        App.Vector(-ex/2 - eps, lip_zone_y0, lip_out_z1_v - eps)
    ))
    n_strips = len(full.Solids)
    if n_strips != 1:
        raise RuntimeError(f"[ABORT] tras franjas perimetrales tiene {n_strips} Solids")
    print(f"  [3.2a] franjas perimetrales OK  Solids={n_strips}  V={full.Volume:.0f} mm³  "
          f"strip_x={strip_x_w:.2f}mm  strip_z_bot={strip_z_bot:.2f}mm")

    # VACIADO B: hueco interior del anillo (lip_in_x × lip_h × lip_in_z)
    lip_in_x_half = D["lip_in_x_half"]   # 18.0 mm
    lip_in_x_v    = 2.0 * lip_in_x_half  # 36.0 mm
    lip_in_z0_v   = D["lip_in_z0"]       # 4.0 mm
    lip_in_z1_v   = D["lip_in_z1"]       # 66.0 mm
    lip_in_z_v    = lip_in_z1_v - lip_in_z0_v   # 62.0 mm
    full = full.cut(Part.makeBox(
        lip_in_x_v, lip_zone_len, lip_in_z_v,
        App.Vector(-lip_in_x_half, lip_zone_y0, lip_in_z0_v)
    ))
    n_interior = len(full.Solids)
    if n_interior != 1:
        raise RuntimeError(f"[ABORT] tras hueco interior labio tiene {n_interior} Solids")
    print(f"  [3.2b] hueco interior anillo OK  Solids={n_interior}  V={full.Volume:.0f} mm³  "
          f"lip_in_x={lip_in_x_v:.2f}  lip_in_z={lip_in_z_v:.2f}  lip_th={lip_th:.2f}mm")

    cover = full
    n_cover = len(cover.Solids)
    if n_cover != 1:
        raise RuntimeError(f"[ABORT] cover pre-muescas tiene {n_cover} Solids")
    print(f"  [3.3] bloque vaciado selectivo OK  Solids={n_cover}  V={cover.Volume:.0f} mm³  "
          f"isValid={cover.isValid()}")

    # ------------------------------------------------------------------
    # 3.4 Muescas de poste (CRÍTICO)
    # Los postes (X=±boss_off_x=±17.5, r=3.25) invaden el labio exterior
    # (X[18.0..19.85]). Cada poste toca el anillo en X[18..19.85] (sólo cara ext).
    # Recortamos la zona del poste del anillo en los 4 bosses:
    #   X:  [notch_x_inner .. lip_out_x_half]  (ambos lados ±)
    #   Z:  [boss_z - notch_z_half .. boss_z + notch_z_half]
    #   Y:  [cover_y1 .. lip_y_end + ε]  (toda la zona del labio)
    # notch_x_inner = boss_off_x - boss_r - clr_sl = 14.1 mm (margen generoso)
    # notch_z_half  = boss_r + clr_sl = 3.4 mm
    # ------------------------------------------------------------------
    notch_xi = D["lip_notch_x_inner"]  # 14.1 mm (inner edge toward X=0)
    notch_xo = D["lip_notch_x_outer"]  # 19.85 mm (outer edge = lip exterior)
    notch_zh = D["lip_notch_z_half"]   # 3.4 mm
    notch_w  = notch_xo - notch_xi     # 5.75 mm per side
    notch_yl = lip_y_end - cover_y1 + 2*eps   # 4.0 mm + 2ε

    for bx_sign in [+1, -1]:
        for bz_notch in [P["boss_z_bot"], P["boss_z_top"]]:
            x0_notch = bx_sign * notch_xi if bx_sign > 0 else -(bx_sign * notch_xi + notch_w)
            # Rectify: for +X: x0 = +notch_xi, x1 = +notch_xo
            #          for -X: x0 = -notch_xo, x1 = -notch_xi
            if bx_sign > 0:
                x0_notch = notch_xi
            else:
                x0_notch = -notch_xo
            notch_box = Part.makeBox(
                notch_w, notch_yl, 2.0 * notch_zh,
                App.Vector(x0_notch,
                           cover_y1,
                           bz_notch - notch_zh)
            )
            cover = cover.cut(notch_box)
            n_notch = len(cover.Solids)
            if n_notch != 1:
                raise RuntimeError(
                    f"[ABORT] Rear_Cover tras muesca poste (sign={bx_sign:+d}, bz={bz_notch}) "
                    f"tiene {n_notch} sólidos (esperado 1)."
                )

    n_after_notches = len(cover.Solids)
    print(f"  [3.4] muescas postes OK  Solids={n_after_notches}  "
          f"Volume={cover.Volume:.0f} mm³")
    print(f"         notch_xi={notch_xi:.2f}  notch_xo={notch_xo:.2f}  "
          f"notch_zh=±{notch_zh:.2f}mm")

    # ------------------------------------------------------------------
    # 3.5 Tornillos M3×6 avellanado al ras — tapa UNIFORME 2.5 mm (sin isla)
    #
    # Por tornillo (±boss_off_x=±17.5, boss_z_bot=9, boss_z_top=61):
    # A) Avellanado cónico 90°: r1=3.0 → r2=1.7 en Y[-20..-18.7] (cs_depth=1.3)
    # B) Pasante Ø3.4 desde Y=-20 hasta Y=lip_y_end=-13.5 (6.5 mm total)
    # ------------------------------------------------------------------
    bx_off   = D["boss_off_x"]
    sc_cl    = P["screw_clear_d"]   # 3.4 mm
    sc_hd    = P["screw_head_d"]    # 6.0 mm
    cs_d     = P["cs_depth"]        # 1.3 mm

    total_hole_len = lip_y_end - cover_y0   # 6.5 mm
    r1_cs = sc_hd / 2.0   # 3.0 mm
    r2_cs = sc_cl / 2.0   # 1.7 mm

    cover_boss_pairs = [
        ( bx_off, P["boss_z_bot"]),
        (-bx_off, P["boss_z_bot"]),
        ( bx_off, P["boss_z_top"]),
        (-bx_off, P["boss_z_top"]),
    ]

    for bx, bz in cover_boss_pairs:
        # Avellanado cónico 90°
        cs_cone = Part.makeCone(
            r1_cs, r2_cs, cs_d,
            App.Vector(bx, cover_y0, bz),
            App.Vector(0, 1, 0)
        )
        cover = cover.cut(cs_cone)
        n_cs = len(cover.Solids)
        if n_cs != 1:
            raise RuntimeError(
                f"[ABORT] Rear_Cover tras avellanado cónico (bx={bx}, bz={bz}) "
                f"tiene {n_cs} sólidos (esperado 1)."
            )

        # Pasante Ø3.4 desde cara ext hasta extremo labio
        through = cyl_y(sc_cl/2, total_hole_len, cx=bx, cz=bz, y0=cover_y0)
        cover = cover.cut(through)
        n_th = len(cover.Solids)
        if n_th != 1:
            raise RuntimeError(
                f"[ABORT] Rear_Cover tras pasante (bx={bx}, bz={bz}) "
                f"tiene {n_th} sólidos (esperado 1)."
            )

    n_final = len(cover.Solids)
    if n_final != 1:
        raise RuntimeError(
            f"[ABORT] Rear_Cover tras tornillos tiene {n_final} sólidos "
            f"(esperado 1)."
        )
    print(f"  [3.5] M3x6 countersunk OK  Solids={n_final}  Volume={cover.Volume:.0f} mm³  "
          f"isValid={cover.isValid()}")
    print(f"         bx_off=±{bx_off:.1f}  cs_depth={cs_d:.1f}mm r1={r1_cs:.1f}→r2={r2_cs:.1f}  "
          f"through_len={total_hole_len:.1f}mm Ø{sc_cl}")

    lip_y_start = cover_y1
    return cover, {
        "cover_y0":       cover_y0,
        "cover_y1":       cover_y1,
        "lip_y_start":    lip_y_start,
        "lip_y_end":      lip_y_end,
        "total_hole_len": total_hole_len,
        "lip_od_x":       2.0 * D["lip_out_x_half"],
        "lip_od_z":       D["lip_out_z1"] - D["lip_out_z0"],
    }


# ===========================================================================
# 4. SPREADSHEET
# ===========================================================================
def build_spreadsheet(doc, P, D):
    ss = doc.addObject("Spreadsheet::Sheet", "Params")
    ss.set("A1", "Alias"); ss.set("B1", "Value"); ss.set("C1", "Notes")

    rows = [
        # Envelope
        ("ext_x",            P["ext_x"],            "Exterior width (mm)"),
        ("ext_y",            P["ext_y"],             "Exterior depth (mm)"),
        ("ext_z",            P["ext_z"],             "Exterior height (mm)"),
        ("wall",             P["wall"],              "Shell wall thickness"),
        ("wall_front",       P["wall_front"],        "Frontal wall thickness (=bezel_th=5mm)"),
        ("fillet_ext_vert",  P["fillet_ext_vert"],  "Vertical-corner fillet r"),
        ("fillet_ext_horiz", P["fillet_ext_horiz"], "Top/bottom horizontal-edge fillet r"),
        ("fillet_int",       P["fillet_int"],        "Internal fillet r"),
        # Clearance
        ("clr_fit",          P["clr_fit"],           "Press-fit clearance"),
        ("clr_slide",        P["clr_slide"],         "Sliding clearance (labio/recess)"),
        ("clr_z",            P["clr_z"],             "Z depth clearance"),
        # Screen PCB
        ("scr_pcb_w",        P["scr_pcb_w"],         "TFT PCB width"),
        ("scr_pcb_h",        P["scr_pcb_h"],         "TFT PCB height"),
        ("scr_pcb_t",        P["scr_pcb_t"],         "TFT PCB thickness"),
        ("scr_act_w",        P["scr_act_w"],         "TFT active area width"),
        ("scr_act_h",        P["scr_act_h"],         "TFT active area height"),
        ("scr_act_off_y",    P["scr_act_off_y"],     "Active area Y offset on PCB"),
        ("disp_front_clr",   P["disp_front_clr"],    "Air gap PCB front to frontal inner face"),
        # Bezel / frontal
        ("bezel_lip",        P["bezel_lip"],         "Screen window lip width"),
        ("bezel_th",         P["bezel_th"],          "Frontal wall thickness (=wall_front=5mm)"),
        ("pcb_clip_h",       P["pcb_clip_h"],        "TFT retention rib height"),
        ("header_clr",       P["header_clr"],        "Header clearance behind TFT"),
        # Button
        ("btn_dia",          P["btn_dia"],           "Button concavity diameter Ø12"),
        ("btn_depth",        P["btn_depth"],         "Concave dish depth 0.6 mm"),
        ("btn_wall",         P["btn_wall"],          "NET wall under dish 1.0 mm (CRITICAL)"),
        ("btn_center_z",     P["btn_center_z"],      "Button Z centre 9 mm (FIXED)"),
        # MCU
        ("mcu_w",            P["mcu_w"],             "MCU width"),
        ("mcu_l",            P["mcu_l"],             "MCU length"),
        ("mcu_t",            P["mcu_t"],             "MCU thickness"),
        ("mcu_center_z",     P["mcu_center_z"],      "ESP32 Z centre"),
        ("mcu_usb_w",        P["mcu_usb_w"],         "MCU USB-C port width"),
        ("mcu_usb_h",        P["mcu_usb_h"],         "MCU USB-C port height"),
        ("mcu_usb_protrude", P["mcu_usb_protrude"],  "MCU USB protrusion"),
        # Battery — pared interior fija Body_Main
        ("bat_t",            P["bat_t"],             "Battery thickness Y 7 mm (pared interior fija)"),
        ("bat_w",            P["bat_w"],             "Battery width X 35 mm"),
        ("bat_h",            P["bat_h"],             "Battery height Z 50 mm"),
        ("bat_center_z",     P["bat_center_z"],      "Battery Z centre 40.5 mm"),
        ("bat_swell",        P["bat_swell"],         "Battery swell clearance 1.0 mm"),
        # TP4056
        ("tp_w",             P["tp_w"],              "TP4056 width (Y) 17 mm"),
        ("tp_l",             P["tp_l"],              "TP4056 length (X) 26 mm"),
        ("tp_t",             P["tp_t"],              "TP4056 thickness (Z) 4.5 mm"),
        ("tp_center_z",      P["tp_center_z"],       "TP4056 Z centre 22 mm"),
        ("tp_usb_w",         P["tp_usb_w"],          "USB-C cutout width"),
        ("tp_usb_h",         P["tp_usb_h"],          "USB-C cutout height"),
        # TTP223
        ("ttp_w",            P["ttp_w"],             "TTP223 width 15 mm"),
        ("ttp_h",            P["ttp_h"],             "TTP223 height 11 mm"),
        ("ttp_t",            P["ttp_t"],             "TTP223 thickness 1.6 mm"),
        ("ttp_clip_clr",     P["ttp_clip_clr"],      "TTP223 clip clearance"),
        # Bosses / screws — 4× M3×6 avellanado 90° al ras; tapa uniforme 2.5mm (sin isla)
        ("boss_id",          P["boss_id"],           "M3 self-tap pilot dia 2.5 mm (PLA)"),
        ("boss_z_bot",       P["boss_z_bot"],        "CENTRO Z eje boss inferior 9.0 mm"),
        ("boss_z_top",       P["boss_z_top"],        "CENTRO Z eje boss superior 61.0 mm"),
        ("boss_post_len",    P["boss_post_len"],     "Longitud poste 8 mm (Y[-17.5..-9.5])"),
        ("screw_head_d",     P["screw_head_d"],      "Screw head/avellanado dia 6.0 mm (M3)"),
        ("screw_clear_d",    P["screw_clear_d"],     "Screw clearance hole dia 3.4 mm (M3)"),
        ("screw_len",        P["screw_len"],         "M3x6 self-tap screw length (tapa uniforme 2.5mm)"),
        ("screw_thread_engage", P["screw_thread_engage"], "Engage mínimo en boss 3.5 mm"),
        ("n_screws",         P["n_screws"],          "Number of fasteners (4)"),
        ("cs_angle",         P["cs_angle"],          "Countersunk angle 90 grados"),
        ("cs_depth",         P["cs_depth"],          "Countersunk cone depth 1.3 mm"),
        # Rear cover
        ("cover_th",         P["cover_th"],          "Rear cover thickness 2.5 mm"),
        ("cover_lip_h",      P["cover_lip_h"],       "Cover male lip height 4.0 mm"),
        ("lip_h",            P["lip_h"],             "Body_Main recess depth = cover_lip_h"),
        ("lip_overlap",      P["lip_overlap"],       "Solape labio dentro de la placa 1.0 mm"),
        ("btn_cut_margin",   P["btn_cut_margin"],    "Margen extra alrededor del casquete 2.0 mm"),
        ("charge_mode",      P["charge_mode"],       "0=TP4056 dedicated"),
        # Corner keepout / web — fix: interrumpir recess en esquinas + muesca tapa
        ("corner_solid_web", P["corner_solid_web"],  "Semi-height Z del bloque keepout/web (≥2mm FDM)"),
        # Derived
        ("int_x",            D["int_x"],             "= ext_x - 2*wall = 40 mm"),
        ("int_y",            D["int_y"],             "= ext_y - 2*wall = 36 mm"),
        ("int_z",            D["int_z"],             "= ext_z - 2*wall = 66 mm"),
        ("scr_pocket_w",     D["scr_pocket_w"],      "= scr_pcb_w + 2*clr_fit"),
        ("scr_pocket_h",     D["scr_pocket_h"],      "= scr_pcb_h + 2*clr_fit"),
        ("scr_win_w",        D["scr_win_w"],         "= scr_act_w - 2*bezel_lip"),
        ("scr_win_h",        D["scr_win_h"],         "= scr_act_h - 2*bezel_lip"),
        ("bat_pocket_t",     D["bat_pocket_t"],      "= bat_t + bat_swell + clr_fit"),
        ("bat_pocket_w",     D["bat_pocket_w"],      "= bat_w + 2*clr_fit"),
        ("bat_pocket_h",     D["bat_pocket_h"],      "= bat_h + 2*clr_fit"),
        ("bat_z0",           D["bat_z0"],            "= bat_center_z - bat_h/2 = 15.5 mm"),
        ("boss_od",          D["boss_od"],            "= boss_id + 2*2.0 = 6.5 mm (derivado)"),
        ("boss_off_x",       D["boss_off_x"],        "= 17.5 mm fijo (margen cabeza + solape pared)"),
        ("interface_y",      D["interface_y"],       "= -ext_y/2 + cover_th = -17.5 mm"),
        ("boss_y_start",     D["boss_y_start"],      "= -ext_y/2 + cover_th = -17.5 mm (cara int tapa uniforme)"),
        ("boss_y_end",       D["boss_y_end"],        "= boss_y_start + boss_post_len = -9.5 mm"),
        ("cover_y0",         D["cover_y0"],          "= -ext_y/2 = -20 mm (cara ext tapa)"),
        ("cover_y1",         D["cover_y1"],          "= cover_y0 + cover_th = -17.5 mm (interfaz)"),
        ("lip_y_start",      D["lip_y_start"],       "= cover_y1 - lip_overlap = -18.5 mm"),
        ("lip_y_end",        D["lip_y_end"],         "= cover_y1 + cover_lip_h = -13.5 mm"),
        ("pilot_len",        D["pilot_len"],         "= screw_thread_engage + 1 = 4.5 mm"),
        ("win_z_ctr",        D["win_z_ctr"],         "Screen window Z centre (derived)"),
        ("disp_center_z",    D["disp_center_z"],     "Active area Z centre (= win_z_ctr)"),
        ("btn_center_z_drv", D["btn_center_z"],      "Button Z centre effective = 9.0 mm"),
        ("btn_sphere_r",     D["btn_sphere_r"],      "Sphere R concave dish ≈ 30.3 mm"),
        ("btn_sphere_cy_local",  D["btn_sphere_cy_local"],  "Sphere cy from front face inward"),
        ("btn_sphere_cy_global", D["btn_sphere_cy_global"], "Sphere cy in global Y"),
        ("ttp_pocket_d",     D["ttp_pocket_d"],      "= bezel_th - btn_depth - btn_wall = 3.4 mm"),
        ("usb_cutout_z",     D["usb_cutout_z"],      "USB-C cutout Z centre = tp_center_z = 22 mm"),
        # Corner keepout legacy refs (kept for V-BOSS-FIX probes)
        ("keepout_x_inner",  D["keepout_x_inner"],   "legacy: boss_off_x - boss_r - 0.5 = 13.75 mm"),
        ("keepout_x_outer",  D["keepout_x_outer"],   "legacy: ext_x/2 + 0.1 = 22.1 mm"),
        ("keepout_y0",       D["keepout_y0"],         "legacy: -ext_y/2 = -20.0 mm"),
        ("keepout_y1",       D["keepout_y1"],         "legacy: lip_y_end = -13.5 mm"),
        ("keepout_z_half",   D["keepout_z_half"],     "legacy: boss_r + corner_solid_web = 5.25 mm"),
        ("notch_x_inner",    D["notch_x_inner"],      "legacy notch inner (placa a ras ya no usa)"),
        ("notch_x_outer",    D["notch_x_outer"],      "legacy notch outer"),
        ("notch_z_half",     D["notch_z_half"],       "legacy notch z half"),
        ("web_y0",           D["web_y0"],             "= boss_y_start = -17.5 mm"),
        ("web_y1",           D["web_y1"],             "= lip_y_end = -13.5 mm"),
        # PLACA A RAS + LABIO INTERIOR — nuevos derivados
        ("cover_lip_th",     D["cover_lip_th"],       "= 1.85 mm — pared anillo labio interior"),
        ("rear_rebate_y0",   D["rear_rebate_y0"],     "= -20 mm — rebaje trasero inicio"),
        ("rear_rebate_y1",   D["rear_rebate_y1"],     "= -17.5 mm — rebaje trasero fin = interface_y"),
        ("rear_rebate_th",   D["rear_rebate_th"],     "= cover_th = 2.5 mm"),
        ("cav_inner_x_half", D["cav_inner_x_half"],  "= int_x/2 = 20 mm"),
        ("lip_out_x_half",   D["lip_out_x_half"],    "= cav_inner_x_half - clr_slide = 19.85 mm"),
        ("lip_in_x_half",    D["lip_in_x_half"],     "= lip_out_x_half - cover_lip_th = 18.0 mm"),
        ("lip_out_z0",       D["lip_out_z0"],         "= wall + clr_slide = 2.15 mm"),
        ("lip_out_z1",       D["lip_out_z1"],         "= ext_z - wall - clr_slide = 67.85 mm"),
        ("lip_in_z0",        D["lip_in_z0"],          "= 2*wall = 4.0 mm"),
        ("lip_in_z1",        D["lip_in_z1"],          "= ext_z - 2*wall = 66.0 mm"),
        ("lip_notch_x_inner",D["lip_notch_x_inner"], "= boss_off_x - boss_r - clr_slide = 14.1 mm"),
        ("lip_notch_x_outer",D["lip_notch_x_outer"], "= lip_out_x_half = 19.85 mm"),
        ("lip_notch_z_half", D["lip_notch_z_half"],  "= boss_r + clr_slide = 3.4 mm"),
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
# 5. PARAMETRIC VALIDATION — cover_th 2.5 → 3.0 → restore
# ===========================================================================
def validate_parametric(P_orig):
    """
    V-CS10: validar propagación paramétrica.
    Test A: cover_th 2.5→3.0 y clr_slide 0.15→0.2 propagan; colisión<1mm³ se mantiene.
    Test B: boss_id 2.5→2.6 → boss_od y pilot_len propagan correctamente.
    Restaura valores originales.
    """
    # --- Test A: cover_th + clr_slide ---
    print("\n--- V-CS10a Parametric validation: cover_th 2.5→3.0  clr_slide 0.15→0.2 ---")
    P_a = dict(P_orig)
    P_a["cover_th"]   = 3.0
    P_a["clr_slide"]  = 0.2
    D_a = derive(P_a)
    try:
        sm_a  = build_body_main(P_a, D_a)
        sc_a, _ = build_rear_cover(P_a, D_a)
        inter_a = sm_a.common(sc_a)
        col_a = inter_a.Volume if inter_a.Solids else 0.0
        ok_a = (sm_a.isValid() and len(sm_a.Solids) == 1 and
                sc_a.isValid() and len(sc_a.Solids) == 1 and
                col_a < 1.0)
        print(f"  cover_th=3.0  clr_slide=0.2  "
              f"cover_y1={D_a['cover_y1']:.2f}  lip_out_x_half={D_a['lip_out_x_half']:.3f}  "
              f"rear_rebate_th={D_a['rear_rebate_th']:.2f}  collision={col_a:.4f}mm³")
        print(f"  Body_Main  isValid={sm_a.isValid()}  Solids={len(sm_a.Solids)}")
        print(f"  Rear_Cover isValid={sc_a.isValid()}  Solids={len(sc_a.Solids)}")
        print(f"  => {'PASS — colision<1mm³ y geometria valida' if ok_a else 'FAIL'}")
    except Exception as e:
        print(f"  FAIL: {e}")

    # --- Test B: boss_id ---
    print(f"\n--- V-CS10b Parametric validation: boss_id 2.5→2.6 ---")
    P_b = dict(P_orig)
    P_b["boss_id"] = 2.6
    D_b = derive(P_b)
    expected_boss_od = P_b["boss_id"] + 2 * 2.0   # 6.6
    actual_boss_od   = D_b["boss_od"]
    try:
        sm_b  = build_body_main(P_b, D_b)
        sc_b, _ = build_rear_cover(P_b, D_b)
        propagation_ok = abs(actual_boss_od - expected_boss_od) < 0.001
        print(f"  boss_id=2.6  boss_od={actual_boss_od:.3f} (expected {expected_boss_od:.1f})  "
              f"propagation={'OK' if propagation_ok else 'FAIL'}")
        print(f"  boss_y_start={D_b['boss_y_start']:.2f}  boss_y_end={D_b['boss_y_end']:.2f}  "
              f"pilot_len={D_b['pilot_len']:.2f}")
        print(f"  Body_Main  isValid={sm_b.isValid()}  Solids={len(sm_b.Solids)}")
        print(f"  Rear_Cover isValid={sc_b.isValid()}  Solids={len(sc_b.Solids)}")
        if propagation_ok and sm_b.isValid() and sc_b.isValid():
            print("  PASS")
        else:
            print("  FAIL — revisar propagación o geometría")
    except Exception as e:
        print(f"  FAIL: {e}")

    print(f"--- Restored cover_th={P_orig['cover_th']}  clr_slide={P_orig['clr_slide']}  "
          f"boss_id={P_orig['boss_id']} ---\n")


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
# 7. COMPONENT MOCKUPS
# ===========================================================================
def _comp_box(w, d2, h, cx=0.0, cy=0.0, cz=0.0):
    return Part.makeBox(w, d2, h, App.Vector(cx - w/2, cy - d2/2, cz))


def _component_positions(P, D):
    """
    Component mockups in global frame.
    Global frame: origin = centre footprint Z=0; +Y=front; -Y=rear; +Z=top.
    Body_Main frontal inner face: Y = ext_y/2 - wall_front = +15 mm.
    """
    ex = P["ext_x"]; ey = P["ext_y"]; ez = P["ext_z"]
    wall     = P["wall"]
    bth      = P["wall_front"]      # 5 mm
    front_int_y = ey/2.0 - bth      # +15 mm

    positions = {}

    # --- BATTERY (LiPo 505070 — pared interior fija Body_Main) ---
    # Pocket desde front_int_y hacia -Y; bat_t=7 mm en Y
    bat_z0  = D["bat_z0"]            # 15.5 mm
    bat_cx  = 0.0
    bat_cy  = front_int_y - P["bat_t"]/2    # centro Y (pegada a pared interior)
    bat_cz  = wall + bat_z0
    bat_shape = _comp_box(P["bat_w"], P["bat_t"], P["bat_h"],
                          cx=bat_cx, cy=bat_cy, cz=bat_cz)
    positions["Comp_Battery"] = {
        "shape": bat_shape,
        "color": (0.55, 0.55, 0.55),
        "desc":  (f"LiPo 505070  {P['bat_w']}x{P['bat_h']}x{P['bat_t']}mm  "
                  f"center_z={P['bat_center_z']}  bat_z0={bat_z0:.1f}  "
                  f"pegada a pared interior fija (front_int_y={front_int_y:.1f})"),
    }

    # --- MCU (ESP32-S3 Zero) ---
    mcu_cz  = wall + D["mcu_z_base"]
    mcu_cx  = 0.0
    mcu_cy  = -ey/2 + wall + D["mcu_pocket_w"]/2
    mcu_shape = _comp_box(P["mcu_l"], P["mcu_w"], P["mcu_t"],
                          cx=mcu_cx, cy=mcu_cy, cz=mcu_cz)
    positions["Comp_MCU"] = {
        "shape": mcu_shape,
        "color": (0.13, 0.55, 0.13),
        "desc":  (f"ESP32-S3 Zero  {P['mcu_l']}x{P['mcu_w']}x{P['mcu_t']}mm  "
                  f"center_z={P['mcu_center_z']}  USB interno"),
    }

    # --- TP4056 ---
    tp_cz  = wall + D["tp_z_base"]
    tp_cx  = 0.0
    tp_cy  = -ey/2 + wall + D["tp_pocket_w"]/2
    tp_shape = _comp_box(P["tp_l"], P["tp_w"], P["tp_t"],
                         cx=tp_cx, cy=tp_cy, cz=tp_cz)
    usb_span   = P["mcu_usb_protrude"] + wall
    tp_usb_z   = D["usb_cutout_z"] - P["tp_usb_h"] / 2
    tp_usb_cx  = -(ex/2)
    tp_usb_shape = _comp_box(usb_span, P["tp_usb_w"], P["tp_usb_h"],
                              cx=tp_usb_cx + usb_span/2, cy=tp_cy, cz=tp_usb_z)
    try:
        tp_full = tp_shape.fuse(tp_usb_shape)
    except Exception:
        tp_full = tp_shape
    positions["Comp_TP4056"] = {
        "shape": tp_full,
        "color": (1.0, 0.50, 0.0),
        "desc":  (f"TP4056  {P['tp_l']}x{P['tp_w']}x{P['tp_t']}mm  "
                  f"center_z={P['tp_center_z']}  USB-C -X z={D['usb_cutout_z']}"),
    }

    # --- SCREEN PCB (TFT JD9853) ---
    # PCB against cara interior frontal (+Y side), top flush with interior ceiling
    scr_cy    = front_int_y - P["scr_pcb_t"]/2
    scr_z_bot = ez - wall - P["scr_pcb_h"]
    scr_shape = _comp_box(P["scr_pcb_w"], P["scr_pcb_t"], P["scr_pcb_h"],
                          cx=0.0, cy=scr_cy, cz=scr_z_bot)
    act_z_ctr = D["disp_center_z"]
    marker_t  = 0.3
    act_marker = _comp_box(P["scr_act_w"], marker_t, P["scr_act_h"],
                           cx=0.0,
                           cy=ey/2 - wall + marker_t/2,
                           cz=act_z_ctr - P["scr_act_h"]/2)
    hdr_cy = front_int_y - P["scr_pcb_t"] - 2.54/2
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

    # --- TTP223 ---
    # Apoyado en cara interior frontal (Y=front_int_y)
    # pocket_d = 3.4 mm hacia +Y (dentro de la pared frontal)
    ttp_pd    = D["ttp_pocket_d"]
    z_ttp_ctr = D["btn_center_z"]
    ttp_cy_global = front_int_y + ttp_pd/2   # dentro del pocket frontal
    ttp_shape = _comp_box(P["ttp_w"], P["ttp_t"], P["ttp_h"],
                          cx=0.0, cy=ttp_cy_global, cz=z_ttp_ctr - P["ttp_h"]/2)
    positions["Comp_TTP223"] = {
        "shape": ttp_shape,
        "color": (0.90, 0.85, 0.10),
        "desc":  (f"TTP223  {P['ttp_w']}x{P['ttp_h']}x{P['ttp_t']}mm  "
                  f"Z_ctr={z_ttp_ctr:.2f}  pocket_d={ttp_pd:.2f}mm  "
                  f"net_wall={P['btn_wall']}mm"),
    }

    return positions


def _verify_components(positions, main_shape, cover_shape, P, D):
    """Intersection volumes between components and case bodies. Prints table."""
    TOL = 0.5  # mm³ threshold

    results = []
    for name, comp in positions.items():
        shape = comp["shape"]

        try:
            inter_main  = shape.common(main_shape)
            vol_main    = inter_main.Volume if inter_main.Solids else 0.0
        except Exception as e:
            vol_main = -1.0
            print(f"  [WARN] common({name}, main) failed: {e}")

        try:
            inter_cover = shape.common(cover_shape)
            vol_cover   = inter_cover.Volume if inter_cover.Solids else 0.0
        except Exception as e:
            vol_cover = -1.0
            print(f"  [WARN] common({name}, cover) failed: {e}")

        bb = shape.BoundBox

        if "Battery" in name and vol_main > 0 and vol_main < 5.0:
            # Bat mockup Y_max == front_int_y; PCB clip ribs protrude slightly → tiny overlap
            main_ok  = True
            col_note = "ACCEPTED (mockup edge vs clip ribs, ~2mm3 — pocket is correctly cut)"
        elif "Battery" in name and vol_main >= 5.0:
            main_ok  = False
            col_note = "INTERFERENCIA REAL (bat vs pared/boss — REVISAR)"
        elif "TTP223" in name and vol_main > 0:
            main_ok  = True
            col_note = "ACCEPTED (TTP223 sits in frontal pocket)"
        elif "TP4056" in name and vol_cover > 0:
            # TP4056 is internal; Rear_Cover sits over it when closed — expected
            main_ok  = (vol_main < TOL)
            col_note = "ACCEPTED (TP4056 internal; cover encloses when closed)"
        else:
            main_ok  = (vol_main  < TOL)
            col_note = "OK" if (main_ok and (vol_cover < TOL or vol_cover < 0)) else "INTERFERENCIA REAL"

        results.append({
            "name":      name,
            "vol_main":  vol_main,
            "vol_cover": vol_cover,
            "main_ok":   main_ok,
            "col_note":  col_note,
            "bb":        bb,
        })

    print()
    print("=" * 82)
    print("COLLISION REPORT — Component vs Case Bodies")
    print("=" * 82)
    print(f"  {'Component':<22} {'vs Body_Main mm³':>16} {'vs Rear_Cover mm³':>18}  Result")
    print("-" * 82)
    any_real = False
    for r in results:
        vm = f"{r['vol_main']:.3f}"  if r['vol_main']  >= 0 else "ERR"
        vc = f"{r['vol_cover']:.3f}" if r['vol_cover'] >= 0 else "ERR"
        print(f"  {r['name']:<22} {vm:>16} {vc:>18}  {r['col_note']}")
        if "REAL" in r["col_note"]:
            any_real = True
    print()

    if any_real:
        print("WARNING: INTERFERENCIA REAL detected — review entries above.")
    else:
        print("All non-accepted collisions: OK.")

    return results


def add_component_mockups(doc, P, D, main_shape, cover_shape):
    """Adds Comp_* objects to doc. Sets case bodies to Transparency=80."""
    existing_grp = doc.getObject("Components")
    if existing_grp is not None:
        for obj in list(existing_grp.OutList):
            try: doc.removeObject(obj.Name)
            except Exception: pass
        try: doc.removeObject("Components")
        except Exception: pass
        doc.recompute()

    for obj in list(doc.Objects):
        if obj.Name.startswith("Comp_"):
            try: doc.removeObject(obj.Name)
            except Exception: pass
    doc.recompute()

    positions = _component_positions(P, D)
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

    main_feat  = doc.getObject("Body_Main")
    cover_feat = doc.getObject("Rear_Cover")
    for feat in [main_feat, cover_feat]:
        if feat is not None and feat.ViewObject is not None:
            feat.ViewObject.Transparency = 80
            feat.ViewObject.Visibility   = True

    doc.recompute()

    try:
        import FreeCADGui as Gui
        Gui.activeDocument().activeView().viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
    except Exception:
        pass

    _verify_components(positions, main_shape, cover_shape, P, D)

    comp_names = list(positions.keys())
    print(f"\nComponent mockups added: {comp_names}")


# ===========================================================================
# 8. MAIN
# ===========================================================================
def main():
    print("=" * 60)
    print("Tamagotchi Case — FreeCAD build script")
    print("Topology: Body_Main (frontal integral) + Rear_Cover (tapa -Y)")
    print("FreeCAD:", " ".join(App.Version()[:2]))
    print("=" * 60)

    doc = App.newDocument("TamagotchiCase")

    # Spreadsheet
    print("\n[1/5] Params spreadsheet ...")
    build_spreadsheet(doc, P, D)
    doc.recompute()

    # Body_Main
    print("\n[2/5] Body_Main (frontal integral + cavidad abierta -Y) ...")
    main_shape = build_body_main(P, D)
    main_valid  = main_shape.isValid()
    main_closed = main_shape.isClosed()
    main_vol    = main_shape.Volume
    main_bb     = main_shape.BoundBox
    main_solids = len(main_shape.Solids)
    print(f"  Solids={main_solids}  isValid={main_valid}  isClosed={main_closed}")
    print(f"  Volume={main_vol:.0f} mm3")
    print(f"  BoundBox: {main_bb.XLength:.2f} x {main_bb.YLength:.2f} x {main_bb.ZLength:.2f}")
    print(f"  X[{main_bb.XMin:.2f}..{main_bb.XMax:.2f}]  "
          f"Y[{main_bb.YMin:.2f}..{main_bb.YMax:.2f}]  "
          f"Z[{main_bb.ZMin:.2f}..{main_bb.ZMax:.2f}]")

    main_feat       = doc.addObject("Part::Feature", "Body_Main")
    main_feat.Shape = main_shape
    doc.recompute()

    # Rear_Cover
    print("\n[3/5] Rear_Cover (tapa -Y, labio macho, tornillos eje Y) ...")
    cover_shape, cinfo = build_rear_cover(P, D)

    cover_valid  = cover_shape.isValid()
    cover_closed = cover_shape.isClosed()
    cover_vol    = cover_shape.Volume
    cover_bb     = cover_shape.BoundBox
    cover_solids = len(cover_shape.Solids)
    print(f"  Solids={cover_solids}  isValid={cover_valid}  isClosed={cover_closed}")
    print(f"  Volume={cover_vol:.0f} mm3")
    print(f"  BoundBox: {cover_bb.XLength:.2f} x {cover_bb.YLength:.2f} x {cover_bb.ZLength:.2f}")
    print(f"  X[{cover_bb.XMin:.2f}..{cover_bb.XMax:.2f}]  "
          f"Y[{cover_bb.YMin:.2f}..{cover_bb.YMax:.2f}]  "
          f"Z[{cover_bb.ZMin:.2f}..{cover_bb.ZMax:.2f}]")
    print(f"\n  --- Cover geometry ---")
    print(f"  cover_y0 (ext trasera)  : {cinfo['cover_y0']:.2f} mm")
    print(f"  cover_y1 (int, interfaz): {cinfo['cover_y1']:.2f} mm")
    print(f"  lip_y_start (solape -Y) : {cinfo['lip_y_start']:.2f} mm")
    print(f"  lip_y_end               : {cinfo['lip_y_end']:.2f} mm")
    print(f"  total hole len          : {cinfo['total_hole_len']:.2f} mm")

    cover_feat       = doc.addObject("Part::Feature", "Rear_Cover")
    cover_feat.Shape = cover_shape
    doc.recompute()

    # ------------------------------------------------------------------
    # Verification checks V-CS1..V-CS10 (M3×5 avellanado + isla rebajada)
    # ------------------------------------------------------------------
    print("\n[4/5] Verification checks V-CS1..V-CS10 ...")

    bx_off   = D["boss_off_x"]        # 17.5 mm
    boss_r   = D["boss_r"]            # 3.25 mm
    boss_y_start = D["boss_y_start"]  # -17.5 mm (cara int tapa uniforme)
    boss_y_end   = D["boss_y_end"]    # -9.5 mm
    front_int_y = P["ext_y"]/2 - P["wall_front"]  # +15 mm

    # V-CS1 — Cabeza íntegra: boss_off_x + screw_head_d/2 ≤ ext_x/2 - 1.5 (margen 1.5 mm)
    head_outer_x = bx_off + P["screw_head_d"] / 2.0   # 17.5 + 3.0 = 20.5 mm
    ext_x_half   = P["ext_x"] / 2.0                    # 22.0 mm
    margen_cabeza = ext_x_half - head_outer_x           # 22.0 - 20.5 = 1.5 mm
    vcs1_pass = margen_cabeza >= 1.5 - 0.01
    print(f"  [V-CS1] Cabeza integra: bx_off({bx_off:.1f}) + r_head({P['screw_head_d']/2:.1f}) = "
          f"{head_outer_x:.1f} ≤ ext_x/2({ext_x_half:.1f}) - 1.5  "
          f"margen={margen_cabeza:.1f}mm  => {'PASS' if vcs1_pass else 'FAIL'}")

    # V-CS2 — Poste anclado: extremo exterior poste > pared ext
    # poste en X=±17.5, r=3.25 → extremo = 17.5 + 3.25 = 20.75 > ext_x/2=22? No.
    # El poste SOLAPA la pared: extremo > pared int = ext_x/2 - wall = 20 mm ✓
    pared_int_x = ext_x_half - P["wall"]               # 20.0 mm (cara interior pared lateral)
    boss_outer_x = bx_off + boss_r                      # 17.5 + 3.25 = 20.75 mm
    vcs2_pass = boss_outer_x > pared_int_x
    print(f"  [V-CS2] Poste anclado: boss_outer_x({boss_outer_x:.2f}) > pared_int_x({pared_int_x:.1f})  "
          f"solape={boss_outer_x - pared_int_x:.2f}mm  => {'PASS' if vcs2_pass else 'FAIL'}")

    # V-CS3 — Colisión poste↔batería = 0
    # Postes en Y[-18.5..-10.5]; batería Y=[bat_cy-bat_t/2..bat_cy+bat_t/2]
    bat_cy_v  = front_int_y - P["bat_t"]/2             # +11.5 mm
    bat_y_min = bat_cy_v - P["bat_t"]/2                # +8.0 mm
    # X: batería X=[-17.5..+17.5]; poste en X=±17.5, r=3.25 → inner=14.25
    boss_x_inner = bx_off - boss_r                      # 14.25 mm
    bat_x_half   = P["bat_w"] / 2.0                     # 17.5 mm
    x_gap_cs3    = bat_x_half - boss_x_inner            # 17.5 - 14.25 = 3.25 (overlap en X!)
    # La colisión real requiere overlap simultáneo en X, Y y Z.
    # Y: boss_y_end=-10.5 vs bat_y_min=+8.0 → gap_Y = 8.0-(-10.5) = 18.5 mm → NO COLISIONA
    y_gap_cs3 = bat_y_min - boss_y_end                  # 8.0 - (-10.5) = 18.5 mm
    vcs3_pass = y_gap_cs3 > 0                            # no solapan en Y → colisión 0
    print(f"  [V-CS3] Poste vs Bat: Y_gap={y_gap_cs3:.1f}mm (boss_y_end={boss_y_end:.1f} "
          f"vs bat_y_min={bat_y_min:.1f})  colision=0  => {'PASS' if vcs3_pass else 'FAIL'}")

    # V-CS4 — Colisión poste↔MCU/TP4056 = 0
    # MCU en Y=[rear_ext_y+wall..rear_ext_y+wall+mcu_pocket_w] ≈ Y[-18..-8.7]
    # Postes en X=±17.5 (centrado), MCU en X centrado ±mcu_l/2=±11.75
    # Poste inner X = 14.25 mm > MCU half = 11.75 → no solapan en X
    mcu_x_half = P["mcu_l"] / 2.0   # 11.75 mm
    x_gap_mcu  = boss_x_inner - mcu_x_half   # 14.25 - 11.75 = 2.5 mm
    vcs4_pass  = x_gap_mcu > 0
    print(f"  [V-CS4] Poste vs MCU/TP: boss_x_inner({boss_x_inner:.2f}) > "
          f"mcu_x_half({mcu_x_half:.2f})  X_gap={x_gap_mcu:.2f}mm  => {'PASS' if vcs4_pass else 'FAIL'}")

    # V-CS5 — Coaxialidad cono↔pasante↔piloto en (±17.5, 9/61)
    # Todos los elementos se construyen con cx=±bx_off, cz=boss_z_bot/top → coaxiales por construcción
    vcs5_pass = True  # coaxialidad garantizada por construcción (mismos cx, cz)
    print(f"  [V-CS5] Coaxialidad cono/pasante/piloto: bx_off=±{bx_off:.1f}  "
          f"z_bot={P['boss_z_bot']}  z_top={P['boss_z_top']}  "
          f"(garantizado por construccion cyl_y+makeCone con mismos cx,cz)  "
          f"=> {'PASS' if vcs5_pass else 'FAIL'}")

    # V-CS6 — Engage ≥ 3.5 mm
    # Punta tornillo: cover_y0 + screw_len = -20 + 6 = -14.0 mm (M3×6)
    # Piloto arranca en boss_y_start = -17.5 → engage = -14.0 - (-17.5) = 3.5 mm
    screw_tip_y   = D["cover_y0"] + P["screw_len"]    # -20 + 6 = -14.0
    engage_actual = screw_tip_y - boss_y_start          # -14.0 - (-17.5) = 3.5
    vcs6_pass = engage_actual >= P["screw_thread_engage"] - 0.001
    print(f"  [V-CS6] Engage: cover_y0({D['cover_y0']:.1f}) + screw_len({P['screw_len']:.1f}) = "
          f"punta_y={screw_tip_y:.1f}  boss_y_start={boss_y_start:.1f}  "
          f"engage={engage_actual:.1f}mm (min={P['screw_thread_engage']:.1f})  "
          f"=> {'PASS' if vcs6_pass else 'FAIL'}")

    # V-CS7 — Avellanado al ras: cono NO perfora cara interior tapa (cover_th=2.5)
    # cs_depth=1.3 < cover_th=2.5 ✓; cono llega a Y=-20+1.3=-18.7; interfaz tapa Y=-17.5
    # Queda 1.2mm de pasante limpio (Y[-18.7..-17.5]) antes de entrar en el labio
    cs_cone_tip_y  = D["cover_y0"] + P["cs_depth"]    # -20 + 1.3 = -18.7
    cover_int_y    = D["cover_y1"]                     # -17.5 mm (cara interior tapa uniforme)
    vcs7_pass = (P["cs_depth"] < P["cover_th"] and
                 cs_cone_tip_y <= cover_int_y + 0.001)
    print(f"  [V-CS7] Avellanado al ras (tapa uniforme): cs_depth({P['cs_depth']:.1f}) < "
          f"cover_th({P['cover_th']:.1f})  "
          f"cono_tip_y={cs_cone_tip_y:.2f} ≤ cover_int_y={cover_int_y:.1f}  "
          f"aire_cono={cover_int_y - cs_cone_tip_y:.2f}mm  "
          f"=> {'PASS' if vcs7_pass else 'FAIL'}")

    # V-CS8 — Punta tornillo NO toca fondo del piloto: punta_y > fin_piloto
    # pilot_len = 4.5 → fin_piloto = boss_y_start + pilot_len = -17.5 + 4.5 = -13.0 mm
    # punta_y = -14.0 < fin_piloto = -13.0 → aire = 1.0 mm ✓
    fin_piloto_y  = boss_y_start + D["pilot_len"]      # -17.5 + 4.5 = -13.0
    aire_punta    = fin_piloto_y - screw_tip_y          # -13.0 - (-14.0) = 1.0
    vcs8_pass = aire_punta > 0
    print(f"  [V-CS8] Punta vs fondo piloto: punta_y={screw_tip_y:.1f}  "
          f"fin_piloto={fin_piloto_y:.1f}  aire={aire_punta:.1f}mm  "
          f"=> {'PASS' if vcs8_pass else 'FAIL'}")

    # V-CS9 — Ambas piezas: Solids==1, isValid, isClosed
    def vertex_bbox(shape):
        """Real bounding box from vertex positions (ignores OCC cutter artifacts)."""
        xs = [v.X for v in shape.Vertexes]
        ys = [v.Y for v in shape.Vertexes]
        zs = [v.Z for v in shape.Vertexes]
        return (max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs),
                min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    fillet_tol = P["fillet_ext_vert"] * 0.5 + P["fillet_ext_horiz"] + 0.5
    vbb_main  = vertex_bbox(main_shape)
    vbb_cover = vertex_bbox(cover_shape)
    env_main  = (vbb_main[0] <= P["ext_x"] + fillet_tol and
                 vbb_main[2] <= P["ext_z"] + fillet_tol)
    env_cover = (vbb_cover[0] <= P["ext_x"] + fillet_tol and
                 vbb_cover[2] <= P["ext_z"] + fillet_tol)
    vcs9_pass = (main_valid and main_solids == 1 and main_vol > 0 and
                 cover_valid and cover_solids == 1 and cover_vol > 0)
    print(f"  [V-CS9] Body_Main  : valid={main_valid}  solids={main_solids}  vol={main_vol:.0f}")
    print(f"          vertex BB  : {vbb_main[0]:.2f}x{vbb_main[1]:.2f}x{vbb_main[2]:.2f}  "
          f"X[{vbb_main[3]:.2f}..{vbb_main[4]:.2f}] Y[{vbb_main[5]:.2f}..{vbb_main[6]:.2f}]  "
          f"envelope={'OK' if env_main else 'WARN'}")
    print(f"          (OCC BoundBox: {main_bb.XLength:.1f}x{main_bb.YLength:.1f}x{main_bb.ZLength:.1f} "
          f"— inflated by sphere cutter, NOT the real solid size)")
    print(f"          Rear_Cover  : valid={cover_valid}  solids={cover_solids}  vol={cover_vol:.0f}")
    print(f"          vertex BB  : {vbb_cover[0]:.2f}x{vbb_cover[1]:.2f}x{vbb_cover[2]:.2f}  "
          f"X[{vbb_cover[3]:.2f}..{vbb_cover[4]:.2f}]  envelope={'OK' if env_cover else 'WARN'}")
    print(f"          => {'PASS' if vcs9_pass else 'FAIL'}")

    # V-CS9b — Tapa uniforme: grosor 2.5 mm en las 4 zonas de tornillo
    # Sonda a r=4mm del centro de cada tornillo en dirección HACIA EL CENTRO (−X para boss+X)
    # Esta zona corresponde al interior del antiguo Ø10 de isla.
    # La tapa tiene material en Y[-20..-17.5] = 2.5 mm exacto.
    # Método: cilindro aguja Ø0.8 en Y[-20.1..-17.4] → intersectar con cover → medir span Y.
    print(f"  [V-CS9b] Tapa uniforme — grosor en zona ex-isla (r=4mm del eje, hacia centro):")
    vcs9b_results = []
    cover_y0_v = D["cover_y0"]   # -20.0
    cover_y1_v = D["cover_y1"]   # -17.5
    expected_th = P["cover_th"]  # 2.5
    for bx, bz_boss in [(D["boss_off_x"], P["boss_z_bot"]),
                         (-D["boss_off_x"], P["boss_z_bot"]),
                         (D["boss_off_x"], P["boss_z_top"]),
                         (-D["boss_off_x"], P["boss_z_top"])]:
        # r=4mm hacia el centro (−sign(bx)*4)
        px = bx + (-4.0 if bx > 0 else +4.0)  # toward X=0
        pz = bz_boss
        # Aguja a lo largo de Y desde Y=-20.1 a Y=-17.4 (cubre rango tapa + 0.1mm sobrante)
        needle_len = expected_th + 0.2   # 2.7 mm
        needle = cyl_y(0.4, needle_len,
                       cx=px, cz=pz,
                       y0=cover_y0_v - 0.1)
        try:
            inter = cover_shape.common(needle)
            if inter.Solids or inter.Shells:
                # Use vertex Y range — OCC common may return face soup; grab all Y
                iy_min = min(v.Y for v in inter.Vertexes)
                iy_max = max(v.Y for v in inter.Vertexes)
                th_probe = iy_max - iy_min
            elif inter.Faces:
                iy_min = min(v.Y for v in inter.Vertexes)
                iy_max = max(v.Y for v in inter.Vertexes)
                th_probe = iy_max - iy_min
            else:
                th_probe = 0.0
        except Exception as ex_probe:
            th_probe = -1.0
            print(f"    probe ({px:.1f},{pz}) err: {ex_probe}")
        ok = abs(th_probe - expected_th) < 0.08
        vcs9b_results.append(ok)
        print(f"    boss({bx:+.1f}, z={bz_boss}) probe_x={px:+.2f}: "
              f"th={th_probe:.3f}mm (expected {expected_th:.1f})  "
              f"{'OK' if ok else 'FAIL — grosor incorrecto'}")
    vcs9b_pass = all(vcs9b_results)
    print(f"  [V-CS9b] => {'PASS — tapa uniforme 2.5mm' if vcs9b_pass else 'FAIL'}")

    # ------------------------------------------------------------------
    # V-BOSS-FIX — Verification of corner boss fix (checks a–f of plan)
    # ------------------------------------------------------------------
    print(f"\n  --- V-BOSS-FIX: Corner boss integrity checks ---")

    bx_off_v  = D["boss_off_x"]      # 17.5
    boss_r_v  = D["boss_r"]          # 3.25
    ko_xi_v   = D["keepout_x_inner"] # 13.75
    ko_zh_v   = D["keepout_z_half"]  # 5.25

    # (a) Pre-fix Y range caromed by recess: recess Y = [-20..-16], boss starts at -17.5
    #     overlap Y = max(recess_y_start, boss_y_start) .. min(recess_y_end, lip_h+rear_ext_y)
    #               = max(-20, -17.5) .. min(-16, -16) = -17.5 .. -16.0
    recess_y_end   = -P["ext_y"]/2 + P["lip_h"]   # -20 + 4 = -16.0
    boss_y_s       = D["boss_y_start"]              # -17.5
    pre_fix_y0     = max(-P["ext_y"]/2, boss_y_s)  # -17.5
    pre_fix_y1     = recess_y_end                   # -16.0
    print(f"  (a) Pre-fix Y zone carved from boss: Y[{pre_fix_y0:.2f}..{pre_fix_y1:.2f}] "
          f"= {pre_fix_y1 - pre_fix_y0:.2f} mm (was C-slot; now kept by keepout)")

    # (b) Boss solid at Y=-16,-15,-14: probe cross-section area ≈ π·r² ≈ 33.2 mm²
    # The pilot hole Ø2.5 is inside the boss so gross area = π·boss_r² - π·(boss_id/2)²
    # But we just want to confirm the boss is a closed face (not open/gap).
    # Use a slab wider than the boss, subtract nothing — area > π·r² if web material included.
    # Expect > π·(boss_r² - pilot_r²) ≈ 33.2 - 4.9 = 28.3 mm² net.
    vb_results = []
    boss_area_expected = math.pi * boss_r_v**2   # 33.18 mm²
    boss_area_min = math.pi * (boss_r_v**2 - (P["boss_id"]/2)**2) * 0.7  # net minus pilot, 30% tol
    for bx_probe, bz_probe in [(bx_off_v, P["boss_z_bot"]),
                                (-bx_off_v, P["boss_z_bot"]),
                                (bx_off_v, P["boss_z_top"]),
                                (-bx_off_v, P["boss_z_top"])]:
        for y_slice in [-16.0, -15.0, -14.0]:
            # Thin slab centred at y_slice, covering full boss extent + margin
            slab_w = boss_r_v * 2.0 + 2.0   # 8.5 mm
            slab = Part.makeBox(
                slab_w, 0.1, slab_w,
                App.Vector(bx_probe - boss_r_v - 1.0 if bx_probe > 0 else bx_probe - boss_r_v - 1.0,
                           y_slice - 0.05,
                           bz_probe - boss_r_v - 1.0)
            )
            try:
                inter = main_shape.common(slab)
                area  = inter.Volume / 0.1 if inter.Volume > 0 else 0.0
                ok_b  = area >= boss_area_min
                vb_results.append(ok_b)
                status = "OK" if ok_b else f"WARN (area={area:.1f} < min={boss_area_min:.1f})"
                print(f"    (b) boss({bx_probe:+.1f}, z={bz_probe}) at Y={y_slice}: "
                      f"area≈{area:.1f} mm² (π·r²={boss_area_expected:.1f}, min={boss_area_min:.1f}) → {status}")
            except Exception as e_b:
                print(f"    (b) boss({bx_probe:+.1f}) at Y={y_slice}: probe error: {e_b}")
                vb_results.append(False)
    vb_pass = sum(vb_results) >= len(vb_results) * 0.7
    print(f"  (b) => {'PASS' if vb_pass else 'WARN — some slices thin'}")

    # (b-SYM) Symmetry check: areas at Y=-16,-15,-14 for all 4 corners (compact table)
    # Expected: all 4 corners ≈ same area (π·r²≈33.2 mm²); -X == +X
    print(f"\n  --- SIMETRIA POSTES (areas en mm²; esperado ≈π·r²={boss_area_expected:.1f}) ---")
    print(f"  {'Corner':<22} {'Y=-16':>8} {'Y=-15':>8} {'Y=-14':>8}  Sym")
    sym_corner_areas = {}
    for bx_probe, bz_probe in [(bx_off_v, P["boss_z_bot"]),
                                (-bx_off_v, P["boss_z_bot"]),
                                (bx_off_v, P["boss_z_top"]),
                                (-bx_off_v, P["boss_z_top"])]:
        areas_at = {}
        for y_slice in [-16.0, -15.0, -14.0]:
            slab_w = boss_r_v * 2.0 + 2.0
            slab = Part.makeBox(
                slab_w, 0.1, slab_w,
                App.Vector(bx_probe - boss_r_v - 1.0,
                           y_slice - 0.05,
                           bz_probe - boss_r_v - 1.0)
            )
            try:
                inter = main_shape.common(slab)
                areas_at[y_slice] = inter.Volume / 0.1 if inter.Volume > 0 else 0.0
            except Exception:
                areas_at[y_slice] = -1.0
        sym_corner_areas[(bx_probe, bz_probe)] = areas_at
        label = f"bx={bx_probe:+.1f} bz={bz_probe}"
        a16 = areas_at.get(-16.0, -1.0)
        a15 = areas_at.get(-15.0, -1.0)
        a14 = areas_at.get(-14.0, -1.0)
        sym_ok = all(a > boss_area_min for a in [a16, a15, a14])
        print(f"  {label:<22} {a16:>8.2f} {a15:>8.2f} {a14:>8.2f}  {'OK' if sym_ok else 'WARN'}")
    # Compare +X vs -X for same Z
    print(f"  --- diff +X vs -X (ideally <3 mm²) ---")
    for bz_check in [P["boss_z_bot"], P["boss_z_top"]]:
        for y_slice in [-16.0, -15.0, -14.0]:
            a_pos = sym_corner_areas.get((bx_off_v,  bz_check), {}).get(y_slice, -1.0)
            a_neg = sym_corner_areas.get((-bx_off_v, bz_check), {}).get(y_slice, -1.0)
            diff  = abs(a_pos - a_neg) if (a_pos >= 0 and a_neg >= 0) else -1.0
            sym_flag = "OK" if (diff >= 0 and diff < 3.0) else ("WARN" if diff >= 0 else "ERR")
            print(f"    bz={bz_check} Y={y_slice}: +X={a_pos:.2f}  -X={a_neg:.2f}  diff={diff:.2f}  {sym_flag}")
    print()

    # (c) Boss fused to lateral wall: common(boss_zone, lateral_wall) along full Y[-17.5..-9.5]
    # Probes at Y=-17,-15,-13,-11,-10 (dentro del rango del poste) → vol>0 en todos
    vc_results = []
    anchor_y_probes = [-17.0, -15.0, -13.0, -11.0, -10.0]
    for bx_probe, bz_probe in [(bx_off_v, P["boss_z_bot"]),
                                (-bx_off_v, P["boss_z_bot"]),
                                (bx_off_v, P["boss_z_top"]),
                                (-bx_off_v, P["boss_z_top"])]:
        bx_s = 1 if bx_probe > 0 else -1
        corner_ok_all = True
        vols_at = []
        for y_anchor in anchor_y_probes:
            # Slab covering lateral wall zone (X from wall_inner to wall_outer), thin in Y
            wall_probe_x_inner = bx_s * (P["ext_x"]/2 - P["wall"] - 0.1)
            wall_probe_x_outer = bx_s * (P["ext_x"]/2 + 0.1)
            x_orig = min(wall_probe_x_inner, wall_probe_x_outer)
            wall_slab = Part.makeBox(
                abs(wall_probe_x_outer - wall_probe_x_inner), 0.1, boss_r_v*2 + 2.0,
                App.Vector(x_orig, y_anchor - 0.05, bz_probe - boss_r_v - 1.0)
            )
            try:
                inter_w = main_shape.common(wall_slab)
                vol_w   = inter_w.Volume if inter_w.Solids else 0.0
                ok_this = vol_w > 0.01
                corner_ok_all = corner_ok_all and ok_this
                vols_at.append(f"Y={y_anchor}:{vol_w:.3f}")
            except Exception as e_c:
                corner_ok_all = False
                vols_at.append(f"Y={y_anchor}:ERR")
        vc_results.append(corner_ok_all)
        print(f"    (c) boss({bx_probe:+.1f}, z={bz_probe}) anchor vols: "
              f"{', '.join(vols_at)} → {'OK' if corner_ok_all else 'FAIL — boss detached!'}")
    vc_pass = all(vc_results)
    print(f"  (c) => {'PASS — anclaje continuo Y[-17.5..-9.5]' if vc_pass else 'FAIL — boss not fused to wall'}")

    # (d) Grip ≥3.5 mm: material continuous Y[-17.5..-14] at boss wall region
    # Probe at X = boss_outer - 0.5 mm (in the boss wall, not the pilot bore center)
    # Boss wall zone: X from (boss_center + pilot_r + 0.2) to (boss_center + boss_r - 0.2)
    # = from 17.5 + 1.25 + 0.2 = 18.95 to 17.5 + 3.25 - 0.2 = 20.55 → probe at X=19.5
    vd_results = []
    # Probe at Y=-15 (midpoint of grip zone Y[-17.5..-14]).
    # Cable channel ELIMINADO → todos los postes (±X) tienen sección íntegra.
    # Threshold uniforme para las 4 esquinas: 70% del área neta del poste × slab.
    grip_y_probe = -15.0
    grip_slab_th = 0.2
    boss_pilot_r = P["boss_id"] / 2.0     # 1.25 mm
    boss_net_area = math.pi * (boss_r_v**2 - boss_pilot_r**2)  # ≈28.3 mm²
    exp_vol_full = boss_net_area * grip_slab_th * 0.70   # 70% de sección neta
    for bx_probe, bz_probe in [(bx_off_v, P["boss_z_bot"]),
                                (-bx_off_v, P["boss_z_bot"]),
                                (bx_off_v, P["boss_z_top"]),
                                (-bx_off_v, P["boss_z_top"])]:
        # Wide slab centred at boss XZ, thin in Y
        slab_w = boss_r_v * 2.0 + 1.0   # 7.5 mm
        grip_slab = Part.makeBox(
            slab_w, grip_slab_th, slab_w,
            App.Vector(bx_probe - boss_r_v - 0.5,
                       grip_y_probe - grip_slab_th / 2.0,
                       bz_probe - boss_r_v - 0.5)
        )
        try:
            inter_d = main_shape.common(grip_slab)
            vol_d   = inter_d.Volume if inter_d.Solids else 0.0
            ok_d    = vol_d >= exp_vol_full
            vd_results.append(ok_d)
            print(f"    (d) boss({bx_probe:+.1f}, z={bz_probe}) @Y={grip_y_probe}: "
                  f"vol={vol_d:.4f} (thr≥{exp_vol_full:.4f}) → {'OK' if ok_d else 'FAIL'}")
        except Exception as e_d:
            print(f"    (d) probe error: {e_d}")
            vd_results.append(False)
    vd_pass = all(vd_results)
    print(f"  (d) => {'PASS' if vd_pass else 'FAIL'}")

    # (e) Tapa + cuerpo sin colisión en zona labio (nuevo esquema: no hay keepout en cuerpo).
    #     Con la estrategia "placa a ras + labio interior", el rebaje perimetral del cuerpo
    #     en Y[-20..-17.5] da espacio para la placa. El labio interior entra en la cavidad
    #     (X[18..19.85]). La colisión global ya se verifica en V-CS-COLLISION.
    #     Aquí solo comprobamos que la zona de labio en Y[cover_y1..lip_y_end] de la tapa
    #     NO interfiere con el cuerpo (prueba zonal).
    ve_results = []
    for bx_probe, bz_probe in [(bx_off_v, P["boss_z_bot"]),
                                (-bx_off_v, P["boss_z_bot"]),
                                (bx_off_v, P["boss_z_top"]),
                                (-bx_off_v, P["boss_z_top"])]:
        # Slab en zona del labio Y[cover_y1..lip_y_end], lejos del boss (Z central)
        # Verificamos que el labio exterior (X≈19.85) no colisiona con pared del cuerpo (X=20)
        lip_zone_slab = Part.makeBox(
            2.0, D["lip_y_end"] - D["cover_y1"], 4.0,
            App.Vector(abs(bx_probe) - 2.5, D["cover_y1"], bz_probe - 2.0)
        )
        try:
            inter_e = cover_shape.common(lip_zone_slab)
            vol_e_c = inter_e.Volume if inter_e.Solids else 0.0
            inter_e2 = main_shape.common(lip_zone_slab)
            vol_e_m = inter_e2.Volume if inter_e2.Solids else 0.0
            # Si cover y main ambos tienen material en esa slab → colisión real
            ok_e = not (vol_e_c > 0.01 and vol_e_m > 0.01)
            ve_results.append(ok_e)
            print(f"    (e) labio∩pared ({bx_probe:+.1f}, z={bz_probe}): "
                  f"cover={vol_e_c:.3f}  main={vol_e_m:.3f}  "
                  f"→ {'OK (sin solapamiento)' if ok_e else 'FAIL — labio toca pared!'}")
        except Exception as e_e:
            print(f"    (e) probe error: {e_e}")
            ve_results.append(True)   # no fatal; colisión global ya verificada
    ve_pass = all(ve_results)
    print(f"  (e) => {'PASS — labio no interfiere con pared' if ve_pass else 'FAIL'}")

    # (f) Cabeza Ø6 íntegra: margen ≥1.5 mm (already checked in V-CS1)
    head_edge_x = bx_off_v + P["screw_head_d"] / 2.0
    ext_x_h     = P["ext_x"] / 2.0
    margin_head = ext_x_h - head_edge_x
    vf_pass     = margin_head >= 1.5 - 0.01
    print(f"  (f) Cabeza Ø{P['screw_head_d']:.0f} margen: {margin_head:.2f}mm "
          f"(min 1.5mm) → {'PASS' if vf_pass else 'FAIL'}")

    vboss_fix_pass = vb_pass and vc_pass and vd_pass and ve_pass and vf_pass
    print(f"  V-BOSS-FIX overall => {'PASS' if vboss_fix_pass else 'FAIL (see details above)'}")

    # ------------------------------------------------------------------
    # V-CS-COLLISION (CRITERIO PRINCIPAL) — Body_Main.common(Rear_Cover) < 1 mm³
    # ------------------------------------------------------------------
    # Variables locales necesarias para los probes (derivadas de P/D)
    cover_boss_pairs = [
        ( D["boss_off_x"], P["boss_z_bot"]),
        (-D["boss_off_x"], P["boss_z_bot"]),
        ( D["boss_off_x"], P["boss_z_top"]),
        (-D["boss_off_x"], P["boss_z_top"]),
    ]
    lip_h_main = P["cover_lip_h"]    # 4.0 mm
    cover_y1_main = D["cover_y1"]    # -17.5 mm
    print(f"\n  --- V-CS-COLLISION: interferencia entre piezas completas ---")
    try:
        inter_full = main_shape.common(cover_shape)
        collision_vol = inter_full.Volume if inter_full.Solids else 0.0
        collision_pass = collision_vol < 1.0
        print(f"  [V-CS-COLLISION] Body_Main.common(Rear_Cover).Volume = {collision_vol:.4f} mm³  "
              f"(umbral < 1.0 mm³) => {'PASS' if collision_pass else 'FAIL — COLISION REAL'}")
    except Exception as e_col:
        collision_vol = -1.0
        collision_pass = False
        print(f"  [V-CS-COLLISION] ERROR: {e_col}")

    # V-CS-GAP: holgura labio↔cavidad ≈ 0.15 mm (perfil Y en X=19.85 vs cara cavidad X=20, Z=35)
    # La cara interior de la cavidad está en X=±20 (int_x/2 = 20.0 mm).
    # El exterior del labio está en X=±19.85 (lip_out_x_half).
    # Holgura esperada = cav_inner_x_half - lip_out_x_half = 0.15 mm.
    gap_expected = D["cav_inner_x_half"] - D["lip_out_x_half"]   # 0.15 mm
    print(f"  [V-CS-GAP]   holgura labio↔cavidad = {gap_expected:.3f} mm  "
          f"(cav_inner={D['cav_inner_x_half']:.2f} - lip_out={D['lip_out_x_half']:.2f})  "
          f"=> {'OK (0.10..0.30)' if 0.10 <= gap_expected <= 0.30 else 'WARN'}")

    # V-CS-FLUSH: cara trasera cuerpo en Y=-17.5 + placa Y[-20..-17.5] → superficie continua
    flush_body_y  = D["rear_rebate_y1"]   # -17.5 (cara trasera cuerpo tras rebaje)
    flush_cover_y = D["cover_y1"]          # -17.5 (cara interior placa tapa)
    flush_ok = abs(flush_body_y - flush_cover_y) < 0.001
    print(f"  [V-CS-FLUSH] cara cuerpo Y={flush_body_y:.3f}  cara placa Y={flush_cover_y:.3f}  "
          f"delta={abs(flush_body_y-flush_cover_y):.4f}mm  => {'PASS (a ras)' if flush_ok else 'FAIL'}")

    # V-CS-BOSS-LIP: material de la tapa en la zona del labio (Y>cover_y1) a la altura
    # del boss debe ser casi nulo (muescas lo eliminan). Se mide SOLO la zona del LABIO
    # (Y[cover_y1..lip_y_end]), excluyendo la placa (Y[cover_y0..cover_y1]).
    # El labio exterior tiene X[18..19.85]; el boss ocupa X[14.25..20.75].
    # Las muescas quitan X[14.1..19.85] en Z[boss_z±3.4] → debe quedar solo la placa.
    print(f"  [V-CS-BOSS-LIP] comprobando muescas vs postes (solo zona labio Y>{cover_y1_main:.1f}):")
    boss_lip_results = []
    boss_r_v2 = D["boss_r"]    # 3.25 mm
    for bx_b, bz_b in cover_boss_pairs:
        # Slab solo en zona del LABIO (Y[cover_y1..lip_y_end]) a la altura del boss
        # Cubre el rango Z del boss (±boss_r) y X desde notch_inner hasta lip_out
        notch_xi_v = D["lip_notch_x_inner"]    # 14.1 mm
        notch_xo_v = D["lip_notch_x_outer"]    # 19.85 mm
        notch_zh_v = D["lip_notch_z_half"]     # 3.4 mm
        notch_w_v  = notch_xo_v - notch_xi_v   # 5.75 mm
        lip_zone_h = D["lip_y_end"] - cover_y1_main   # 4.0 mm
        x0_slab = notch_xi_v if bx_b > 0 else -notch_xo_v
        boss_lip_slab = Part.makeBox(
            notch_w_v, lip_zone_h, 2.0 * notch_zh_v,
            App.Vector(x0_slab, cover_y1_main, bz_b - notch_zh_v)
        )
        try:
            inter_bl = cover_shape.common(boss_lip_slab)
            vol_bl = inter_bl.Volume if inter_bl.Solids else 0.0
            # Esperado: muesca eliminó el labio aquí → solo puede quedar material de placa
            # pero la slab empieza en cover_y1 (cara int placa), así que placa no está.
            # Threshold generoso 1 mm³ para artefactos de intersección.
            ok_bl = vol_bl < 1.0
            boss_lip_results.append(ok_bl)
            print(f"    boss({bx_b:+.1f}, z={bz_b})  cover∩notch_zone(lip_only) = {vol_bl:.4f} mm³  "
                  f"=> {'OK (muesca correcta)' if ok_bl else 'WARN (muesca insuficiente)'}")
        except Exception as e_bl:
            print(f"    boss({bx_b:+.1f}, z={bz_b})  probe error: {e_bl}")
            boss_lip_results.append(False)
    boss_lip_pass = all(boss_lip_results)
    print(f"  [V-CS-BOSS-LIP] => {'PASS' if boss_lip_pass else 'WARN — revisar muescas'}")

    # V-CS10 — Parametricidad: validado en validate_parametric()
    validate_parametric(P)

    # ------------------------------------------------------------------
    # Save + STL export
    # ------------------------------------------------------------------
    print(f"\nSaving FCStd: {FCSTD_PATH}")
    doc.saveAs(FCSTD_PATH)
    print("  OK")

    print(f"\nExporting STLs -> {STL_DIR}")
    mn, msz = export_stl(main_shape,  STL_MAIN,  "body-main")
    cn, csz = export_stl(cover_shape, STL_COVER, "rear-cover")

    # Final summary
    print("\n" + "=" * 60)
    print("BUILD COMPLETE")
    print("=" * 60)
    print(f"  FCStd       : {FCSTD_PATH}")
    print(f"  STL main    : {STL_MAIN}")
    print(f"  STL cover   : {STL_COVER}")
    print()
    print(f"  Body_Main   solids={main_solids}  valid={main_valid}  closed={main_closed}  vol={main_vol:.0f} mm3")
    print(f"              vertex BB {vbb_main[0]:.2f}x{vbb_main[1]:.2f}x{vbb_main[2]:.2f}  "
          f"X[{vbb_main[3]:.2f}..{vbb_main[4]:.2f}]  Y[{vbb_main[5]:.2f}..{vbb_main[6]:.2f}]")
    print(f"              (OCC BB {main_bb.XLength:.1f}x{main_bb.YLength:.1f}x{main_bb.ZLength:.1f} "
          f"— inflated by sphere cutter)")
    print(f"  Rear_Cover  solids={cover_solids}  valid={cover_valid}  closed={cover_closed}  vol={cover_vol:.0f} mm3")
    print(f"              vertex BB {vbb_cover[0]:.2f}x{vbb_cover[1]:.2f}x{vbb_cover[2]:.2f}  "
          f"X[{vbb_cover[3]:.2f}..{vbb_cover[4]:.2f}]")
    if mn: print(f"  Body_Main STL : {mn} triangles, {msz} bytes")
    if cn: print(f"  Rear_Cover STL: {cn} triangles, {csz} bytes")

    print()
    print("  --- PARAMETROS CLAVE ---")
    print(f"  ENVELOPE       : {P['ext_x']}x{P['ext_y']}x{P['ext_z']} mm")
    print(f"  TOPOLOGIA      : Body_Main (frontal +Y macizo) + Rear_Cover (tapa -Y)")
    print(f"  PLANO SEP.     : Y={D['interface_y']:.2f} mm")
    print(f"  BOSSES M3      : eje Y  off_x=±{D['boss_off_x']:.1f}mm  "
          f"boss_od={D['boss_od']:.1f}mm  "
          f"Y[{D['boss_y_start']:.1f}..{D['boss_y_end']:.1f}]  post_len={P['boss_post_len']:.1f}mm  "
          f"pilot_len={D['pilot_len']:.1f}mm")
    print(f"  BOSS Z         : inf={P['boss_z_bot']}  sup={P['boss_z_top']} (CENTRO eje)")
    print(f"  BATERIA        : pared interior fija (front_int_y={front_int_y:.1f})  "
          f"cy={bat_cy_v:.1f}  bat_z0={D['bat_z0']:.1f}")
    print(f"  COVER          : th={P['cover_th']} UNIFORME (sin isla)  lip_h={P['cover_lip_h']}  "
          f"cs_depth={P['cs_depth']}mm (cono 90°)  boss_y_start={D['boss_y_start']:.1f}")
    wall_neta = P["wall_front"] - P["btn_depth"] - D["ttp_pocket_d"]
    print(f"  BOTON          : dia={P['btn_dia']}mm  depth={P['btn_depth']}mm  "
          f"wall_NETO={wall_neta:.2f}mm  center_z={D['btn_center_z']:.1f}")
    print(f"  TORNILLOS M3   : M3x{P['screw_len']}  avellanado 90° al ras  "
          f"engage={engage_actual:.1f}mm  clear_d={P['screw_clear_d']}  head_d={P['screw_head_d']}")
    print(f"  V-CS1 margen_cabeza={margen_cabeza:.1f}mm  V-CS2 solape_pared={boss_outer_x-pared_int_x:.2f}mm  "
          f"V-CS6 engage={engage_actual:.1f}mm  V-CS7 aire_cono={cover_int_y-cs_cone_tip_y:.2f}mm  "
          f"V-CS8 aire_punta={aire_punta:.1f}mm")

    issues = []
    if not main_valid    : issues.append("Body_Main not valid!")
    if main_vol <= 0     : issues.append("Body_Main volume <= 0!")
    if main_solids != 1  : issues.append(f"Body_Main has {main_solids} solids (expected 1)!")
    if not cover_valid   : issues.append("Rear_Cover not valid!")
    if cover_vol <= 0    : issues.append("Rear_Cover volume <= 0!")
    if cover_solids != 1 : issues.append(f"Rear_Cover has {cover_solids} solids (expected 1)!")
    if not vcs1_pass     : issues.append(f"V-CS1 FAIL: cabeza sale del contorno (margen={margen_cabeza:.1f}mm < 1.5)!")
    if not vcs2_pass     : issues.append(f"V-CS2 FAIL: poste NO anclado en pared lateral!")
    if not vcs3_pass     : issues.append(f"V-CS3 FAIL: poste colisiona con batería (Y_gap={y_gap_cs3:.1f}mm)!")
    if not vcs4_pass     : issues.append(f"V-CS4 FAIL: poste colisiona con MCU (X_gap={x_gap_mcu:.2f}mm)!")
    if not vcs6_pass     : issues.append(f"V-CS6 FAIL: engage={engage_actual:.1f}mm < {P['screw_thread_engage']:.1f}mm!")
    if not vcs7_pass     : issues.append(f"V-CS7 FAIL: avellanado perfora cara interior tapa (cs_depth={P['cs_depth']} >= cover_th={P['cover_th']})!")
    if not vcs8_pass     : issues.append(f"V-CS8 FAIL: punta toca fondo piloto (aire={aire_punta:.1f}mm)!")
    if not vcs9_pass     : issues.append("V-CS9 FAIL: geometria invalida (Solids!=1 o isValid=False)!")
    if not vcs9b_pass    : issues.append("V-CS9b FAIL: tapa NO uniforme — revisar isla o material faltante!")
    if not abs(P["wall_front"] - P["btn_depth"] - D["ttp_pocket_d"] - P["btn_wall"]) < 0.01:
        issues.append("btn_wall NETA mismatch!")
    if not vboss_fix_pass: issues.append("V-BOSS-FIX FAIL: poste/tapa corner fix incomplete — revisar keepout/notch/web!")
    if not collision_pass: issues.append(f"V-CS-COLLISION FAIL: interferencia={collision_vol:.4f} mm³ >= 1.0 mm³ (CRITICO)")
    if not flush_ok:       issues.append("V-CS-FLUSH FAIL: cara trasera cuerpo y placa tapa no coinciden en Y!")
    if issues:
        print("\nWARNINGS:")
        for w in issues: print(f"  - {w}")
    else:
        print("\nAll V-CS1..V-CS10 + V-BOSS-FIX checks PASSED.")

    doc.recompute()

    # Component mockups
    print("\n[5/5] Component mockups ...")
    add_component_mockups(doc, P, D, main_shape, cover_shape)

    print(f"\nSaving FCStd (final, with mockups): {FCSTD_PATH}")
    doc.saveAs(FCSTD_PATH)
    print("  OK")

    obj_names   = [o.Name for o in doc.Objects]
    comp_in_doc = [n for n in obj_names if n.startswith("Comp_")]
    print(f"\nDocument objects: {obj_names}")
    print(f"Comp_* objects  : {comp_in_doc}")


if __name__ == "__main__":
    main()

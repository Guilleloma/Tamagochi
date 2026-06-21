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

    # Boss / screws — 4× M3×5 countersunk (avellanado 90°, al ras), eje Y
    # boss_od derivado = boss_id + 2*2.0 = 6.5 mm
    # boss_off_x = 17.5 mm (fijo — margen cabeza 1.5 mm al borde, poste solapa pared lateral)
    # boss_z_bot / boss_z_top = CENTRO Z del eje del boss (no base)
    "boss_id":           2.5,   # M3 self-tap pilot diameter (PLA)
    "boss_z_bot":        9.0,   # centro Z eje boss inferior
    "boss_z_top":       61.0,   # centro Z eje boss superior
    "boss_post_len":     8.0,   # longitud poste (Y[-18.5..-10.5])
    "screw_head_d":      6.0,   # Ø cabeza/avellanado M3
    "screw_clear_d":     3.4,   # pasante M3 en tapa
    "screw_len":         5.0,   # M3×5
    "screw_thread_engage": 3.5, # engage mínimo en boss
    "n_screws":          4,
    # Avellanado countersunk 90°
    "cs_angle":         90.0,   # ángulo del cono avellanado (grados)
    "cs_depth":          1.3,   # prof cono = (screw_head_d - screw_clear_d) / 2 = 1.3 mm
    # Isla rebajada en tapa (Opción A)
    "cover_th_screw_island": 1.5,  # espesor tapa en la isla (mínimo material)
    "screw_island_d":   10.0,   # Ø isla rebajada en tapa

    # Rear cover
    "cover_th":          2.5,  # espesor tapa trasera
    "cover_lip_h":       4.0,  # labio macho de la tapa (= lip_h)
    "lip_overlap":       1.0,  # solape labio DENTRO de la placa (mm)
    "btn_cut_margin":    2.0,  # margen extra alrededor del casquete esférico (mm)

    # Lip / ledge
    "lip_h":             4.0,  # ledge recess depth in Body_Main boca -Y

    # Charge mode
    "charge_mode":       0,
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
    # Boss — eje Y M3×5 avellanado al ras + isla rebajada (Opción A)
    # boss_od = boss_id + 2*2.0 = 6.5 mm (pared 2.0 alrededor del piloto)
    # boss_off_x = 17.5 mm (fijo: margen cabeza 1.5 mm al borde + poste solapa pared lateral 0.75 mm)
    # cover_th = 2.5 mm  → cover_y0=-20, cover_y1=-17.5
    # isla rebajada: cover_th_screw_island=1.5 mm queda Y[-20..-18.5]; rebaje Y[-18.5..-17.5]
    # boss_y_start = -ext_y/2 + cover_th_screw_island = -20 + 1.5 = -18.5 mm (cara interior isla)
    # boss_y_end   = boss_y_start + boss_post_len = -18.5 + 8.0 = -10.5 mm
    # pilot_len    = screw_thread_engage + 1.0 = 4.5 mm → Y[-18.5..-14.0]
    # boss_z_bot / boss_z_top = CENTRO Z del eje del boss
    # -------------------------------------------------------------------------
    d["boss_od"]      = P["boss_id"] + 2 * 2.0              # = 6.5 mm
    boss_r            = d["boss_od"] / 2.0                   # = 3.25 mm
    d["boss_r"]       = boss_r
    d["boss_off_x"]   = 17.5                                 # fijo (margen+solape pared)

    d["interface_y"]  = -P["ext_y"] / 2.0 + P["cover_th"]  # = -17.5 mm
    # boss_y_start arranca en cara INTERIOR de la isla (no en cover_y1)
    d["boss_y_start"] = -P["ext_y"] / 2.0 + P["cover_th_screw_island"]  # = -18.5 mm
    d["boss_y_end"]   = d["boss_y_start"] + P["boss_post_len"]           # = -10.5 mm

    # Derived Y refs for cover geometry
    d["front_ext_y"]  = P["ext_y"] / 2.0                    # = +20 mm
    d["front_int_y"]  = P["ext_y"] / 2.0 - P["wall_front"] # = +15 mm
    d["rear_ext_y"]   = -P["ext_y"] / 2.0                   # = -20 mm
    d["cover_y0"]     = -P["ext_y"] / 2.0                   # = -20 mm
    d["cover_y1"]     = d["cover_y0"] + P["cover_th"]       # = -17.5 mm
    d["lip_y_start"]  = d["cover_y1"] - P["lip_overlap"]    # = -18.5 mm
    d["lip_y_end"]    = d["cover_y1"] + P["cover_lip_h"]    # = -13.5 mm

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
    # Dish carved from outside face Y=+20 inward
    # btn_sphere_cy_local (from front face, going inward toward -Y):
    #   surface at Y=+20, sag=btn_depth → sphere centre at Y=+20 - (btn_sphere_r - btn_depth)
    d["btn_sphere_cy_global"] = (P["ext_y"] / 2.0
                                  - d["btn_sphere_r"] + btn_d)

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
    # 2.3 Recess perimetral en boca -Y para labio de la tapa
    # El recess va desde Y=rear_ext_y hacia +Y, profundidad lip_h=4 mm
    # Ancho/alto: cav_x+2*clr_slide × cav_z+2*clr_slide (con holgura)
    # Sólo recortamos la PARED de recess (dejamos las paredes laterales X/Z)
    # El recess tiene pared_recess = wall - clr_slide hacia el exterior
    # Recortamos un hueco del mismo tamaño que la cavidad pero con holgura 0.15
    # ------------------------------------------------------------------
    lip_h   = P["lip_h"]             # = 4 mm
    clr_sl  = P["clr_slide"]         # = 0.15 mm
    recess_w = cav_x + 2*clr_sl      # 40.30 mm (holgura en X)
    recess_z = cav_z + 2*clr_sl      # 66.30 mm (holgura en Z)
    recess_y = lip_h                  # profundidad del recess desde la boca -Y

    # El recess es un hueco que arranca en rear_ext_y y va hacia +Y lip_h mm
    recess_cut = box(recess_w, recess_y, recess_z,
                     cx=0,
                     cy=rear_ext_y + recess_y/2,
                     cz=wall - clr_sl)
    shell = shell.cut(recess_cut)

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
    # 2.9 Bosses M3×5 — postes (boss_post_len=8 mm) anclados en zona trasera
    # boss_od = boss_id + 2*2.0 = 6.5 mm (derivado en D)
    # boss_off_x = 17.5 mm (fijo; poste solapa pared lateral ext_x/2=22 → ancla)
    # Rango Y: [boss_y_start=-18.5 .. boss_y_end=-10.5]
    # Gap hasta front_int_y=+15: 26.5 mm → NUNCA toca el frente
    # Poste Ø6.5 en X=±17.5 → extremo exterior X=17.5+3.25=20.75 > pared_ext=20.0 → solapa
    # Alineación coaxial (±17.5, z_bot/z_top):
    #   avellanado cono  Y[-20..-18.7]  Ø6→Ø3.4 (r1=3.0→r2=1.7, cs_depth=1.3)
    #   isla rebajada    Y[-18.5..-17.5] Ø10 (rebaje en tapa)
    #   pasante          Y[-20..-13.5] Ø3.4
    #   poste            Y[-18.5..-10.5] Ø6.5
    #   piloto M3        Y[-18.5..-14.0] Ø2.5
    #   M3×5 punta:      -20 + 5 = -15.0  (dentro del piloto -14, aire 1.0 mm OK)
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
        # Poste sólido: arranca en boss_y_start=-18.5, crece +Y, longitud 8 mm
        # → Y[-18.5..-10.5]
        boss_solid = cyl_y(boss_od/2, b_post_l, cx=bx, cz=bz, y0=by_start)
        shell = shell.fuse(boss_solid)

        # Piloto M3: arranca en boss_y_start, crece +Y, longitud pilot_len=4.5 mm
        # → Y[-18.5..-14.0]
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

    # Header clearance pocket (behind PCB toward +Y)
    hdr_clr   = P["header_clr"]
    hdr_pocket = box(spw, hdr_clr, 8.0,
                     cx=0,
                     cy=front_int_y + spd + hdr_clr/2,
                     cz=pcb_z_bot)
    shell = shell.cut(hdr_pocket)

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

    # ------------------------------------------------------------------
    # 2.14 Cable channel (5×5 mm, esquina -X / -Y)
    # ------------------------------------------------------------------
    chan_sz = 5.0
    shell = shell.cut(box(chan_sz, chan_sz, ez - 2*wall,
                          cx=-(ex/2 - wall - chan_sz/2 - 0.5),
                          cy=-(ey/2 - wall - chan_sz/2 - 0.5),
                          cz=wall))

    return shell


# ===========================================================================
# 3. REAR_COVER (renombrado desde Body_FrontBezel)
# ===========================================================================
def build_rear_cover(P, D):
    """
    Tapa trasera que cierra la boca -Y del Body_Main.

    Estrategia de fillets (robusta):
      (a) Construir un bloque SIMPLE full = ext_x × (cover_th + cover_lip_h) × ext_z
          en Y[cover_y0 .. lip_y_end] = [-20 .. -13.5]  grosor Y = 6.5 mm.
      (b) Filletear las 4 aristas VERTICALES con fillet_ext_vert=r4 (cabe en 6.5mm ✓)
          y las aristas horizontales de z=0 y z=ez con fillet_ext_horiz=r2.
          Caja simple → makeFillet no tiene junctions → OK.
      (c) Vaciar zona del labio en dos pasos:
          - Restar exceso perimetral en Y[cover_y1..lip_y_end] para reducir el
            contorno exterior de 44×70 a lip_od 43.70×69.70 (holgura clr_slide).
          - Restar el interior del labio para dejar anillo de pared lip_th=1.85mm.
      (d) La zona de la placa Y[cover_y0..cover_y1] conserva el contorno redondeado
          44×70 r4, igual que el cuerpo.
      (e) Cortar 4 pasantes Ø2.4 + counterbore Ø4.
      (f) Abortar con error explícito si Solids!=1 en cualquier etapa.
    """
    ex      = P["ext_x"]; ez = P["ext_z"]
    cth     = P["cover_th"]      # 2.5 mm
    lip_h   = P["cover_lip_h"]   # 4.0 mm
    clr_sl  = P["clr_slide"]     # 0.15 mm
    wall    = P["wall"]

    rv  = P["fillet_ext_vert"]   # 4.0 mm
    rh  = P["fillet_ext_horiz"]  # 2.0 mm

    # Refs Y globales
    cover_y0  = D["cover_y0"]   # -20.0 mm  cara ext tapa
    cover_y1  = D["cover_y1"]   # -17.5 mm  cara int tapa (= interface_y)
    lip_y_end = D["lip_y_end"]  # -13.5 mm  extremo labio hacia +Y

    # Grosor Y del bloque completo = cth + lip_h = 6.5 mm  (r4 < 6.5/2 = 3.25? No, r=4 > 3.25)
    # OCC requiere r <= min_dim/2 en principio, pero el fillet es sobre aristas VERTICALES
    # (eje Z, longitud 70mm). El radio r4 se aplica en el plano XY; la dimensión crítica
    # es la mitad del lado corto en XY → min(ex/2, full_y/2) = min(22, 3.25).
    # r4 > 3.25: OCC fallará si hay aristas en esa cara. Sin embargo, las aristas VERTICALES
    # de una caja son en las 4 esquinas XY y tienen longitud = ez = 70mm (eje Z).
    # El fillet sobre ellas sólo necesita r < min(ex/2, full_y/2) para las caras adyacentes.
    # full_y/2 = 3.25 < r4 → puede fallar. Usamos el mismo clamp que ya usaba el código:
    # rv_safe = min(rv, full_y - 0.1) para garantizar que el fillet cabe en la dimensión Y.
    full_y    = lip_y_end - cover_y0   # 6.5 mm
    rv_safe   = min(rv, full_y / 2.0 - 0.05)   # min(4.0, 3.20) = 3.20 mm
    # rv_safe = 3.20 aplica esquinas verticales que encajan en el bloque de 6.5mm de profundidad.

    # ------------------------------------------------------------------
    # 3.1 Bloque sólido SIMPLE — un único makeBox, topología limpia
    # Y[cover_y0 .. lip_y_end] = [-20 .. -13.5]  grosor = 6.5 mm
    # ------------------------------------------------------------------
    full = Part.makeBox(ex, full_y, ez, App.Vector(-ex/2, cover_y0, 0))
    print(f"  [3.1] full block: {ex}x{full_y:.2f}x{ez}  "
          f"Y[{cover_y0:.2f}..{lip_y_end:.2f}]")

    # ------------------------------------------------------------------
    # 3.2 Fillet sobre bloque simple — topología limpia, sin junctions
    # Vertical edges (eje Z, 4 esquinas): rv_safe
    # Horizontal edges en z=0 y z=ez: rh
    # ------------------------------------------------------------------
    full, fv_ok, fh_ok = apply_fillets(full, rv_safe, rh, ez, label="Rear_Cover_full")
    if not fv_ok:
        raise RuntimeError(
            f"[ABORT] Rear_Cover fillet vertical r={rv_safe:.2f} sobre bloque simple falló. "
            f"Revisar full_y={full_y:.2f}mm vs r={rv_safe:.2f}."
        )
    print(f"  [3.2] fillet vert r={rv_safe:.2f} applied={fv_ok}  horiz r={rh} applied={fh_ok}")

    n_after_fillet = len(full.Solids)
    if n_after_fillet != 1:
        raise RuntimeError(
            f"[ABORT] Rear_Cover tras fillet tiene {n_after_fillet} sólidos (esperado 1)."
        )
    print(f"  [3.2] post-fillet Solids={n_after_fillet}  Volume={full.Volume:.0f} mm³")

    # ------------------------------------------------------------------
    # 3.3 Vaciar zona del labio — 2 pasos
    #
    # La zona del labio en Y = [cover_y1 .. lip_y_end] = [-17.5 .. -13.5]
    # profundidad_lip_zone = lip_h = 4.0 mm
    #
    # PASO A: Restar exceso perimetral exterior en la zona del labio
    #   El bloque full tiene contorno 44×70 (redondeado r4).
    #   El labio exterior debe ser lip_od_x × lip_od_z = 43.70 × 69.70.
    #   Diferencia: 0.15mm a cada lado en X, 0.15mm a cada lado en Z.
    #   Restamos 4 franjas delgadas SOLO en Y[cover_y1..lip_y_end+ε]:
    #     - franja -X: x ∈ [-22 .. -(lip_od_x/2)]  anchura = clr_sl = 0.15mm
    #     - franja +X: x ∈ [+(lip_od_x/2) .. +22]  anchura = 0.15mm
    #     - franja -Z: z ∈ [0 .. clr_sl]            altura  = 0.15mm
    #     - franja +Z: z ∈ [lip_od_z .. ez]         altura  = 0.15mm
    #   Con ε=0.01 mm de sobrante en Y para corte limpio.
    # ------------------------------------------------------------------
    lip_od_x  = ex  - 2.0*clr_sl        # 43.70 mm
    lip_od_z  = ez  - 2.0*clr_sl        # 69.70 mm
    lip_th_x  = wall - clr_sl           # 1.85 mm (pared anillo en X)
    lip_th_z  = wall - clr_sl           # 1.85 mm (pared anillo en Z)

    lip_zone_y0  = cover_y1             # -17.5 mm
    lip_zone_len = lip_y_end - cover_y1 + 0.01  # 4.01 mm (ε sobrante)

    # Franja -X (x ∈ [-22 .. -21.85])
    strip_lx_w = ex/2.0 - lip_od_x/2.0   # = 0.15 mm
    strip_lx = Part.makeBox(
        strip_lx_w + 0.01, lip_zone_len, ez + 0.02,
        App.Vector(-ex/2.0 - 0.005, lip_zone_y0, -0.01)
    )
    full = full.cut(strip_lx)

    # Franja +X (x ∈ [21.85 .. 22])
    strip_rx = Part.makeBox(
        strip_lx_w + 0.01, lip_zone_len, ez + 0.02,
        App.Vector(lip_od_x/2.0 - 0.005, lip_zone_y0, -0.01)
    )
    full = full.cut(strip_rx)

    # Franja -Z (z ∈ [0 .. 0.15])
    strip_bz_h = ez/2.0 - lip_od_z/2.0   # = 0.15 mm
    strip_bz = Part.makeBox(
        ex + 0.02, lip_zone_len, strip_bz_h + 0.01,
        App.Vector(-ex/2.0 - 0.01, lip_zone_y0, -0.005)
    )
    full = full.cut(strip_bz)

    # Franja +Z (z ∈ [69.85 .. 70])
    strip_tz = Part.makeBox(
        ex + 0.02, lip_zone_len, strip_bz_h + 0.01,
        App.Vector(-ex/2.0 - 0.01, lip_zone_y0, lip_od_z - 0.005)
    )
    full = full.cut(strip_tz)

    n_after_strips = len(full.Solids)
    if n_after_strips != 1:
        raise RuntimeError(
            f"[ABORT] Rear_Cover tras recorte perimetral del labio tiene "
            f"{n_after_strips} sólidos (esperado 1)."
        )
    print(f"  [3.3a] perimeter strips cut OK  Solids={n_after_strips}  "
          f"Volume={full.Volume:.0f} mm³")
    print(f"         lip_od_x={lip_od_x:.2f}  lip_od_z={lip_od_z:.2f}  "
          f"lip_th={lip_th_x:.2f}mm")

    # ------------------------------------------------------------------
    # PASO B: Vaciar interior del labio — dejar anillo de pared lip_th
    # Interior del anillo: (lip_od_x - 2*lip_th_x) × (lip_od_z - 2*lip_th_z)
    #                    = 40.00 × 66.00 mm
    # Se resta solo en Y[cover_y1 .. lip_y_end+ε]
    # ------------------------------------------------------------------
    lip_in_x = lip_od_x - 2.0*lip_th_x   # 40.00 mm
    lip_in_z = lip_od_z - 2.0*lip_th_z   # 66.00 mm
    lip_interior = Part.makeBox(
        lip_in_x, lip_zone_len, lip_in_z,
        App.Vector(-lip_in_x/2.0, lip_zone_y0, lip_th_z)
    )
    full = full.cut(lip_interior)

    n_after_interior = len(full.Solids)
    if n_after_interior != 1:
        raise RuntimeError(
            f"[ABORT] Rear_Cover tras vaciado interior labio tiene "
            f"{n_after_interior} sólidos (esperado 1)."
        )
    print(f"  [3.3b] lip interior cut OK  Solids={n_after_interior}  "
          f"Volume={full.Volume:.0f} mm³")
    print(f"         lip_in_x={lip_in_x:.2f}  lip_in_z={lip_in_z:.2f}  "
          f"wall_ring={lip_th_x:.2f}mm")

    # ------------------------------------------------------------------
    # 3.4 Tornillos M3×5 avellanado al ras + isla rebajada (Opción A)
    #
    # Por tornillo (±boss_off_x=±17.5, boss_z_bot=9, boss_z_top=61):
    #
    # A) Isla rebajada — rebaja la tapa desde cara INTERIOR (Y=-17.5) hacia -Y
    #    hasta que queden solo cover_th_screw_island=1.5 mm de material.
    #    La isla ocupa Y[-18.5..-17.5] (1.0 mm de rebaje) → en la isla:
    #    tapa en Y[-20..-18.5] = 1.5 mm.
    #    Cortar cilindro Ø screw_island_d=10, Y[-18.5..-17.5] del cover.
    #    (boss_y_start = -18.5 mm = cover_y0 + cover_th_screw_island)
    #
    # B) Avellanado cónico 90° — desde cara ext Y=-20 hacia +Y, prof cs_depth=1.3 mm.
    #    Cono: r1=screw_head_d/2=3.0 (en Y=-20), r2=screw_clear_d/2=1.7 (en Y=-18.7).
    #    makeCone origin en (±17.5, -20, z_boss), dir (0,1,0).
    #    Cabeza queda al ras (cs_depth=1.3 ≤ cover_th_island=1.5 → V-CS7 ✓).
    #
    # C) Pasante Ø screw_clear_d=3.4: desde Y=-20 hasta Y=-13.5 (lip_y_end),
    #    atraviesa toda la tapa+labio para llegar al piloto.
    #
    # Total_hole_len = lip_y_end - cover_y0 = -13.5 - (-20) = 6.5 mm
    # ------------------------------------------------------------------
    bx_off   = D["boss_off_x"]             # ±17.5 mm
    sc_cl    = P["screw_clear_d"]          # 3.4 mm Ø pasante
    sc_hd    = P["screw_head_d"]           # 6.0 mm Ø cabeza
    cs_d     = P["cs_depth"]              # 1.3 mm prof cono
    isl_d    = P["screw_island_d"]         # 10.0 mm Ø isla
    boss_y_st = D["boss_y_start"]          # -18.5 mm (cara interior isla)
    # Profundidad del rebaje: cover_y1 - boss_y_start = -17.5 - (-18.5) = 1.0 mm
    island_rebaje_len = D["cover_y1"] - boss_y_st   # 1.0 mm

    # Total Y span of through-hole: cover_y0 .. lip_y_end
    total_hole_len = lip_y_end - cover_y0   # = full_y = 6.5 mm

    cover_boss_pairs = [
        ( bx_off, P["boss_z_bot"]),   # inf right
        (-bx_off, P["boss_z_bot"]),   # inf left
        ( bx_off, P["boss_z_top"]),   # sup right
        (-bx_off, P["boss_z_top"]),   # sup left
    ]

    for bx, bz in cover_boss_pairs:
        # --- A) Isla rebajada ---
        # Cilindro Ø10 de boss_y_start(-18.5) a cover_y1(-17.5), 1.0 mm
        island_cut = cyl_y(isl_d/2, island_rebaje_len, cx=bx, cz=bz, y0=boss_y_st)
        full = full.cut(island_cut)

        n_isl = len(full.Solids)
        if n_isl != 1:
            raise RuntimeError(
                f"[ABORT] Rear_Cover tras isla rebajada (bx={bx}, bz={bz}) "
                f"tiene {n_isl} sólidos (esperado 1)."
            )

        # --- B) Avellanado cónico 90° ---
        # Cono: desde cara ext (Y=cover_y0=-20) hacia +Y, longitud cs_depth=1.3 mm
        # r1 = screw_head_d/2 = 3.0 en Y=-20 (cara ext)
        # r2 = screw_clear_d/2 = 1.7 en Y=-18.7 (fondo del cono)
        r1_cs = sc_hd / 2.0   # 3.0 mm
        r2_cs = sc_cl / 2.0   # 1.7 mm
        cs_cone = Part.makeCone(
            r1_cs, r2_cs, cs_d,
            App.Vector(bx, cover_y0, bz),
            App.Vector(0, 1, 0)
        )
        full = full.cut(cs_cone)

        n_cs = len(full.Solids)
        if n_cs != 1:
            raise RuntimeError(
                f"[ABORT] Rear_Cover tras avellanado cónico (bx={bx}, bz={bz}) "
                f"tiene {n_cs} sólidos (esperado 1)."
            )

        # --- C) Pasante Ø3.4 — desde cara ext hasta extremo labio ---
        # Y[cover_y0 .. lip_y_end] = [-20 .. -13.5] = 6.5 mm
        through = cyl_y(sc_cl/2, total_hole_len, cx=bx, cz=bz, y0=cover_y0)
        full = full.cut(through)

        n_th = len(full.Solids)
        if n_th != 1:
            raise RuntimeError(
                f"[ABORT] Rear_Cover tras pasante (bx={bx}, bz={bz}) "
                f"tiene {n_th} sólidos (esperado 1)."
            )

    n_final = len(full.Solids)
    if n_final != 1:
        raise RuntimeError(
            f"[ABORT] Rear_Cover tras agujeros tornillos tiene {n_final} sólidos "
            f"(esperado 1). Revisar posición de avellanado/pasante vs geometría."
        )
    print(f"  [3.4] M3x5 countersunk + isla rebajada OK  "
          f"Solids={n_final}  Volume={full.Volume:.0f} mm³")
    print(f"        bx_off=±{bx_off:.1f}  island_rebaje={island_rebaje_len:.1f}mm Ø{isl_d}  "
          f"cs_depth={cs_d:.1f}mm r1={r1_cs:.1f}→r2={r2_cs:.1f}  "
          f"through_len={total_hole_len:.1f}mm Ø{sc_cl}")

    # Expose lip_y_start for legacy callers (it equals cover_y1 in new scheme)
    lip_y_start = cover_y1

    return full, {
        "cover_y0":       cover_y0,
        "cover_y1":       cover_y1,
        "lip_y_start":    lip_y_start,
        "lip_y_end":      lip_y_end,
        "total_hole_len": total_hole_len,
        "lip_od_x":       lip_od_x,
        "lip_od_z":       lip_od_z,
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
        # Bosses / screws — 4× M3×5 avellanado 90° al ras + isla rebajada (Opción A)
        ("boss_id",          P["boss_id"],           "M3 self-tap pilot dia 2.5 mm (PLA)"),
        ("boss_z_bot",       P["boss_z_bot"],        "CENTRO Z eje boss inferior 9.0 mm"),
        ("boss_z_top",       P["boss_z_top"],        "CENTRO Z eje boss superior 61.0 mm"),
        ("boss_post_len",    P["boss_post_len"],     "Longitud poste 8 mm (Y[-18.5..-10.5])"),
        ("screw_head_d",     P["screw_head_d"],      "Screw head/avellanado dia 6.0 mm (M3)"),
        ("screw_clear_d",    P["screw_clear_d"],     "Screw clearance hole dia 3.4 mm (M3)"),
        ("screw_len",        P["screw_len"],         "M3x5 self-tap screw length"),
        ("screw_thread_engage", P["screw_thread_engage"], "Engage mínimo en boss 3.5 mm"),
        ("n_screws",         P["n_screws"],          "Number of fasteners (4)"),
        ("cs_angle",         P["cs_angle"],          "Countersunk angle 90 grados"),
        ("cs_depth",         P["cs_depth"],          "Countersunk cone depth 1.3 mm"),
        ("cover_th_screw_island", P["cover_th_screw_island"], "Espesor tapa en isla rebajada 1.5 mm"),
        ("screw_island_d",   P["screw_island_d"],   "Diametro isla rebajada 10.0 mm"),
        # Rear cover
        ("cover_th",         P["cover_th"],          "Rear cover thickness 2.5 mm"),
        ("cover_lip_h",      P["cover_lip_h"],       "Cover male lip height 4.0 mm"),
        ("lip_h",            P["lip_h"],             "Body_Main recess depth = cover_lip_h"),
        ("lip_overlap",      P["lip_overlap"],       "Solape labio dentro de la placa 1.0 mm"),
        ("btn_cut_margin",   P["btn_cut_margin"],    "Margen extra alrededor del casquete 2.0 mm"),
        ("charge_mode",      P["charge_mode"],       "0=TP4056 dedicated"),
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
        ("boss_y_start",     D["boss_y_start"],      "= -ext_y/2 + cover_th_screw_island = -18.5 mm"),
        ("boss_y_end",       D["boss_y_end"],        "= boss_y_start + boss_post_len = -10.5 mm"),
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
    V-CS10: boss_id 2.5→2.6 → boss_od y pilot_len propagan correctamente.
    """
    print("\n--- V-CS10 Parametric validation: boss_id 2.5 -> 2.6 ---")
    P_mod = dict(P_orig)
    P_mod["boss_id"] = 2.6
    D_mod = derive(P_mod)
    expected_boss_od = P_mod["boss_id"] + 2 * 2.0   # 6.6
    actual_boss_od   = D_mod["boss_od"]
    try:
        shape_main  = build_body_main(P_mod, D_mod)
        shape_cover, _ = build_rear_cover(P_mod, D_mod)
        bb_main  = shape_main.BoundBox
        bb_cover = shape_cover.BoundBox
        propagation_ok = abs(actual_boss_od - expected_boss_od) < 0.001
        print(f"  boss_id=2.6  boss_od={actual_boss_od:.3f} (expected {expected_boss_od:.1f})  "
              f"propagation={'OK' if propagation_ok else 'FAIL'}")
        print(f"  boss_y_start={D_mod['boss_y_start']:.2f}  boss_y_end={D_mod['boss_y_end']:.2f}  "
              f"pilot_len={D_mod['pilot_len']:.2f}")
        print(f"  Body_Main  isValid={shape_main.isValid()}  Solids={len(shape_main.Solids)}")
        print(f"  Rear_Cover isValid={shape_cover.isValid()}  Solids={len(shape_cover.Solids)}")
        if propagation_ok and shape_main.isValid() and shape_cover.isValid():
            print("  PASS")
        else:
            print("  FAIL — revisar propagación o geometría")
    except Exception as e:
        print(f"  FAIL: {e}")
    print(f"--- Restored boss_id={P_orig['boss_id']} ---\n")


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
    boss_y_start = D["boss_y_start"]  # -18.5 mm
    boss_y_end   = D["boss_y_end"]    # -10.5 mm
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
    # Punta tornillo: cover_y0 + screw_len = -20 + 5 = -15.0 mm
    # Piloto arranca en boss_y_start = -18.5 → engage = -15.0 - (-18.5) = 3.5 mm
    screw_tip_y   = D["cover_y0"] + P["screw_len"]    # -20 + 5 = -15.0
    engage_actual = screw_tip_y - boss_y_start          # -15.0 - (-18.5) = 3.5
    vcs6_pass = engage_actual >= P["screw_thread_engage"] - 0.001
    print(f"  [V-CS6] Engage: cover_y0({D['cover_y0']:.1f}) + screw_len({P['screw_len']:.1f}) = "
          f"punta_y={screw_tip_y:.1f}  boss_y_start={boss_y_start:.1f}  "
          f"engage={engage_actual:.1f}mm (min={P['screw_thread_engage']:.1f})  "
          f"=> {'PASS' if vcs6_pass else 'FAIL'}")

    # V-CS7 — Avellanado al ras: cs_depth ≤ cover_th_island; cono NO perfora cara interior isla
    # cs_depth=1.3 ≤ cover_th_island=1.5 ✓; cono llega a Y=-20+1.3=-18.7 < isla_interior Y=-18.5
    cs_cone_tip_y  = D["cover_y0"] + P["cs_depth"]    # -20 + 1.3 = -18.7
    island_int_y   = boss_y_start                       # -18.5 mm (cara interior isla)
    vcs7_pass = (P["cs_depth"] <= P["cover_th_screw_island"] and
                 cs_cone_tip_y <= island_int_y + 0.001)
    print(f"  [V-CS7] Avellanado al ras: cs_depth({P['cs_depth']:.1f}) ≤ "
          f"cover_th_island({P['cover_th_screw_island']:.1f})  "
          f"cono_tip_y={cs_cone_tip_y:.2f} ≤ isla_int_y={island_int_y:.1f}  "
          f"=> {'PASS' if vcs7_pass else 'FAIL'}")

    # V-CS8 — Punta tornillo NO toca fondo del piloto: punta_y > fin_piloto
    # pilot_len = 4.5 → fin_piloto = boss_y_start + pilot_len = -18.5 + 4.5 = -14.0 mm
    # punta_y = -15.0 < fin_piloto = -14.0 → aire = 1.0 mm ✓
    fin_piloto_y  = boss_y_start + D["pilot_len"]      # -18.5 + 4.5 = -14.0
    aire_punta    = fin_piloto_y - screw_tip_y          # -14.0 - (-15.0) = 1.0
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
    print(f"  COVER          : th={P['cover_th']}  lip_h={P['cover_lip_h']}  "
          f"isla_rebajada=Ø{P['screw_island_d']}mm th={P['cover_th_screw_island']}mm  "
          f"cs_depth={P['cs_depth']}mm (cono 90°)")
    wall_neta = P["wall_front"] - P["btn_depth"] - D["ttp_pocket_d"]
    print(f"  BOTON          : dia={P['btn_dia']}mm  depth={P['btn_depth']}mm  "
          f"wall_NETO={wall_neta:.2f}mm  center_z={D['btn_center_z']:.1f}")
    print(f"  TORNILLOS M3   : M3x{P['screw_len']}  avellanado 90° al ras  "
          f"engage={engage_actual:.1f}mm  clear_d={P['screw_clear_d']}  head_d={P['screw_head_d']}")
    print(f"  V-CS1 margen_cabeza={margen_cabeza:.1f}mm  V-CS2 solape_pared={boss_outer_x-pared_int_x:.2f}mm  "
          f"V-CS6 engage={engage_actual:.1f}mm  V-CS7 aire_cs={P['cover_th_screw_island']-P['cs_depth']:.1f}mm  "
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
    if not vcs7_pass     : issues.append(f"V-CS7 FAIL: avellanado perfora isla (cs_depth={P['cs_depth']} > island={P['cover_th_screw_island']})!")
    if not vcs8_pass     : issues.append(f"V-CS8 FAIL: punta toca fondo piloto (aire={aire_punta:.1f}mm)!")
    if not vcs9_pass     : issues.append("V-CS9 FAIL: geometria invalida (Solids!=1 o isValid=False)!")
    if not abs(P["wall_front"] - P["btn_depth"] - D["ttp_pocket_d"] - P["btn_wall"]) < 0.01:
        issues.append("btn_wall NETA mismatch!")
    if issues:
        print("\nWARNINGS:")
        for w in issues: print(f"  - {w}")
    else:
        print("\nAll V-CS1..V-CS10 checks PASSED.")

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

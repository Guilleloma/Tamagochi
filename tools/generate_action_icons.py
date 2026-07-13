#!/usr/bin/env python3
"""
Genera 7 PNGs 16x16 monocromos para los iconos del HUD inferior de IdleMain.

Convencion del proyecto (ver tools/png_to_xbm.py):
    PNG fondo BLANCO + silueta NEGRA. La silueta se mapea a bit=1 (pixel
    encendido en el OLED SSD1306).

Output:
    assets/sprites/action_feed.png
    assets/sprites/action_play.png
    assets/sprites/action_clean.png
    assets/sprites/action_medicate.png
    assets/sprites/action_discipline.png
    assets/sprites/action_sleep.png
    assets/sprites/action_stats.png

Tras ejecutarlo:
    python3 tools/png_to_xbm.py assets/sprites/ include/sprites_generated.h

Requisitos: pip install Pillow
"""

from pathlib import Path
from PIL import Image


# Cada icono es una lista de 16 strings de 16 caracteres.
# '#' = pixel negro (silueta, ON en OLED). ' ' = pixel blanco (fondo).
ICONS: dict[str, list[str]] = {
    # Hamburguesa: pan superior curvo + relleno + pan inferior.
    "action_feed": [
        "                ",
        "                ",
        "    ######      ",
        "   ########     ",
        "  ##########    ",
        " ############   ",
        "                ",
        " ############   ",
        " ############   ",
        "                ",
        " ############   ",
        "  ##########    ",
        "                ",
        "                ",
        "                ",
        "                ",
    ],
    # Mando: cuerpo redondeado con cruceta a la izquierda y botones a la derecha.
    "action_play": [
        "                ",
        "                ",
        "  ###########   ",
        " #############  ",
        " ##  #     # #  ",
        " # ### ##  ###  ",
        " ##  #  #  # #  ",
        " #      ## ###  ",
        " #         # #  ",
        " #############  ",
        "  ###########   ",
        "                ",
        "                ",
        "                ",
        "                ",
        "                ",
    ],
    # Escoba/cepillo: mango diagonal + cerdas.
    "action_clean": [
        "                ",
        "            ##  ",
        "           ##   ",
        "          ##    ",
        "         ##     ",
        "        ##      ",
        "       ##       ",
        "      ##        ",
        "    ######      ",
        "   ########     ",
        "  # # # # #     ",
        "  # # # # #     ",
        " #  # # #  #    ",
        " #  # # #  #    ",
        "                ",
        "                ",
    ],
    # Capsula medicinal: oval horizontal con linea central de division.
    "action_medicate": [
        "                ",
        "                ",
        "    #######     ",
        "  ###########   ",
        " #####  ###### ",
        "######   ###### ",
        "######   ###### ",
        "######   ###### ",
        "######   ###### ",
        "######   ###### ",
        " #####   ###### ",
        "  ###########   ",
        "    #######     ",
        "                ",
        "                ",
        "                ",
    ],
    # Signo de exclamacion: cuerpo grueso + punto inferior.
    "action_discipline": [
        "                ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "     ####       ",
        "                ",
        "                ",
        "     ####       ",
        "     ####       ",
        "                ",
        "                ",
    ],
    # Letra Z grande estilo zzz.
    "action_sleep": [
        "                ",
        "                ",
        "  ############  ",
        "  ############  ",
        "          ###   ",
        "         ###    ",
        "        ###     ",
        "       ###      ",
        "      ###       ",
        "     ###        ",
        "    ###         ",
        "   ###          ",
        "  ############  ",
        "  ############  ",
        "                ",
        "                ",
    ],
    # Tres barras verticales crecientes (grafico de stats).
    "action_stats": [
        "                ",
        "                ",
        "            ##  ",
        "            ##  ",
        "       ##   ##  ",
        "       ##   ##  ",
        "       ##   ##  ",
        "  ##   ##   ##  ",
        "  ##   ##   ##  ",
        "  ##   ##   ##  ",
        "  ##   ##   ##  ",
        "  ##   ##   ##  ",
        "                ",
        " ############## ",
        "                ",
        "                ",
    ],
}


def buildPng(grid: list[str]) -> Image.Image:
    if len(grid) != 16:
        raise ValueError(f"Cada grid debe tener 16 filas, vinieron {len(grid)}")
    img = Image.new("1", (16, 16), 1)  # mode '1' = binary; 1 = blanco
    pixels = img.load()
    for y, row in enumerate(grid):
        if len(row) < 16:
            row = row.ljust(16)
        for x, ch in enumerate(row[:16]):
            pixels[x, y] = 0 if ch == "#" else 1
    return img


def main() -> None:
    outDir = Path(__file__).resolve().parent.parent / "assets" / "sprites"
    outDir.mkdir(parents=True, exist_ok=True)

    for name, grid in ICONS.items():
        img = buildPng(grid)
        outPath = outDir / f"{name}.png"
        img.save(outPath, "PNG", optimize=True)
        print(f"  OK: {outPath.name}")

    print(f"\n{len(ICONS)} iconos generados en {outDir}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Convierte PNGs monocromos a arrays C en PROGMEM compatibles con U8g2 drawXBMP().

Uso:
    python3 tools/png_to_xbm.py assets/sprites/  include/sprites_generated.h

Convencion del proyecto (default):
    Los PNGs se dibujan de forma natural: fondo BLANCO + silueta NEGRA.
    Por defecto la silueta (pixel negro) se mapea a bit=1 -> pixel ENCENDIDO en
    el OLED SSD1306, sobre fondo APAGADO. Esto es lo que se ve correctamente
    en el OLED del Tamagochi (silueta luminosa sobre fondo oscuro).

    Para el caso opuesto (PNG con fondo negro + silueta blanca) usar --raw,
    que aplica la convencion XBM estandar (pixel blanco -> bit=1).

Convencion XBM (U8g2):
    - LSB first dentro de cada byte
    - Pixel (0,0) es el bit 0 del primer byte

Convencion de nombres PNG -> identificador C:
    baby_idle_0.png      -> kSpriteBabyIdle0
    overlay_sick.png     -> kSpriteOverlaySick
    action_food_meal.png -> kSpriteActionFoodMeal

Requisitos:
    pip install Pillow
"""

import sys
from pathlib import Path
from PIL import Image


def pngToXbmBytes(pngPath: Path, raw: bool = False) -> tuple[list[int], int, int]:
    img = Image.open(pngPath).convert("1")
    width, height = img.size

    bytesPerRow = (width + 7) // 8
    result: list[int] = []

    for row in range(height):
        for colByte in range(bytesPerRow):
            byteVal = 0
            for bit in range(8):
                col = colByte * 8 + bit
                if col < width:
                    pixel = img.getpixel((col, row))
                    # Default del proyecto: PNG con fondo blanco + silueta
                    # negra -> bit=1 sobre la silueta (ON en OLED).
                    # Con --raw: convención XBM estándar (pixel blanco -> bit=1).
                    shouldSet = (pixel != 0) if raw else (pixel == 0)
                    if shouldSet:
                        byteVal |= (1 << bit)
            result.append(byteVal)

    return result, width, height


def bytesToCArray(name: str, data: list[int], width: int, height: int) -> str:
    byteCount = len(data)
    comment = f"// {width}x{height}px - {byteCount} bytes PROGMEM"

    lines = [
        comment,
        f"static const uint8_t {name}[] PROGMEM = {{",
    ]

    chunks = [data[i : i + 8] for i in range(0, len(data), 8)]
    for chunk in chunks:
        hexRow = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexRow},")

    lines.append("};")
    return "\n".join(lines)


def cIdentifierFromFilename(filename: str) -> str:
    stem = Path(filename).stem
    parts = stem.split("_")
    camel = "".join(part.capitalize() for part in parts)
    return f"kSprite{camel}"


def generateHeader(inputDir: Path, outputFile: Path, raw: bool = False) -> None:
    pngFiles = sorted(inputDir.glob("*.png"))

    if not pngFiles:
        print(f"ERROR: No se encontraron PNGs en {inputDir}", file=sys.stderr)
        sys.exit(1)

    guard = outputFile.stem.upper().replace("-", "_") + "_H"
    lines = [
        "// AUTO-GENERADO por tools/png_to_xbm.py - NO editar manualmente",
        f"// Fuente: {inputDir}",
        "#pragma once",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <Arduino.h>",
        "",
    ]

    totalBytes = 0

    for pngPath in pngFiles:
        try:
            data, width, height = pngToXbmBytes(pngPath, raw=raw)
            name = cIdentifierFromFilename(pngPath.name)
            lines.append(bytesToCArray(name, data, width, height))
            lines.append("")
            totalBytes += len(data)
            print(f"  OK: {pngPath.name} -> {name} ({width}x{height}, {len(data)} bytes)")
        except (OSError, ValueError) as exc:
            print(f"  WARN: {pngPath.name} omitido - {exc}", file=sys.stderr)

    lines += [
        f"#endif // {guard}",
        "",
        f"// Total PROGMEM: {totalBytes} bytes ({totalBytes / 1024:.2f} KB)",
    ]

    outputFile.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nGenerado: {outputFile} ({totalBytes} bytes totales)")


def main() -> None:
    args = list(sys.argv[1:])
    raw = False
    if "--raw" in args:
        raw = True
        args.remove("--raw")

    if len(args) != 2:
        print(f"Uso: {sys.argv[0]} [--raw] <directorio_png> <archivo_salida.h>")
        sys.exit(1)

    inputDir = Path(args[0])
    outputFile = Path(args[1])

    if not inputDir.is_dir():
        print(f"ERROR: {inputDir} no es un directorio", file=sys.stderr)
        sys.exit(1)

    outputFile.parent.mkdir(parents=True, exist_ok=True)
    generateHeader(inputDir, outputFile, raw=raw)


if __name__ == "__main__":
    main()

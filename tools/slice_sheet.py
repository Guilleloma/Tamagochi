#!/usr/bin/env python3
"""
Trocea un sprite sheet horizontal generado por Nano Banana en frames
individuales 1-bit listos para png_to_xbm.py.

Flujo:
    1. Detecta dividers (columnas casi totalmente negras) en la imagen fuente.
    2. Crea un crop por frame entre dividers (descarta zonas de divider).
    3. Recorta el bounding box del contenido negro real (margen blanco).
    4. Redimensiona cada frame a target_size x target_size (default 32).
    5. Threshold a 1-bit (umbral 128, sin antialiasing residual).
    6. Guarda como <name>_<index>.png en el directorio de salida.

Uso:
    python3 tools/slice_sheet.py assets/sprites/raw/baby_idle.png \\
                                 assets/sprites/ \\
                                 --name baby_idle \\
                                 --size 32

Notas:
    - El nombre debe coincidir con la convencion del pipeline png_to_xbm.py:
      baby_idle_0.png -> kSpriteBabyIdle0
    - Si la imagen no tiene dividers detectables, se asume 1 frame unico.
    - Requiere: pip install Pillow
"""

import argparse
import sys
from pathlib import Path
from PIL import Image, ImageOps


BLACK_COLUMN_RATIO = 0.85  # >=85% pixeles negros => columna es divider
MIN_DIVIDER_WIDTH_PX = 3   # ignora ruido aislado
THRESHOLD = 128            # corte para binarizacion final


def loadGrayscale(path: Path) -> Image.Image:
    img = Image.open(path).convert("L")
    return img


def isBlackColumn(img: Image.Image, x: int) -> bool:
    height = img.size[1]
    black = 0
    for y in range(height):
        if img.getpixel((x, y)) < THRESHOLD:
            black += 1
    return (black / height) >= BLACK_COLUMN_RATIO


def isBlackRow(img: Image.Image, y: int) -> bool:
    width = img.size[0]
    black = 0
    for x in range(width):
        if img.getpixel((x, y)) < THRESHOLD:
            black += 1
    return (black / width) >= BLACK_COLUMN_RATIO


def trimSolidBorder(img: Image.Image) -> Image.Image:
    """Recorta marco perimetral de filas/columnas casi totalmente negras."""
    w, h = img.size
    top = 0
    while top < h and isBlackRow(img, top):
        top += 1
    bottom = h
    while bottom > top and isBlackRow(img, bottom - 1):
        bottom -= 1
    left = 0
    while left < w and isBlackColumn(img, left):
        left += 1
    right = w
    while right > left and isBlackColumn(img, right - 1):
        right -= 1
    if (left, top, right, bottom) == (0, 0, w, h):
        return img
    return img.crop((left, top, right, bottom))


def findDividerSpans(img: Image.Image) -> list[tuple[int, int]]:
    width = img.size[0]
    spans: list[tuple[int, int]] = []
    inSpan = False
    spanStart = 0

    for x in range(width):
        if isBlackColumn(img, x):
            if not inSpan:
                inSpan = True
                spanStart = x
        else:
            if inSpan:
                inSpan = False
                if x - spanStart >= MIN_DIVIDER_WIDTH_PX:
                    spans.append((spanStart, x))

    if inSpan and (width - spanStart) >= MIN_DIVIDER_WIDTH_PX:
        spans.append((spanStart, width))

    # descartar dividers que tocan los bordes (no separan, son marco)
    return [s for s in spans if s[0] > 0 and s[1] < width]


def frameRangesFromDividers(width: int, dividers: list[tuple[int, int]]) -> list[tuple[int, int]]:
    if not dividers:
        return [(0, width)]

    ranges: list[tuple[int, int]] = []
    cursor = 0
    for start, end in dividers:
        if start > cursor:
            ranges.append((cursor, start))
        cursor = end
    if cursor < width:
        ranges.append((cursor, width))
    return ranges


def cropContentBoundingBox(frame: Image.Image) -> Image.Image:
    """Recorta el margen blanco alrededor del contenido negro."""
    inverted = ImageOps.invert(frame)
    bbox = inverted.getbbox()
    if bbox is None:
        return frame
    return frame.crop(bbox)


def fitToSquareCanvas(frame: Image.Image, size: int) -> Image.Image:
    """Encaja el frame en un canvas cuadrado blanco preservando aspect ratio."""
    w, h = frame.size
    scale = min(size / w, size / h)
    newW = max(1, int(round(w * scale)))
    newH = max(1, int(round(h * scale)))
    resized = frame.resize((newW, newH), Image.LANCZOS)

    canvas = Image.new("L", (size, size), 255)
    offsetX = (size - newW) // 2
    offsetY = (size - newH) // 2
    canvas.paste(resized, (offsetX, offsetY))
    return canvas


def binarize(img: Image.Image) -> Image.Image:
    return img.point(lambda v: 255 if v >= THRESHOLD else 0, mode="L").convert("1")


def sliceSheet(inputPath: Path, outputDir: Path, name: str, size: int,
               preservePosition: bool = False, single: bool = False) -> None:
    img = loadGrayscale(inputPath)
    width = img.size[0]

    if single:
        dividers: list[tuple[int, int]] = []
        frames = [(0, width)]
    else:
        dividers = findDividerSpans(img)
        frames = frameRangesFromDividers(width, dividers)

    print(f"Input:    {inputPath}  ({img.size[0]}x{img.size[1]})")
    print(f"Dividers: {len(dividers)} detected -> {len(frames)} frame{'s' if len(frames) != 1 else ''}")
    print(f"Target:   {size}x{size} per frame")

    outputDir.mkdir(parents=True, exist_ok=True)

    for index, (x0, x1) in enumerate(frames):
        crop = img.crop((x0, 0, x1, img.size[1]))
        deborder = trimSolidBorder(crop)
        if preservePosition:
            staged = deborder
        else:
            staged = cropContentBoundingBox(deborder)
        fitted = fitToSquareCanvas(staged, size)
        binary = binarize(fitted)

        suffix = "" if single else f"_{index}"
        outPath = outputDir / f"{name}{suffix}.png"
        binary.save(outPath)
        print(f"  -> {outPath.name}  ({staged.size[0]}x{staged.size[1]} content)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="PNG sheet generado por Nano Banana")
    parser.add_argument("output_dir", type=Path, help="Directorio destino de los frames")
    parser.add_argument("--name", required=True, help="Prefijo de frame (ej: baby_idle)")
    parser.add_argument("--size", type=int, default=32, help="Tamano target por frame (32 o 16)")
    parser.add_argument("--preserve-position", action="store_true",
                        help="No centrar el contenido (mantener posicion relativa en el canvas, util para overlays con posicion fija)")
    parser.add_argument("--single", action="store_true",
                        help="Tratar la imagen como un unico frame (no detectar dividers, nombre sin _index)")
    args = parser.parse_args()

    if not args.input.is_file():
        print(f"ERROR: {args.input} no existe", file=sys.stderr)
        sys.exit(1)

    if args.size not in (16, 32):
        print(f"WARN: tamano inusual {args.size} (esperado 16 o 32)", file=sys.stderr)

    sliceSheet(args.input, args.output_dir, args.name, args.size, args.preserve_position, args.single)


if __name__ == "__main__":
    main()

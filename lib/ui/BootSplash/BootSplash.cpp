// =============================================================================
// BootSplash.cpp — Splash animado estilo Game Boy DMG.
//
// Este archivo incluye Arduino_GFX_Library.h y se excluye de env:native vía
// lib_ignore = BootSplash en platformio.ini [env:native].
// La lógica de posición Y está en BootSplashLogic/ (compilable en native).
// =============================================================================

#include "BootSplash.h"

// =============================================================================
// Bitmap 1-bit "Furbygotchi" — 87 px ancho × 9 px alto. BOLD.
//
// 11 glyphs de 7×9, separados por 1 columna de 0s.
// Orden de bits: MSB a la izquierda dentro de cada byte.
// Cada fila = 87 bits → 11 bytes (último byte usa solo 7 bits; bit 0 ignorado).
//
// Trazo bold estilo pixel — strokes de 2 px, curvas redondeadas.
// y y g tienen descendente (filas 7-8 por debajo del cuerpo de minúsculas).
// 1=píxel logo, 0=transparente.
//
// Escala: kLogoScale=2 → 174×18 px en pantalla (centrado en 240×296).
//
// ─────────────────────── REJILLA ASCII (87 cols) ─────────────────────────────
//
//  F       u       r       b       y       g       o       t       c       h       i
//  0123456.0123456.0123456.0123456.0123456.0123456.0123456.0123456.0123456.0123456.0123456
//
// Row0: ######..................##..............................................##...........
// Row1: ##......................##................................##............##......##...
// Row2: ##......................##................................##............##...........
// Row3: #####...##..##..##.###..##.##...##..##...####....####...######...####...##.##...##.
// Row4: ##......##..##..####....##..##..##..##..##..##..##..##....##....##......###.##..##.
// Row5: ##......##..##..##......##..##...##.##..##..##..##..##....##....##......##..##..##.
// Row6: ##......##.###..##......##.##.....####...#####...####.....###....####...##..##..##.
// Row7: ##.......####.......................##......##.....................................
// Row8: .................................####....####....................................
//
// ─────────────────────── PER-GLYPH (7×9) ────────────────────────────────────
//
// F:         u:         r:         b:         y:         g:
//   ######.    .......    .......    ##.....    .......    .......
//   ##.....    .......    .......    ##.....    .......    .......
//   ##.....    .......    .......    ##.....    .......    .......
//   #####..    ##..##.    ##.###.    ##.##..    ##..##.    .####..
//   ##.....    ##..##.    ####...    ##..##.    ##..##.    ##..##.
//   ##.....    ##..##.    ##.....    ##..##.    .##.##.    ##..##.
//   ##.....    ##.###.    ##.....    ##.##..    ..####.    .#####.
//   ##.....    .####..    .......    .......    ....##.    ....##.
//   .......    .......    .......    .......    .####..    .####..
//
// o:         t:         c:         h:         i:
//   .......    .......    .......    ##.....    .......
//   .......    ..##...    .......    ##.....    ..##...
//   .......    ..##...    .......    ##.....    .......
//   .####..    ######.    .####..    ##.##..    ..##...
//   ##..##.    ..##...    ##.....    ###.##.    ..##...
//   ##..##.    ..##...    ##.....    ##..##.    ..##...
//   .####..    ..###..    .####..    ##..##.    ..##...
//   .......    .......    .......    .......    .......
//   .......    .......    .......    .......    .......
//
// ─────────────────────────────────────────────────────────────────────────────
//
// Columns per glyph (7 cols each, 1 gap between):
//   F: cols  0- 6    u: cols  8-14    r: cols 16-22
//   b: cols 24-30    y: cols 32-38    g: cols 40-46
//   o: cols 48-54    t: cols 56-62    c: cols 64-70
//   h: cols 72-78    i: cols 80-86
//
// Packing: byte_idx = col/8, bit_pos = 7-(col%8).
// 11 bytes per row, 9 rows = 99 bytes total.
// =============================================================================

static const uint8_t kFurbyLogo[] PROGMEM = {
    // ROW 0: F=1111110, u=0000000, r=0000000, b=1100000, y=0000000, g=0000000, o=0000000, t=0000000, c=0000000, h=1100000, i=0000000
    0xFC, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00,
    // ROW 1: F=1100000, u=0000000, r=0000000, b=1100000, y=0000000, g=0000000, o=0000000, t=0011000, c=0000000, h=1100000, i=0011000
    0xC0, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x30, 0x00, 0xC0, 0x30,
    // ROW 2: F=1100000, u=0000000, r=0000000, b=1100000, y=0000000, g=0000000, o=0000000, t=0011000, c=0000000, h=1100000, i=0000000
    0xC0, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x30, 0x00, 0xC0, 0x00,
    // ROW 3: F=1111100, u=1100110, r=1101110, b=1101100, y=1100110, g=0111100, o=0111100, t=1111110, c=0111100, h=1101100, i=0011000
    0xF8, 0xCC, 0xDC, 0xD8, 0xCC, 0x78, 0x78, 0xFC, 0x78, 0xD8, 0x30,
    // ROW 4: F=1100000, u=1100110, r=1111000, b=1100110, y=1100110, g=1100110, o=1100110, t=0011000, c=1100000, h=1110110, i=0011000
    0xC0, 0xCC, 0xF0, 0xCC, 0xCC, 0xCC, 0xCC, 0x30, 0xC0, 0xEC, 0x30,
    // ROW 5: F=1100000, u=1100110, r=1100000, b=1100110, y=0110110, g=1100110, o=1100110, t=0011000, c=1100000, h=1100110, i=0011000
    0xC0, 0xCC, 0xC0, 0xCC, 0x6C, 0xCC, 0xCC, 0x30, 0xC0, 0xCC, 0x30,
    // ROW 6: F=1100000, u=1101110, r=1100000, b=1101100, y=0011110, g=0111110, o=0111100, t=0011100, c=0111100, h=1100110, i=0011000
    0xC0, 0xDC, 0xC0, 0xD8, 0x3C, 0x7C, 0x78, 0x38, 0x78, 0xCC, 0x30,
    // ROW 7: F=1100000, u=0111100, r=0000000, b=0000000, y=0000110, g=0000110, o=0000000, t=0000000, c=0000000, h=0000000, i=0000000
    0xC0, 0x78, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ROW 8: F=0000000, u=0000000, r=0000000, b=0000000, y=0111100, g=0111100, o=0000000, t=0000000, c=0000000, h=0000000, i=0000000
    0x00, 0x00, 0x00, 0x00, 0x78, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// =============================================================================
// drawLogoBitmap — dibuja el bitmap 1-bit escalado nearest-neighbor.
//
// g     : canvas GFX.
// x, y  : coordenada superior-izquierda del logo en pantalla.
// scale : factor de escala (px de pantalla por px fuente).
// color : color RGB565 para los píxeles activos.
// =============================================================================
static void drawLogoBitmap(Arduino_GFX* g, int16_t x, int16_t y,
                           int16_t scale, uint16_t color) {
    for (int16_t row = 0; row < kLogoSrcH; row++) {
        for (int16_t col = 0; col < kLogoSrcW; col++) {
            // Byte index y bit position dentro del bitmap empaquetado.
            int byteIdx = row * 11 + (col / 8);
            int bitPos  = 7 - (col % 8);
            uint8_t b   = pgm_read_byte(&kFurbyLogo[byteIdx]);
            if (b & (1 << bitPos)) {
                g->fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

// =============================================================================
// playBootSplash — reproduce la animación completa de arranque.
//
// Se llama UNA SOLA VEZ desde setup(), justo tras canvas->begin().
// Dura ≈ (kFallFrames + kBounceFrames + kHoldFrames) * kFrameDelayMs
//      = (20 + 6 + 40) * 33ms ≈ 2.2 s.
// =============================================================================
void playBootSplash(Arduino_GFX* canvas) {
    const int16_t xLogo = (240 - kLogoSrcW * kLogoScale) / 2;  // centrado horizontal
    const int     totalFrames = kFallFrames + kBounceFrames + kHoldFrames;

    for (int frame = 0; frame <= totalFrames; frame++) {
        canvas->fillScreen(COL_SPLASH_BG);
        drawLogoBitmap(canvas, xLogo, bootSplashLogoY(frame), kLogoScale, COL_SPLASH_LOGO);
        canvas->flush();
        delay(kFrameDelayMs);
    }
}

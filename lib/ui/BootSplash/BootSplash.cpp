// =============================================================================
// BootSplash.cpp — Splash animado estilo Game Boy DMG.
//
// Este archivo incluye Arduino_GFX_Library.h y se excluye de env:native vía
// lib_ignore = BootSplash en platformio.ini [env:native].
// La lógica de posición Y está en BootSplashLogic/ (compilable en native).
// =============================================================================

#include "BootSplash.h"

// =============================================================================
// Bitmap 1-bit "Furbygotchi" — 65 px ancho × 7 px alto.
//
// 11 glyphs de 5×7, separados por 1 columna de 0s.
// Orden de bits: MSB a la izquierda dentro de cada byte.
// Cada fila = 65 bits → 9 bytes (último byte usa solo el bit 7 = col 64).
//
// Trazo estilo Game Boy BIOS: bordes redondeados (esquinas limadas).
// 1=píxel logo, 0=transparente.
//
// ─────────────────────────── REJILLA ASCII ───────────────────────────────────
//
// Col:  0         1         2         3         4         5         6
//       0123456789012345678901234567890123456789012345678901234567890123456
//       F    .u    .r    .b    .y    .g    .o    .t    .c    .h    .i    .
//       ↑    ↑     ↑     ↑     ↑     ↑     ↑     ↑     ↑     ↑     ↑
//       0    6     12    18    24    30    36    42    48    54    60
//
// Glyph F (cols 0-4):       Glyph u (cols 6-10):      Glyph r (cols 12-16):
//  01110                     00000                      00000
//  10000                     00000                      00000
//  10000                     01110                      01110
//  11110                     10001                      10000
//  10000                     10001                      10000
//  10000                     10011                      10000
//  00000                     01101                      00000
//
// Glyph b (cols 18-22):     Glyph y (cols 24-28):     Glyph g (cols 30-34):
//  10000                     00000                      00000
//  10000                     00000                      00000
//  10110                     10001                      01110
//  11001                     10001                      10001
//  10001                     10001                      10001
//  11001                     01111                      01111
//  10110                     00001                      01110
//
// Glyph o (cols 36-40):     Glyph t (cols 42-46):     Glyph c (cols 48-52):
//  00000                     00100                      00000
//  00000                     00100                      00000
//  01110                     01110                      01110
//  10001                     00100                      10000
//  10001                     00100                      10000
//  10001                     00110                      10001
//  01110                     00010                      01110
//
// Glyph h (cols 54-58):     Glyph i (cols 60-64):
//  10000                     00100
//  10000                     00000
//  10110                     01100
//  11001                     00100
//  10001                     00100
//  10001                     00100
//  00000                     01110
//
// ─────────────────────────────────────────────────────────────────────────────
//
// Mapa de bits por fila (65 bits, 9 bytes, MSB primero):
//
// Fila 0: F=01110 _u=000000 _r=000000 _b=110000 _y=000000 _g=000000 _o=000000 _t=001000 _c=000000 _h=100000 _i=00100
//   Fila 0 raw bits (65 bits, col 0=MSB del byte 0):
//   F     gap u     gap r     gap b     gap y     gap g     gap o     gap t     gap c     gap h     gap i
//   01110 0 00000 0 00000 0 10000 0 00000 0 00000 0 00000 0 00100 0 00000 0 10000 0 00100
//   0111 0000 0000 0000 0100 0000 0000 0000 0001 0000 0000 0010 0000 0100 0000 0100
//   Byte: 0x70 0x00 0x04 0x00 0x00 0x08 0x00 0x20 0x40
//
// ─── Reconstrucción completa fila a fila ──────────────────────────────────────
//
// Notación abreviada por columna (col 0 es bit MSB del byte 0):
//
// ROW 0:
//   col: 00000000001111111111222222222233333333334444444444555555555566666
//        01234567890123456789012345678901234567890123456789012345678901234
//        01110000000000001000000000000000000000001000000000001000000100
//   Bits agrupados en bytes de 8:
//   01110000 = 0x70
//   00000000 = 0x00
//   01000000 = 0x40   ← col 16 es 'b' col[0]=1 → col global 18 ... wait
//
// ─────────────────────────────────────────────────────────────────────────────
// Rebase correcto — cada glyph empieza en col = glyph_index * 6.
// F:0, u:6, r:12, b:18, y:24, g:30, o:36, t:42, c:48, h:54, i:60
//
// ROW 0 (cols 0..64):
//   F(0-4)=01110  gap(5)=0
//   u(6-10)=00000 gap(11)=0
//   r(12-16)=00000 gap(17)=0
//   b(18-22)=10000 gap(23)=0
//   y(24-28)=00000 gap(29)=0
//   g(30-34)=00000 gap(35)=0
//   o(36-40)=00000 gap(41)=0
//   t(42-46)=00100 gap(47)=0
//   c(48-52)=00000 gap(53)=0
//   h(54-58)=10000 gap(59)=0
//   i(60-64)=00100
//
//   65 bits: 01110 0 00000 0 00000 0 10000 0 00000 0 00000 0 00000 0 00100 0 00000 0 10000 0 00100
//   = 0111 0000 0000 0000 0100 0000 0000 0000 0000 0001 0000 0000 0010 0000 0100 0001 0|0100
//     Byte0 Byte1 Byte2 Byte3 Byte4 Byte5 Byte6 Byte7 Byte8(MSB only, pad 7 zeros)
//
//   Let me flatten all 65 bits:
//   pos: 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64
//   bit: 0  1  1  1  0  0  0  0  0  0  0  0  0  0  0  0  0  0  1  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  1  0  0  0  0  0  0  0  0  0  1  0  0  0  0  0  0  0  1  0  0
//   Byte0: 01110000 = 0x70
//   Byte1: 00000000 = 0x00
//   Byte2: 10000000 = 0x80  ← col18=1 (b row0)
//   Byte3: 00000000 = 0x00
//   Byte4: 00000000 = 0x00
//   Byte5: 00001000 = 0x08  ← col44=1 (t row0: 00100 → col42=0,43=0,44=1,45=0,46=0)
//   Byte6: 00000000 = 0x00
//   Byte7: 01000000 = 0x40  ← col54=1 (h row0)
//   Byte8: 00100000 = 0x20  ← col62=1 (i row0: 00100 → col60=0,61=0,62=1,63=0,64=0)
//          ^^ but col62 is bit index 62 in byte8 it's bit (62-64)=bit2 from MSB → 0b00100000
//
// I'll represent byte8 with col64 at bit7 of byte8 (MSB=col64... no)
// Standard packing: col N → byte N/8, bit (7 - N%8) within that byte.
// So col 62 → byte 7 (62/8=7), bit (7-62%8)=(7-6)=1 → 0b00000010? No:
// byte index = 62 >> 3 = 7. bit within byte = 7 - (62 & 7) = 7-6 = 1 → bit1 of byte7 → 0x02.
// col 64 → byte 8, bit (7-0) = 7 → 0x80 of byte8.
//
// Let me redo cleanly using the formula: bit_in_byte = 7 - (col % 8), byte_idx = col / 8.
// =============================================================================

// Helper macro: set bit for column c in row array r[9]
// Bytes are packed MSB-first (col 0 = bit7 of byte0).
// Formula: byte_idx = c/8, bit = 7-(c%8)
//
// Rather than macros, I precomputed the 9 bytes per row below.
// Each row comment shows the 65 bits as a string for verification.

// Columns per glyph:
//   F: cols  0- 4    u: cols  6-10    r: cols 12-16
//   b: cols 18-22    y: cols 24-28    g: cols 30-34
//   o: cols 36-40    t: cols 42-46    c: cols 48-52
//   h: cols 54-58    i: cols 60-64
//
// Each glyph: 5 bits, rows 0-6.
// Gap (1 col of 0) after each glyph except last.
//
// Pixel patterns (1=opaque, 0=transparent):
//
// F:  01110      u: 00000      r: 00000
//     10000         00000         00000
//     10000         01110         01110
//     11110         10001         10000
//     10000         10001         10000
//     10000         10011         10000
//     00000         01101         00000
//
// b:  10000      y: 00000      g: 00000
//     10000         00000         00000
//     10110         10001         01110
//     11001         10001         10001
//     10001         10001         10001
//     11001         01111         01111
//     10110         00001         01110
//
// o:  00000      t: 00100      c: 00000
//     00000         00100         00000
//     01110         01110         01110
//     10001         00100         10000
//     10001         00100         10000
//     10001         00110         10001
//     01110         00010         01110
//
// h:  10000      i: 00100
//     10000         00000
//     10110         01100
//     11001         00100
//     10001         00100
//     10001         00100
//     00000         01110
//
// =============================================================================
// Packed bitmap: 9 bytes × 7 rows = 63 bytes.
// Row 0 = top row of glyphs.
//
// Packing helper (verified with bit formula col/8 and 7-(col%8)):
//   For each row, I list the 65-bit string and compute 9 bytes.
//
// ROW 0: F=01110, u=00000, r=00000, b=10000, y=00000, g=00000, o=00000, t=00100, c=00000, h=10000, i=00100
//   65 bits (col0..col64):
//   0111 0|0 0000|0 0 0000|0 0 1000|0 0 0000|0 0 0000|0 0 0000|0 0 0010|0 0 0000|0 1 0000|0 0 0100|0
//   Byte0(c0-7):  01110000 = 0x70
//   Byte1(c8-15): 00000000 = 0x00
//   Byte2(c16-23):10000000 = 0x80  (c18=1: b row0 bit0)
//   Byte3(c24-31):00000000 = 0x00
//   Byte4(c32-39):00000000 = 0x00
//   Byte5(c40-47):00001000 = 0x08  (c44=1: t row0 bit2 → c42=0,43=0,44=1)
//   Byte6(c48-55):00000000 = 0x00
//         Wait: c54=1 (h row0). c54 → byte54/8=6, bit7-(54%8)=7-6=1 → 0x02
//   Byte6(c48-55):00000010 = 0x02  (c54=1)
//   Byte7(c56-63):00000000 = 0x00
//         c62=1 (i row0 bit2: col60=0,61=0,62=1). c62→byte7, bit7-6=1 → 0x02
//   Byte7(c56-63):00000010 = 0x02
//   Byte8(c64):   00000000 = 0x00  (c64=0: i bit4)
//
// Wait, let me redo: i row0 = 00100. i starts at col60.
//   col60=0, col61=0, col62=1, col63=0, col64=0.
//   c62 → byte7, bit(7-6)=1 → byte7 |= 0x02. ✓
//
// h row0 = 10000. h starts at col54.
//   col54=1, col55=0, col56=0, col57=0, col58=0.
//   c54 → byte6, bit(7-6)=1 → byte6 |= 0x02. ✓
//
// t row0 = 00100. t starts at col42.
//   col42=0, col43=0, col44=1, col45=0, col46=0.
//   c44 → byte5, bit(7-4)=3 → 0x08. ✓
//
// b row0 = 10000. b starts at col18.
//   col18=1 → byte2, bit(7-2)=5 → 0x20.
//   So Byte2 = 0x20, not 0x80. Let me recheck:
//   c18 → byte = 18/8 = 2. bit = 7 - (18%8) = 7-2 = 5 → 0x20. ✓
//
// F row0 = 01110. F starts at col0.
//   col0=0→ bit7=0, col1=1→bit6=1, col2=1→bit5=1, col3=1→bit4=1, col4=0→bit3=0
//   Byte0 bits from c0-c7: 0,1,1,1,0,0,0,0 = 0x70. ✓
//
// ROW 0 corrected:
//   Byte0=0x70, Byte1=0x00, Byte2=0x20, Byte3=0x00, Byte4=0x00,
//   Byte5=0x08, Byte6=0x02, Byte7=0x02, Byte8=0x00
//
// ROW 1: F=10000, u=00000, r=00000, b=10000, y=00000, g=00000, o=00000, t=00100, c=00000, h=10000, i=00000
//   F col0=1→byte0 bit7=0x80; u=00000; r=00000; b col18=1→byte2 bit5=0x20;
//   y=00000; g=00000; o=00000; t col44=1→byte5 bit3=0x08; c=00000;
//   h col54=1→byte6 bit1=0x02; i=00000.
//   Byte0=0x80, Byte1=0x00, Byte2=0x20, Byte3=0x00, Byte4=0x00,
//   Byte5=0x08, Byte6=0x02, Byte7=0x00, Byte8=0x00
//
// ROW 2: F=10000, u=01110, r=01110, b=10110, y=10001, g=01110, o=01110, t=01110, c=01110, h=10110, i=01100
//   F: c0=1→0x80 in byte0
//   u: c6=0,c7=1,c8=1,c9=1,c10=0 → c7:byte0 bit0=0x01; c8:byte1 bit7=0x80; c9:byte1 bit6=0x40
//      → byte0|=0x01, byte1|=0xC0? Let me recheck u=01110: c6=0,c7=1,c8=1,c9=1,c10=0
//      c7→byte0 bit0=0x01. c8→byte1 bit7=0x80. c9→byte1 bit6=0x40. c10→byte1 bit5=0x20.
//      byte0|=0x01, byte1|=0xE0.  Wait u=01110: c6=0,c7=1,c8=1,c9=1,c10=0 → c10=0 so no 0x20.
//      byte0|=0x01, byte1|=(0x80|0x40)=0xC0.
//   r: r=01110: c12=0,c13=1,c14=1,c15=1,c16=0
//      c13→byte1 bit2=0x04. c14→byte1 bit1=0x02. c15→byte1 bit0=0x01.
//      byte1|=0x07.
//   b: b=10110: c18=1,c19=0,c20=1,c21=1,c22=0
//      c18→byte2 bit5=0x20. c20→byte2 bit3=0x08. c21→byte2 bit2=0x04.
//      byte2|=0x2C.
//   y: y=10001: c24=1,c25=0,c26=0,c27=0,c28=1
//      c24→byte3 bit7=0x80. c28→byte3 bit3=0x08.
//      byte3|=0x88.
//   g: g=01110: c30=0,c31=1,c32=1,c33=1,c34=0
//      c31→byte3 bit0=0x01. c32→byte4 bit7=0x80. c33→byte4 bit6=0x40.
//      byte3|=0x01. byte4|=0xC0.
//   o: o=01110: c36=0,c37=1,c38=1,c39=1,c40=0
//      c37→byte4 bit2=0x04. c38→byte4 bit1=0x02. c39→byte4 bit0=0x01.
//      byte4|=0x07.
//   t: t=01110: c42=0,c43=1,c44=1,c45=1,c46=0
//      c43→byte5 bit4=0x10. c44→byte5 bit3=0x08. c45→byte5 bit2=0x04.
//      byte5|=0x1C.
//   c: c=01110: c48=0,c49=1,c50=1,c51=1,c52=0
//      c49→byte6 bit6=0x40. c50→byte6 bit5=0x20. c51→byte6 bit4=0x10.
//      byte6|=0x70.
//   h: h=10110: c54=1,c55=0,c56=1,c57=1,c58=0
//      c54→byte6 bit1=0x02. c56→byte7 bit7=0x80. c57→byte7 bit6=0x40.
//      byte6|=0x02. byte7|=0xC0.
//   i: i=01100: c60=0,c61=1,c62=1,c63=0,c64=0
//      c61→byte7 bit2=0x04. c62→byte7 bit1=0x02.
//      byte7|=0x06.
//
//   ROW 2: Byte0=(0x80|0x01)=0x81, Byte1=(0xC0|0x07)=0xC7, Byte2=0x2C,
//           Byte3=(0x88|0x01)=0x89, Byte4=(0xC0|0x07)=0xC7, Byte5=0x1C,
//           Byte6=(0x70|0x02)=0x72, Byte7=(0xC0|0x06)=0xC6, Byte8=0x00
//
// ROW 3: F=11110, u=10001, r=10000, b=11001, y=10001, g=10001, o=10001, t=00100, c=10000, h=11001, i=00100
//   F: c0=1,c1=1,c2=1,c3=1,c4=0 → byte0: bits7,6,5,4 → 0xF0
//   u: c6=1,c7=0,c8=0,c9=0,c10=1 → c6→byte0 bit1=0x02; c10→byte1 bit5=0x20
//      byte0|=0x02. byte1|=0x20.
//   r: c12=1,c13=0,c14=0,c15=0,c16=0 → c12→byte1 bit3=0x08
//      byte1|=0x08.
//   b: c18=1,c19=1,c20=0,c21=0,c22=1 → c18→byte2 bit5=0x20; c19→byte2 bit4=0x10; c22→byte2 bit1=0x02
//      byte2|=0x32.
//   y: c24=1,c25=0,c26=0,c27=0,c28=1 → byte3 bit7=0x80, byte3 bit3=0x08
//      byte3|=0x88.
//   g: c30=1,c31=0,c32=0,c33=0,c34=1 → c30→byte3 bit1=0x02; c34→byte4 bit5=0x20
//      byte3|=0x02. byte4|=0x20.
//   o: c36=1,c37=0,c38=0,c39=0,c40=1 → c36→byte4 bit3=0x08; c40→byte5 bit7=0x80
//      byte4|=0x08. byte5|=0x80.
//   t: c42=0,c43=0,c44=1,c45=0,c46=0 → c44→byte5 bit3=0x08
//      byte5|=0x08.
//   c: c48=1,c49=0,c50=0,c51=0,c52=0 → c48→byte6 bit7=0x80
//      byte6|=0x80.
//   h: c54=1,c55=1,c56=0,c57=0,c58=1 → c54→byte6 bit1=0x02; c55→byte6 bit0=0x01; c58→byte7 bit5=0x20
//      byte6|=0x03. byte7|=0x20.
//   i: c60=0,c61=0,c62=1,c63=0,c64=0 → c62→byte7 bit1=0x02
//      byte7|=0x02.
//
//   ROW 3: Byte0=(0xF0|0x02)=0xF2, Byte1=(0x20|0x08)=0x28, Byte2=0x32,
//           Byte3=(0x88|0x02)=0x8A, Byte4=(0x20|0x08)=0x28, Byte5=(0x80|0x08)=0x88,
//           Byte6=(0x80|0x03)=0x83, Byte7=(0x20|0x02)=0x22, Byte8=0x00
//
// ROW 4: F=10000, u=10001, r=10000, b=10001, y=10001, g=10001, o=10001, t=00100, c=10000, h=10001, i=00100
//   F: c0=1→byte0 bit7=0x80
//   u: c6=1,c10=1→byte0 bit1=0x02; c10→byte1 bit5=0x20
//      byte0|=0x02. byte1|=0x20.
//   r: c12=1→byte1 bit3=0x08
//      byte1|=0x08.
//   b: c18=1,c22=1→byte2 bit5=0x20; c22→byte2 bit1=0x02
//      byte2|=0x22.
//   y: c24=1,c28=1→byte3 bit7=0x80; byte3 bit3=0x08
//      byte3|=0x88.
//   g: c30=1,c34=1→c30→byte3 bit1=0x02; c34→byte4 bit5=0x20
//      byte3|=0x02. byte4|=0x20.
//   o: c36=1,c40=1→c36→byte4 bit3=0x08; c40→byte5 bit7=0x80
//      byte4|=0x08. byte5|=0x80.
//   t: c44=1→byte5 bit3=0x08
//      byte5|=0x08.
//   c: c48=1→byte6 bit7=0x80
//      byte6|=0x80.
//   h: c54=1,c58=1→c54→byte6 bit1=0x02; c58→byte7 bit5=0x20
//      byte6|=0x02. byte7|=0x20.
//   i: c62=1→byte7 bit1=0x02
//      byte7|=0x02.
//
//   ROW 4: Byte0=(0x80|0x02)=0x82, Byte1=(0x20|0x08)=0x28, Byte2=0x22,
//           Byte3=(0x88|0x02)=0x8A, Byte4=(0x20|0x08)=0x28, Byte5=(0x80|0x08)=0x88,
//           Byte6=(0x80|0x02)=0x82, Byte7=(0x20|0x02)=0x22, Byte8=0x00
//
// ROW 5: F=10000, u=10011, r=10000, b=11001, y=01111, g=01111, o=10001, t=00110, c=10001, h=10001, i=00100
//   F: c0=1→0x80
//   u: u=10011: c6=1,c7=0,c8=0,c9=1,c10=1
//      c6→byte0 bit1=0x02; c9→byte1 bit6=0x40; c10→byte1 bit5=0x20
//      byte0|=0x02. byte1|=0x60.
//   r: c12=1→byte1 bit3=0x08
//      byte1|=0x08.
//   b: b=11001: c18=1,c19=1,c22=1
//      c18→byte2 bit5=0x20; c19→byte2 bit4=0x10; c22→byte2 bit1=0x02
//      byte2|=0x32.
//   y: y=01111: c24=0,c25=1,c26=1,c27=1,c28=1
//      c25→byte3 bit6=0x40; c26→byte3 bit5=0x20; c27→byte3 bit4=0x10; c28→byte3 bit3=0x08
//      byte3|=0x78.
//   g: g=01111: c30=0,c31=1,c32=1,c33=1,c34=1
//      c31→byte3 bit0=0x01; c32→byte4 bit7=0x80; c33→byte4 bit6=0x40; c34→byte4 bit5=0x20
//      byte3|=0x01. byte4|=0xE0.
//   o: o=10001: c36=1,c40=1
//      c36→byte4 bit3=0x08; c40→byte5 bit7=0x80
//      byte4|=0x08. byte5|=0x80.
//   t: t=00110: c42=0,c43=0,c44=1,c45=1,c46=0
//      c44→byte5 bit3=0x08; c45→byte5 bit2=0x04
//      byte5|=0x0C.
//   c: c=10001: c48=1,c52=1
//      c48→byte6 bit7=0x80; c52→byte6 bit3=0x08
//      byte6|=0x88.
//   h: h=10001: c54=1,c58=1
//      c54→byte6 bit1=0x02; c58→byte7 bit5=0x20
//      byte6|=0x02. byte7|=0x20.
//   i: i=00100: c62=1→byte7 bit1=0x02
//      byte7|=0x02.
//
//   ROW 5: Byte0=(0x80|0x02)=0x82, Byte1=(0x60|0x08)=0x68, Byte2=0x32,
//           Byte3=(0x78|0x01)=0x79, Byte4=(0xE0|0x08)=0xE8, Byte5=(0x80|0x0C)=0x8C,
//           Byte6=(0x88|0x02)=0x8A, Byte7=(0x20|0x02)=0x22, Byte8=0x00
//
// ROW 6: F=00000, u=01101, r=00000, b=10110, y=00001, g=01110, o=01110, t=00010, c=01110, h=00000, i=01110
//   F=00000: nothing
//   u: u=01101: c6=0,c7=1,c8=1,c9=0,c10=1
//      c7→byte0 bit0=0x01; c8→byte1 bit7=0x80; c10→byte1 bit5=0x20
//      byte0|=0x01. byte1|=0xA0.
//   r=00000: nothing
//   b: b=10110: c18=1,c19=0,c20=1,c21=1,c22=0
//      c18→byte2 bit5=0x20; c20→byte2 bit3=0x08; c21→byte2 bit2=0x04
//      byte2|=0x2C.
//   y: y=00001: c28=1→byte3 bit3=0x08
//      byte3|=0x08.
//   g: g=01110: c30=0,c31=1,c32=1,c33=1,c34=0
//      c31→byte3 bit0=0x01; c32→byte4 bit7=0x80; c33→byte4 bit6=0x40
//      byte3|=0x01. byte4|=0xC0.
//   o: o=01110: c36=0,c37=1,c38=1,c39=1,c40=0
//      c37→byte4 bit2=0x04; c38→byte4 bit1=0x02; c39→byte4 bit0=0x01
//      byte4|=0x07.
//   t: t=00010: c42=0,c43=0,c44=0,c45=1,c46=0
//      c45→byte5 bit2=0x04
//      byte5|=0x04.
//   c: c=01110: c48=0,c49=1,c50=1,c51=1,c52=0
//      c49→byte6 bit6=0x40; c50→byte6 bit5=0x20; c51→byte6 bit4=0x10
//      byte6|=0x70.
//   h=00000: nothing
//   i: i=01110: c60=0,c61=1,c62=1,c63=1,c64=0
//      c61→byte7 bit2=0x04; c62→byte7 bit1=0x02; c63→byte7 bit0=0x01
//      byte7|=0x07.
//
//   ROW 6: Byte0=0x01, Byte1=0xA0, Byte2=0x2C,
//           Byte3=(0x08|0x01)=0x09, Byte4=(0xC0|0x07)=0xC7, Byte5=0x04,
//           Byte6=0x70, Byte7=0x07, Byte8=0x00
//
// =============================================================================

static const uint8_t kFurbyLogo[] PROGMEM = {
    // ROW 0: F=01110, u=00000, r=00000, b=10000, y=00000, g=00000, o=00000, t=00100, c=00000, h=10000, i=00100
    0x70, 0x00, 0x20, 0x00, 0x00, 0x08, 0x02, 0x02, 0x00,
    // ROW 1: F=10000, u=00000, r=00000, b=10000, y=00000, g=00000, o=00000, t=00100, c=00000, h=10000, i=00000
    0x80, 0x00, 0x20, 0x00, 0x00, 0x08, 0x02, 0x00, 0x00,
    // ROW 2: F=10000, u=01110, r=01110, b=10110, y=10001, g=01110, o=01110, t=01110, c=01110, h=10110, i=01100
    0x81, 0xC7, 0x2C, 0x89, 0xC7, 0x1C, 0x72, 0xC6, 0x00,
    // ROW 3: F=11110, u=10001, r=10000, b=11001, y=10001, g=10001, o=10001, t=00100, c=10000, h=11001, i=00100
    0xF2, 0x28, 0x32, 0x8A, 0x28, 0x88, 0x83, 0x22, 0x00,
    // ROW 4: F=10000, u=10001, r=10000, b=10001, y=10001, g=10001, o=10001, t=00100, c=10000, h=10001, i=00100
    0x82, 0x28, 0x22, 0x8A, 0x28, 0x88, 0x82, 0x22, 0x00,
    // ROW 5: F=10000, u=10011, r=10000, b=11001, y=01111, g=01111, o=10001, t=00110, c=10001, h=10001, i=00100
    0x82, 0x68, 0x32, 0x79, 0xE8, 0x8C, 0x8A, 0x22, 0x00,
    // ROW 6: F=00000, u=01101, r=00000, b=10110, y=00001, g=01110, o=01110, t=00010, c=01110, h=00000, i=01110
    0x01, 0xA0, 0x2C, 0x09, 0xC7, 0x04, 0x70, 0x07, 0x00,
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
            int byteIdx = row * 9 + (col / 8);
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

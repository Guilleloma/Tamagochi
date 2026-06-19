/**
 * jd9853_panel.h — Init sequence y subclase Arduino_JD9853 compartida.
 *
 * Extraída de main_phase2.cpp para que tanto el harness de verificación
 * (main_phase2.cpp) como el main real del juego (main_esp32.cpp) puedan
 * instanciar el panel sin duplicar la secuencia de init nativa.
 *
 * Uso:
 *   #include "jd9853_panel.h"
 *   // Declarar bus y canvas como en el harness:
 *   static Arduino_DataBus* tftBus = makeTftBus();
 *   static Arduino_GFX*     tft   = makeJD9853(tftBus);
 *   static Arduino_Canvas*  canvas = new Arduino_Canvas(240, 296, tft);
 *
 * Solo se incluye en targets ESP32-S3 (Arduino_GFX disponible).
 */

#pragma once

#include <Arduino_GFX_Library.h>

// ─── GPIO de la pantalla (Waveshare ESP32-S3 Zero + JD9853 240×296) ───────────
#ifndef TFT_SCLK
  #define TFT_SCLK 12
#endif
#ifndef TFT_MOSI
  #define TFT_MOSI 11
#endif
#ifndef TFT_RST
  #define TFT_RST  10
#endif
#ifndef TFT_DC
  #define TFT_DC    9
#endif
#ifndef TFT_CS
  #define TFT_CS    8
#endif
#ifndef TFT_BL
  #define TFT_BL    7
#endif

// ─── Secuencia de init nativa JD9853 ─────────────────────────────────────────
// Documentada por reverse-engineering de la pantalla IPS 240×296 de Waveshare.
// NO es la secuencia estándar ST7789 — hay parámetros de gamma y timings específicos.
static const uint8_t kJD9853InitOps[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8,  0xDE, 0x00,
    WRITE_C8_D8,  0xB2, 0x23,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,  0x00, 0x47, 0x00, 0x6F,
    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,  0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8,  0xC1, 0x16,
    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,  0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A,
                     0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,  0x04, 0x06, 0x6B, 0x0F, 0x00,
    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8,  0x3A, 0x05,
    WRITE_C8_D8,  0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    WRITE_COMMAND_8, 0x29,
    END_WRITE,
    DELAY, 10,
};

// ─── Subclase Arduino_JD9853 ──────────────────────────────────────────────────
// Hereda de Arduino_ST7789 porque el controlador JD9853 es compatible en
// protocolo SPI y comandos MADCTL/COLMOD, pero usa una secuencia de init
// distinta (EXTC unlock 0xDF + parámetros de gamma propios).
class Arduino_JD9853 : public Arduino_ST7789 {
public:
    Arduino_JD9853(Arduino_DataBus* bus, int8_t rst, uint8_t r, bool ips,
                   int16_t w, int16_t h,
                   uint8_t co1, uint8_t ro1, uint8_t co2, uint8_t ro2)
        : Arduino_ST7789(bus, rst, r, ips, w, h, co1, ro1, co2, ro2) {}

    /**
     * clearNativePanel — limpia el área FÍSICA completa del panel (240×320)
     * escribiendo directamente al GRAM sin offsets.
     *
     * Por qué es necesario: el panel físico tiene 320 líneas de GRAM, pero
     * el canvas solo ocupa 296 px con row_offset=12 (filas 12-307 del GRAM).
     * Las filas 0-11 y 308-319 nunca se escriben en operación normal y
     * contienen basura aleatoria que asoma por las esquinas redondeadas del
     * cristal. Llamar a este método UNA VEZ en setup(), tras tft->begin(),
     * pone todo el GRAM al color de fondo y elimina el ruido visible.
     *
     * Se envían los comandos CASET/RASET/RAMWR directamente a través del bus
     * (sin pasar por writeAddrWindow) para saltarse los offsets _xStart/_yStart
     * que el driver suma automáticamente.
     */
    void clearNativePanel(uint16_t color) {
        static constexpr uint16_t NATIVE_W = 240;
        static constexpr uint16_t NATIVE_H = 320;

        _bus->beginWrite();
        // CASET (0x2A): columnas 0..239 del panel nativo
        _bus->writeC8D16D16(0x2A, 0, NATIVE_W - 1);
        // RASET (0x2B): filas 0..319 del panel nativo (incluye las muertas 0-11 y 308-319)
        _bus->writeC8D16D16(0x2B, 0, NATIVE_H - 1);
        // RAMWR (0x2C): iniciar escritura al GRAM
        _bus->writeCommand(0x2C);
        // Escribir NATIVE_W × NATIVE_H píxeles del color indicado
        _bus->writeRepeat(color, static_cast<uint32_t>(NATIVE_W) * NATIVE_H);
        _bus->endWrite();
    }

protected:
    void tftInit() override {
        // Reset hardware: HIGH → LOW → HIGH con los delays especificados en la datasheet.
        if (_rst != GFX_NOT_DEFINED) {
            pinMode(_rst, OUTPUT);
            digitalWrite(_rst, HIGH); delay(100);
            digitalWrite(_rst, LOW);  delay(120);
            digitalWrite(_rst, HIGH); delay(120);
        }
        // Software reset antes de la secuencia de init.
        _bus->sendCommand(0x01);  // SWRESET
        delay(120);
        _bus->batchOperation(kJD9853InitOps, sizeof(kJD9853InitOps));
    }
};

// ─── Funciones de fábrica ─────────────────────────────────────────────────────

/**
 * Crea el bus SPI hardware ESP32.
 * El caller es propietario del puntero (no se libera — lifetime de la aplicación).
 */
inline Arduino_DataBus* makeTftBus() {
    return new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
}

/**
 * Crea el objeto GFX del panel JD9853 240×296 IPS.
 * col_offset1/2 = 0 y row_offset1/2 = 12: desplazamiento físico de la pantalla
 * respecto al controlador (12 filas vacías arriba y abajo en el panel de 320 líneas).
 */
inline Arduino_GFX* makeJD9853(Arduino_DataBus* bus) {
    return new Arduino_JD9853(bus, TFT_RST, 0, /*ips=*/true,
                               240, 296,
                               /*col_offset1=*/0, /*row_offset1=*/12,
                               /*col_offset2=*/0, /*row_offset2=*/12);
}

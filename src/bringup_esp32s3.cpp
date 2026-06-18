/**
 * bringup_esp32s3.cpp — FASE 1: sprites reales desde LittleFS / PSRAM
 *
 * Hardware:
 *   MCU  : Waveshare ESP32-S3 Zero (ESP32-S3FH4R2, 4MB QIO flash, 2MB PSRAM quad)
 *   Panel: JD9853 240×296 IPS 3.3V (init compatible con ST7789 IPS)
 *
 * Cableado:
 *   SCL/SCK → GPIO12   SDA/MOSI → GPIO11   RES/RST → GPIO10
 *   DC      → GPIO9    CS       → GPIO8    BL      → GPIO7
 *   VCC → 3V3, GND → GND, MISO no conectado.
 *
 * INTENTO B (activo): init nativa JD9853 via subclase Arduino_JD9853.
 *
 * FASE 1 añade:
 *   - SpriteStore: parsea manifest.json en LittleFS, carga frames en PSRAM.
 *   - loop() anima la animación "walk" del mago (8 frames, 8 fps).
 *   - Si loadAnimation falla, muestra mensaje de error en pantalla y Serial.
 *
 * DOUBLE BUFFER (sin parpadeo):
 *   - Arduino_Canvas (240×296, RGB565) alojado en PSRAM via ps_malloc().
 *   - Todo el dibujo (fondo negro, header, sprite escalado) se hace sobre el
 *     canvas en memoria. Al final del frame se llama canvas->flush(), que hace
 *     un único push del framebuffer completo al panel JD9853 via SPI.
 *   - El panel nunca ve el "borrado" y el "redibujado" por separado → sin parpadeo.
 *
 * Compilar:  pio run -e esp32s3
 * Subir FS:  pio run -e esp32s3 -t uploadfs
 * Flashear:  pio run -e esp32s3 -t upload
 *   Si falla: mantén BOOT → pulsa RESET → suelta BOOT → vuelve a lanzar upload.
 */

#include <Arduino_GFX_Library.h>
#include "SpriteStore.h"


// ─── GPIO ────────────────────────────────────────────────────────────────────
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_RST  10
#define TFT_DC    9
#define TFT_CS    8
#define TFT_BL    7

// ─── Secuencia de init nativa JD9853 ─────────────────────────────────────────
//
// Estructura del array (interpretada por Arduino_DataBus::batchOperation):
//   BEGIN_WRITE / END_WRITE  → abre/cierra transacción SPI
//   WRITE_COMMAND_8, 0xXX    → envía 1 byte de comando
//   WRITE_C8_D8,  cmd, d0    → comando + 1 byte dato
//   WRITE_C8_D16, cmd, d0, d1 → comando + 2 bytes dato (MSB primero)
//   WRITE_BYTES, N, d0…dN-1  → N bytes de datos (sin comando previo)
//   DELAY, ms                → espera milisegundos
//
// Fuente de la secuencia: datasheet JD9853 + notas de bring-up de comunidad.
// El comando de gamma 0xC8 (32 bytes de fábrica) está OMITIDO — sin él
// se aplica la gamma lineal por defecto, suficiente para ver imagen.
//
static const uint8_t jd9853_init_operations[] = {

    // 1. Sleep Out — el panel arranca en sleep; hay que sacarlo antes de configurar.
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,   // SLPOUT
    END_WRITE,
    DELAY, 120,              // datasheet: esperar ≥120 ms tras SLPOUT

    BEGIN_WRITE,

    // 2. Unlock de registros extendidos JD9853.
    //    Sin esto el panel ignora todos los comandos propietarios (0xB2, 0xBB…).
    //    0xDF es el unlock command; {0x98, 0x53} es la clave de fabricante.
    WRITE_C8_D16, 0xDF, 0x98, 0x53,

    // 3. Selección de página de registros (0 = página principal).
    WRITE_C8_D8,  0xDE, 0x00,

    // 4. Configuración de frame rate / display timing.
    WRITE_C8_D8,  0xB2, 0x23,

    // 5. Gate/Source timing (4 bytes).
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,  0x00, 0x47, 0x00, 0x6F,

    // 6. Power control — tensiones VCOM, VREG, etc. (6 bytes).
    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,  0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    // 7. VCOM voltaje.
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,

    // 8. Power control 2.
    WRITE_C8_D8,  0xC1, 0x16,

    // 9. Timing control interna (8 bytes).
    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,  0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    // 10. Source driver timing (12 bytes).
    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A,
                     0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    // 11. Display output control (5 bytes).
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,  0x04, 0x06, 0x6B, 0x0F, 0x00,

    // 12. Display timing ajuste.
    WRITE_C8_D16, 0xD7, 0x00, 0x30,

    // 13. Pixel format: 0x05 = 16bpp RGB565.
    WRITE_C8_D8,  0x3A, 0x05,

    // 14. MADCTL: rotación 0°, orden RGB (sin mirror, sin swap).
    WRITE_C8_D8,  0x36, 0x00,

    // 15. Invert ON — necesario en paneles IPS para colores correctos.
    //     (equivale al flag ips=true del constructor ST7789)
    WRITE_COMMAND_8, 0x21,

    // 16. Display ON.
    WRITE_COMMAND_8, 0x29,

    END_WRITE,
    DELAY, 10,
};

// ─── Subclase inline: Arduino_JD9853 ─────────────────────────────────────────
//
// Hereda Arduino_ST7789 íntegramente (setRotation, writeAddrWindow, offsets,
// SPI MODE3, etc.) pero sobreescribe tftInit() para inyectar la init nativa
// antes de que el framework configure COLMOD y MADCTL.
//
// Por qué no tocar la librería directamente:
//   - Mantener lib_deps con tag fijo (reproducibilidad).
//   - El sketch es el único artefacto que cambia entre intentos de diagnóstico.
//
class Arduino_JD9853 : public Arduino_ST7789
{
public:
    // Mismo constructor que Arduino_ST7789 — reenvía todos los parámetros.
    Arduino_JD9853(
        Arduino_DataBus *bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0,
        bool ips = false, int16_t w = 240, int16_t h = 320,
        uint8_t col_offset1 = 0, uint8_t row_offset1 = 0,
        uint8_t col_offset2 = 0, uint8_t row_offset2 = 0)
        : Arduino_ST7789(bus, rst, r, ips, w, h,
                         col_offset1, row_offset1, col_offset2, row_offset2)
    {}

protected:
    // tftInit() se llama desde Arduino_TFT::begin() justo después del reset
    // de hardware y antes de configurar la ventana de dirección.
    // Aquí reemplazamos la secuencia ST7789 estándar por la nativa JD9853.
    void tftInit() override
    {
        // Reset hardware (igual que Arduino_ST7789::tftInit).
        if (_rst != GFX_NOT_DEFINED) {
            pinMode(_rst, OUTPUT);
            digitalWrite(_rst, HIGH);
            delay(100);
            digitalWrite(_rst, LOW);
            delay(120);   // ST7789_RST_DELAY
            digitalWrite(_rst, HIGH);
            delay(120);
        }

        // Software reset como seguro adicional.
        _bus->sendCommand(0x01); // SWRESET
        delay(120);

        // Init nativa JD9853 via batchOperation.
        // El array está definido en el scope de archivo como static const.
        _bus->batchOperation(jd9853_init_operations, sizeof(jd9853_init_operations));

        // invertDisplay(false) llamada desde Arduino_ST7789::tftInit() original
        // queda omitida intencionalmente: ya enviamos 0x21 (INVON) en el array,
        // que es el equivalente correcto para IPS. Una llamada a invertDisplay(false)
        // aquí enviaría 0x20 (INVOFF) y deshacería el efecto.
    }
};

// ─── Construcción del bus y display ──────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED /*MISO*/
);

// INTENTO B activo: Arduino_JD9853 con init nativa.
// Parámetros:
//   bus, TFT_RST, rotation=0, IPS=true (necesario para colores correctos),
//   240×296 (resolución real del panel),
//   col_off1=0, row_off1=12, col_off2=0, row_off2=12
//     → 296 px de ventana centrados en los 320 px del glass (24px totales / 2 = 12).
//
// CALIBRACIÓN VISUAL con drawRect(0,0,239,295,WHITE):
//   - Marco pegado arriba y abajo → row_off1=12 correcto.
//   - Marco desplazado hacia arriba → aumentar row_off1 (probar 0, 12, 24).
//   - Marco desplazado hacia abajo  → disminuir row_off1 (probar 0).
Arduino_GFX *gfx = new Arduino_JD9853(
    bus,
    TFT_RST,
    0,            // rotation
    true,         // IPS = true
    240, 296,
    0,  12,       // col_off1, row_off1
    0,  12        // col_off2, row_off2
);

// ─── Canvas (double buffer en PSRAM) ─────────────────────────────────────────
//
// Arduino_Canvas mantiene un framebuffer RGB565 del tamaño de la pantalla.
// En ESP32 con PSRAM detectada, begin() lo aloja con ps_malloc() →
// los 240×296×2 = 141.6 KB van a PSRAM, liberando SRAM interna.
//
// flush() llama internamente a gfx->draw16bitRGBBitmap(), que envía el
// buffer completo al panel JD9853 en un único bloque SPI. El panel solo
// ve el frame final, ya compuesto — sin parpadeo.
//
Arduino_Canvas *canvas = new Arduino_Canvas(240, 296, gfx);

// ─── SpriteStore + animación walk del mago ────────────────────────────────────

// ID del sprite exportado por Pixel Sprites.
// Coincide con el campo "id" en manifest.json y con el nombre de carpeta
// en LittleFS (/sprites/0e273289-.../walk/0.bin, etc.).
static const char* SPRITE_ID = "0e273289-be9b-4048-a2c3-acb89579dcdc";

// Color key: 0xF81F = magenta = transparente (definido en manifest.colorKey).
static const uint16_t KEY_COLOR = 0xF81F;

// SpriteStore y frames walk en PSRAM.
static SpriteStore store;
static ColorSpriteFrames walkAnim;

// Flag: si el loader falló, loop() muestra un mensaje de error estático.
static bool spriteLoadOk = false;

// ─── Helper: escala nearest-neighbor con color key (transparente) ─────────────
/**
 * Dibuja un sprite RGB565 escalado nearest-neighbor sobre un destino Arduino_GFX.
 * Cuando el destino es Arduino_Canvas, todas las escrituras van al framebuffer
 * en memoria (PSRAM) — no al panel. El panel solo se actualiza con flush().
 *
 * @param display  Puntero al objeto Arduino_GFX (canvas o panel directo).
 * @param destX    Coordenada X de destino (esquina superior izquierda).
 * @param destY    Coordenada Y de destino.
 * @param srcW     Ancho del sprite fuente en píxeles.
 * @param srcH     Alto del sprite fuente en píxeles.
 * @param pixels   Array RGB565 del sprite (srcW * srcH elementos).
 * @param scale    Factor de escala entero (p.ej. 4 → 48×48 se convierte en 192×192).
 * @param keyColor Color a tratar como transparente (se salta sin pintar).
 */
static void drawSpriteScaled(
    Arduino_GFX *display,
    int16_t destX, int16_t destY,
    uint16_t srcW, uint16_t srcH,
    const uint16_t *pixels,
    uint8_t scale,
    uint16_t keyColor)
{
    for (uint16_t sy = 0; sy < srcH; sy++) {
        for (uint16_t sx = 0; sx < srcW; sx++) {
            uint16_t color = pixels[sy * srcW + sx];
            if (color == keyColor) continue; // transparente — saltar
            display->fillRect(
                destX + sx * scale,
                destY + sy * scale,
                scale, scale,
                color
            );
        }
    }
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(500); // esperar a que USB CDC esté listo (ESP32-S3 CDC necesita ~300ms)
    Serial.println("\n[BRINGUP] ESP32-S3 + JD9853 240x296 — FASE 1 / double buffer PSRAM");

    // Diagnóstico de memoria antes de init
    Serial.printf("[MEM] Free heap  : %u bytes\n", ESP.getFreeHeap());
    Serial.printf("[MEM] Free PSRAM : %u bytes\n", ESP.getFreePsram());

    // Backlight ON
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    Serial.println("[TFT] Backlight HIGH");

    // ── Inicializar el panel JD9853 @ 27 MHz ─────────────────────────────────
    // IMPORTANTE: se inicializa gfx (el panel físico) directamente, NO el canvas.
    // El canvas se inicializa después con GFX_SKIP_OUTPUT_BEGIN para evitar
    // que llame a gfx->begin() una segunda vez y re-ejecute la init JD9853.
    if (!gfx->begin(27000000)) {
        Serial.println("[TFT] ERROR: gfx->begin() fallo — revisar cableado");
        while (true) { delay(1000); }
    }
    Serial.println("[TFT] gfx->begin() OK — init JD9853 nativa enviada");

    // ── Inicializar el canvas — framebuffer va a PSRAM ───────────────────────
    // GFX_SKIP_OUTPUT_BEGIN (-2): canvas.begin() reserva el framebuffer pero
    // no llama a gfx->begin() de nuevo (el panel ya está inicializado arriba).
    // En ESP32 con PSRAM detectada, ps_malloc() asigna los ~142 KB en PSRAM.
    uint32_t psramBefore = ESP.getFreePsram();
    if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.println("[CANVAS] ERROR: canvas->begin() fallo — sin memoria");
        while (true) { delay(1000); }
    }
    uint32_t psramAfter = ESP.getFreePsram();
    Serial.printf("[CANVAS] Framebuffer OK — PSRAM antes: %u B, despues: %u B, consumo: %u B\n",
                  psramBefore, psramAfter, psramBefore - psramAfter);
    Serial.printf("[CANVAS] Esperado: %u B (240*296*2)\n", 240 * 296 * 2);

    // ── Dibujo inicial sobre el canvas (se muestra tras flush) ───────────────
    canvas->fillScreen(BLACK);
    canvas->drawRect(0, 0, 239, 295, WHITE);

    canvas->setTextColor(WHITE);
    canvas->setTextSize(2);
    canvas->setCursor(20, 20);
    canvas->print("JD9853 OK");

    canvas->setTextSize(1);
    canvas->setCursor(20, 50);
    canvas->printf("Heap:  %u B", ESP.getFreeHeap());
    canvas->setCursor(20, 62);
    canvas->printf("PSRAM: %u B", ESP.getFreePsram());

    // Primer flush: muestra la pantalla inicial sin parpadeo
    canvas->flush();
    Serial.println("[SETUP] Pantalla inicial dibujada (via canvas flush)");

    // ── Cargar sprites desde LittleFS → PSRAM ────────────────────────────────
    Serial.println("[SETUP] Iniciando SpriteStore...");

    if (!store.begin("/sprites/manifest.json")) {
        // LittleFS no montó o manifest roto — mostrar error visible en pantalla
        canvas->fillScreen(BLACK);
        canvas->setTextColor(RED);
        canvas->setTextSize(1);
        canvas->setCursor(10, 100);
        canvas->print("ERROR: LittleFS / manifest");
        canvas->setCursor(10, 115);
        canvas->print("Haz: pio -t uploadfs");
        canvas->flush();
        Serial.println("[SETUP] FATAL: SpriteStore.begin() fallo — ver Serial arriba");
        spriteLoadOk = false;
        return;
    }

    if (!store.loadAnimation(SPRITE_ID, "walk", walkAnim)) {
        canvas->fillScreen(BLACK);
        canvas->setTextColor(RED);
        canvas->setTextSize(1);
        canvas->setCursor(10, 100);
        canvas->print("ERROR: no se cargo walk");
        canvas->setCursor(10, 115);
        canvas->printf("ID: %.20s...", SPRITE_ID);
        canvas->flush();
        Serial.println("[SETUP] FATAL: loadAnimation('walk') fallo — ver Serial arriba");
        spriteLoadOk = false;
        return;
    }

    spriteLoadOk = true;
    Serial.printf("[SETUP] walk cargado: %u frames %ux%u @%u fps. PSRAM libre: %u B\n",
                  walkAnim.count, walkAnim.w, walkAnim.h, walkAnim.fps,
                  ESP.getFreePsram());

    // ── Dibujar frame 0 en el canvas y hacer flush ───────────────────────────
    // Escala: 48×48 × 4 = 192×192 px — centrado en la pantalla 240×296
    const uint8_t SCALE = 4;
    const int16_t spriteScreenW = (int16_t)(walkAnim.w * SCALE);
    const int16_t spriteScreenH = (int16_t)(walkAnim.h * SCALE);
    const int16_t spriteX       = (240 - spriteScreenW) / 2;
    const int16_t spriteY       = (296 - spriteScreenH) / 2;

    // Redibuja el canvas completo: fondo + header actualizado + frame 0
    canvas->fillScreen(BLACK);
    canvas->setTextColor(WHITE);
    canvas->setTextSize(2);
    canvas->setCursor(20, 20);
    canvas->print("JD9853 OK");
    canvas->setTextSize(1);
    canvas->setCursor(20, 50);
    canvas->printf("Heap:  %u B", ESP.getFreeHeap());
    canvas->setCursor(20, 62);
    canvas->printf("PSRAM: %u B (post-load)", ESP.getFreePsram());

    drawSpriteScaled(canvas, spriteX, spriteY, walkAnim.w, walkAnim.h,
                     walkAnim.frames[0], SCALE, KEY_COLOR);
    canvas->flush();

    Serial.println("[SETUP] frame 0 dibujado — entrando en loop de animacion");
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void loop()
{
    // Si el loader falló en setup(), no hay nada que animar.
    // El mensaje de error ya está en pantalla (vía canvas flush en setup).
    if (!spriteLoadOk) {
        static uint32_t lastBeat = 0;
        if (millis() - lastBeat >= 2000) {
            lastBeat = millis();
            Serial.println("[LOOP] sprite no cargado — loop inactivo. "
                           "Revisa LittleFS (pio -t uploadfs) y reflashea.");
        }
        return;
    }

    static uint8_t  currentFrame = 0;
    static uint32_t lastSwap     = 0;
    static uint32_t lastBeat     = 0;

    // fps declarado en el manifest (8 fps = 125 ms/frame)
    const uint32_t frameMs = (walkAnim.fps > 0) ? (1000u / walkAnim.fps) : 125u;

    // Escala: 48×48 × 4 = 192×192 px — centrado en la pantalla 240×296
    const uint8_t  SCALE         = 4;
    const int16_t  spriteScreenW = (int16_t)(walkAnim.w * SCALE);
    const int16_t  spriteScreenH = (int16_t)(walkAnim.h * SCALE);
    const int16_t  spriteX       = (240 - spriteScreenW) / 2;
    const int16_t  spriteY       = (296 - spriteScreenH) / 2;

    // Heartbeat: confirma que loop() corre correctamente
    if (millis() - lastBeat >= 1000) {
        lastBeat = millis();
        Serial.printf("[HB] t=%lu ms  frame=%u/%u  heap=%u  psram=%u\n",
                      millis(), currentFrame, walkAnim.count,
                      ESP.getFreeHeap(), ESP.getFreePsram());
    }

    // Avanzar frame al ritmo del fps declarado en el manifest
    if (millis() - lastSwap >= frameMs) {
        lastSwap = millis();

        // ── Componer frame completo en el canvas (todo en PSRAM, sin tocar el panel) ──

        // 1. Fondo negro — borra el frame anterior
        canvas->fillScreen(BLACK);

        // 2. Header de estado (barato, redibujado cada frame para simplicidad)
        canvas->setTextColor(WHITE);
        canvas->setTextSize(2);
        canvas->setCursor(20, 20);
        canvas->print("JD9853 OK");
        canvas->setTextSize(1);
        canvas->setCursor(20, 50);
        canvas->printf("Heap:  %u B", ESP.getFreeHeap());
        canvas->setCursor(20, 62);
        canvas->printf("PSRAM: %u B", ESP.getFreePsram());

        // 3. Frame actual del sprite — nearest-neighbor x4, con color key
        drawSpriteScaled(canvas, spriteX, spriteY,
                         walkAnim.w, walkAnim.h,
                         walkAnim.frames[currentFrame],
                         SCALE, KEY_COLOR);

        // 4. Flush: push del framebuffer completo al panel en un único bloque SPI.
        //    El panel solo recibe el frame ya compuesto — sin borrado visible.
        canvas->flush();

        // Avanzar al siguiente frame (loop circular)
        currentFrame = (currentFrame + 1) % walkAnim.count;
    }
}

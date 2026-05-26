// =============================================================================
// Tamagochi — Fase 0: Bring-up
//
// Objetivos de esta fase:
//   1. "Hola Tamagochi" en OLED con sprite dummy 32x32 animado
//   2. Log por serie de pulsaciones GPIO0 (short / long / double)
//   3. Medición de FPS real cada 5s para validar budget de render
//
// NO incluye: WiFi, persistencia, stats, FSM, nada de Fase 1+.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <OneButton.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Display — SSD1306 128x64 I2C, full buffer (1KB en RAM).
// Full buffer permite drawXBMP sin restricciones de zona de página.
// El constructor HW_I2C delega en Wire, que configuramos manualmente con
// Wire.begin(SDA, SCL) para poder verificar los pines en Fase 0.
// U8X8_PIN_NONE = sin pin RESET dedicado (el SSD1306 de ideaspark no lo expone).
// -----------------------------------------------------------------------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// -----------------------------------------------------------------------------
// Botón — OneButton gestiona debounce, short, long y double click.
// true = INPUT_PULLUP (redundante pero explícito; la placa ya tiene pull-up HW).
// -----------------------------------------------------------------------------
OneButton button(PIN_BUTTON, true);

// -----------------------------------------------------------------------------
// Sprite dummy 32x32 — formato XBM (1 bit por pixel, byte-packed, LSB first).
// Dos frames: frame 0 = cara feliz, frame 1 = cara con ojos cerrados (parpadeo).
// El diseño no pretende ser bonito — sirve para validar drawXBMP y medir FPS.
//
// En XBM, bit=0 → pixel activo (blanco en OLED normal), bit=1 → apagado.
// Los bytes se leen de izquierda a derecha, LSB primero.
// -----------------------------------------------------------------------------

// Frame 0: blob redondo con ojos abiertos y sonrisa
static const unsigned char PROGMEM kSpriteFrame0[128] = {
    0x00, 0xFF, 0xFF, 0x00,  // fila 0
    0x80, 0xFF, 0xFF, 0x01,  // fila 1
    0xC0, 0xFF, 0xFF, 0x03,  // fila 2
    0xE0, 0xFF, 0xFF, 0x07,  // fila 3
    0xF0, 0xFF, 0xFF, 0x0F,  // fila 4
    0xF8, 0xFF, 0xFF, 0x1F,  // fila 5
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 6
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 7
    0xFE, 0x99, 0x99, 0x7F,  // fila 8  — ojos (huecos en 0x99)
    0xFE, 0x99, 0x99, 0x7F,  // fila 9
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 10
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 11
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 12
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 13
    0xFF, 0xC3, 0xC3, 0xFF,  // fila 14 — sonrisa (huecos en 0xC3)
    0xFF, 0xE7, 0xE7, 0xFF,  // fila 15
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 16
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 17
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 18
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 19
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 20
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 21
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 22
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 23
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 24
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 25
    0xF8, 0xFF, 0xFF, 0x1F,  // fila 26
    0xF0, 0xFF, 0xFF, 0x0F,  // fila 27
    0xE0, 0xFF, 0xFF, 0x07,  // fila 28
    0xC0, 0xFF, 0xFF, 0x03,  // fila 29
    0x80, 0xFF, 0xFF, 0x01,  // fila 30
    0x00, 0xFF, 0xFF, 0x00   // fila 31
};

// Frame 1: blob con ojos cerrados (líneas horizontales)
static const unsigned char PROGMEM kSpriteFrame1[128] = {
    0x00, 0xFF, 0xFF, 0x00,
    0x80, 0xFF, 0xFF, 0x01,
    0xC0, 0xFF, 0xFF, 0x03,
    0xE0, 0xFF, 0xFF, 0x07,
    0xF0, 0xFF, 0xFF, 0x0F,
    0xF8, 0xFF, 0xFF, 0x1F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFE, 0x01, 0x80, 0x7F,  // fila 8  — ojos cerrados (línea horizontal)
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 9
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC3, 0xC3, 0xFF,  // sonrisa igual
    0xFF, 0xE7, 0xE7, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFE, 0xFF, 0xFF, 0x7F,
    0xFE, 0xFF, 0xFF, 0x7F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xF8, 0xFF, 0xFF, 0x1F,
    0xF0, 0xFF, 0xFF, 0x0F,
    0xE0, 0xFF, 0xFF, 0x07,
    0xC0, 0xFF, 0xFF, 0x03,
    0x80, 0xFF, 0xFF, 0x01,
    0x00, 0xFF, 0xFF, 0x00
};

// Tabla de frames para iterar por índice
static const unsigned char* const kSpriteFrames[2] = {
    kSpriteFrame0,
    kSpriteFrame1
};

// -----------------------------------------------------------------------------
// Estado de animación y FPS
// -----------------------------------------------------------------------------
static uint8_t  currentAnimFrame  = 0;
static uint32_t lastRenderMs      = 0;
static uint32_t lastAnimMs        = 0;
static uint32_t lastFpsReportMs   = 0;
static uint32_t framesRendered    = 0;

// -----------------------------------------------------------------------------
// Callbacks del botón — imprimen por serie el evento detectado.
// Los mensajes en español facilitan la verificación en campo.
// -----------------------------------------------------------------------------
static void onShortClick() {
    Serial.println("[BOTON] Click corto");
}

static void onDoubleClick() {
    Serial.println("[BOTON] Doble click");
}

static void onLongPress() {
    Serial.println("[BOTON] Pulsacion larga");
}

// -----------------------------------------------------------------------------
// renderFrame — dibuja el sprite centrado en la mitad derecha del OLED
// y el texto "Hola Tamagochi / Fase 0" a la izquierda.
// Posición sprite: x=96 hace que quede centrado verticalmente en la derecha
// (128 - 32 = 96 → x=48 para centro absoluto; a la derecha x=96).
// -----------------------------------------------------------------------------
static void renderFrame(uint8_t frameIndex) {
    u8g2.clearBuffer();

    // Texto identificador de fase
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "Hola Tamagochi");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 24, "Fase 0 - Bring-up");

    // Sprite 32x32 alineado a la derecha
    // drawXBMP(x, y, w, h, bitmap)
    u8g2.drawXBMP(96, 16, 32, 32, kSpriteFrames[frameIndex]);

    u8g2.sendBuffer();
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("[BOOT] Tamagochi - Fase 0 iniciando...");

    // I2C explícito con los pines de config.h.
    // Si el OLED no aparece, cambiar OLED_SDA/SCL en config.h y reflashear.
    Wire.begin(OLED_SDA, OLED_SCL);
    Serial.printf("[BOOT] I2C iniciado en SDA=GPIO%d SCL=GPIO%d\n", OLED_SDA, OLED_SCL);

    if (!u8g2.begin()) {
        // En caso de fallo (I2C no responde), loguear y seguir —
        // el OLED puede estar en pines distintos en esta revisión de placa.
        Serial.println("[ERROR] u8g2.begin() fallo - verificar pines I2C en config.h");
    } else {
        Serial.println("[BOOT] OLED SSD1306 inicializado correctamente");
    }

    // Pantalla de inicio inmediata para confirmar visualmente que funciona
    renderFrame(0);

    // Configuración del botón GPIO0
    // INPUT_PULLUP explícito aunque la placa ya tiene pull-up hardware —
    // esto protege si se usa la placa sin la resistencia de arranque.
    button.attachClick(onShortClick);
    button.attachDoubleClick(onDoubleClick);
    button.attachLongPressStart(onLongPress);
    Serial.printf("[BOOT] Boton configurado en GPIO%d (INPUT_PULLUP)\n", PIN_BUTTON);

    // Inicializar temporizadores
    uint32_t now = millis();
    lastRenderMs    = now;
    lastAnimMs      = now;
    lastFpsReportMs = now;

    Serial.println("[BOOT] Listo. Escuchando botones y renderizando...");
}

// =============================================================================
// loop
// =============================================================================
void loop() {
    // OneButton necesita tick() en cada iteración para detectar gestos correctamente.
    // No usar delays en el loop por esta misma razón.
    button.tick();

    uint32_t now = millis();

    // Render a 5 FPS (cada RENDER_FRAME_MS)
    if (now - lastRenderMs >= RENDER_FRAME_MS) {
        renderFrame(currentAnimFrame);
        framesRendered++;
        lastRenderMs = now;
    }

    // Alternar frame de animación cada ANIM_FRAME_MS (independiente del render)
    if (now - lastAnimMs >= ANIM_FRAME_MS) {
        currentAnimFrame = (currentAnimFrame + 1) % 2;
        lastAnimMs = now;
    }

    // Reporte de FPS real cada FPS_REPORT_MS
    if (now - lastFpsReportMs >= FPS_REPORT_MS) {
        float elapsedSeconds = (now - lastFpsReportMs) / 1000.0f;
        float fps = framesRendered / elapsedSeconds;
        Serial.printf("[FPS] %.2f fps (%lu frames en %.1fs)\n",
                      fps, framesRendered, elapsedSeconds);
        framesRendered    = 0;
        lastFpsReportMs   = now;
    }

    // yield() cede el control al scheduler del ESP8266 (watchdog, TCP stack...).
    // Crítico en loops no bloqueantes para evitar soft WDT reset.
    yield();
}

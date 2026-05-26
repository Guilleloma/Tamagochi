// =============================================================================
// Tamagochi — Fase 0: Bring-up (refactor hito 5)
//
// Este archivo es la capa de orquestación. La lógica de display y botón
// vive ahora en lib/ui/DisplayRenderer/ y lib/ui/InputHandler/.
//
// Comportamiento visible: idéntico a Fase 0.
//   - OLED: "Hola Tamagochi" + "Fase 0 - Bring-up" + blob 32×32 animado (2 frames, 400ms)
//   - Serie: logs [BOOT] idénticos, [BOTON] idénticos, [FPS] idénticos
//
// Hito siguiente (6): integrar EventBus — InputHandler emitirá eventos en vez
// de Serial.println, y main.cpp suscribirá handlers de lógica de juego.
// =============================================================================

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "EventBus.h"
#include "InputHandler.h"
#include "DisplayRenderer.h"

// -----------------------------------------------------------------------------
// Hardware — SSD1306 128x64 SOFTWARE I2C.
// La placa ideaspark V3.2 cablea el OLED a GPIO14/12, no al HW I2C (GPIO5/4).
// U8X8_PIN_NONE = sin pin RESET dedicado.
// Full buffer (1KB RAM): drawXBMP sin restricciones de página.
// -----------------------------------------------------------------------------
static U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// Módulos — EventBus inyectado por referencia a cada módulo (no Singleton clásico)
static EventBus        bus;
static DisplayRenderer dr(u8g2, bus);
static InputHandler    ih(bus, PIN_BUTTON);

// Schedulers
static uint32_t lastRenderMs    = 0;
static uint32_t lastFpsReportMs = 0;
static uint32_t framesRendered  = 0;

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("[BOOT] Tamagochi - Fase 0 iniciando...");

    Serial.printf("[BOOT] OLED SW I2C en SCL=GPIO%d SDA=GPIO%d\n", OLED_SCL, OLED_SDA);

    // begin() inicializa u8g2 internamente; renderiza frame inicial para confirmar OLED
    dr.begin();
    dr.setScene(Scene::IdleMain);
    dr.render();   // frame de boot visible cuanto antes
    Serial.println("[BOOT] OLED SSD1306 inicializado correctamente");

    // Configurar OneButton con callbacks via InputHandler
    ih.begin();
    Serial.printf("[BOOT] Boton configurado en GPIO%d (INPUT_PULLUP)\n", PIN_BUTTON);

    uint32_t now = millis();
    lastRenderMs    = now;
    lastFpsReportMs = now;

    Serial.println("[BOOT] Listo. Escuchando botones y renderizando...");
}

// =============================================================================
// loop
// =============================================================================
void loop() {
    // OneButton necesita tick() en cada iteración para detectar gestos correctamente.
    ih.loop();

    uint32_t now = millis();

    // Render a 5 FPS (cada RENDER_FRAME_MS = 200ms)
    if (now - lastRenderMs >= RENDER_FRAME_MS) {
        dr.render();
        framesRendered++;
        lastRenderMs = now;
    }

    // Reporte de FPS real cada FPS_REPORT_MS (5s)
    if (now - lastFpsReportMs >= FPS_REPORT_MS) {
        float elapsedSeconds = (now - lastFpsReportMs) / 1000.0f;
        float fps = framesRendered / elapsedSeconds;
        Serial.printf("[FPS] %.2f fps (%u frames en %.1fs)\n",
                      fps, framesRendered, elapsedSeconds);
        framesRendered  = 0;
        lastFpsReportMs = now;
    }

    // yield() cede el control al scheduler del ESP8266 (watchdog, TCP stack...).
    // Crítico en loops no bloqueantes para evitar soft WDT reset.
    yield();
}

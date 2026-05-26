// =============================================================================
// Tamagochi — Fase 1: Core jugable offline (hito 6)
//
// Capa de orquestación: instancia módulos, suscribe handlers al EventBus
// y ejecuta los schedulers del loop principal.
//
// Comportamiento visible (hito 6):
//   - OLED IdleMain: "Hola Tamagochi" + blob animado (idéntico a Fase 0)
//   - OLED StatsView: hunger / happiness / health con valores reales
//   - Botón corto   → alterna IdleMain ↔ StatsView
//   - Botón largo   → alimentar: hunger -20 pts (visible inmediatamente en StatsView)
//   - Botón doble   → evento publicado pero sin suscriptor en hito 6 (reservado)
//   - Cada segundo  → StatsEngine aplica decay; si cambia algún entero → StatsChanged
//
// Lo que NO está en este hito (hito 7+):
//   - Persistence (LittleFS load/save)
//   - OTA / WiFi / secrets.h
// =============================================================================

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "InputHandler.h"
#include "DisplayRenderer.h"

// -----------------------------------------------------------------------------
// Hardware — SSD1306 128×64 SOFTWARE I2C.
// La placa ideaspark V3.2 cablea el OLED a GPIO14 (SCL) y GPIO12 (SDA).
// Full buffer (1 KB RAM): drawXBMP sin restricciones de página.
// -----------------------------------------------------------------------------
static U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// -----------------------------------------------------------------------------
// Singletons — orden de construcción importa: bus primero (inyectado por ref).
// -----------------------------------------------------------------------------
static EventBus        bus;
static StatsEngine     statsEngine;                 // defaults: hunger=50, happiness=50, health=100
static InputHandler    input(bus, PIN_BUTTON);
static DisplayRenderer renderer(u8g2, bus);

// -----------------------------------------------------------------------------
// Estado de escena — accesible a los handlers estáticos.
// -----------------------------------------------------------------------------
static Scene currentScene = Scene::IdleMain;

// -----------------------------------------------------------------------------
// Schedulers
// -----------------------------------------------------------------------------
static uint32_t lastStatsTickMs = 0;
static uint32_t lastRenderMs    = 0;
static uint32_t lastFpsReportMs = 0;
static uint32_t framesRendered  = 0;

// =============================================================================
// Event handlers — funciones de archivo; tienen acceso a los singletons globales.
// Deben ejecutarse en < 2 ms para no romper el budget de 5 FPS (EventBus síncrono).
// =============================================================================

// Botón corto: alterna entre IdleMain y StatsView.
static void onShortClick(const Event&) {
    currentScene = (currentScene == Scene::IdleMain) ? Scene::StatsView : Scene::IdleMain;
    renderer.setScene(currentScene);
    LOG_INFO("Scene -> %s", currentScene == Scene::IdleMain ? "IdleMain" : "StatsView");
}

// Botón largo: alimentar al Tamagochi (-20 hunger). Publica StatsChanged
// para que el renderer actualice la vista inmediatamente sin esperar al tick.
static void onLongPress(const Event&) {
    statsEngine.applyFeed(20);
    bus.publish({EventType::StatsChanged, 0});
    LOG_INFO("Feed applied -> hunger=%u", statsEngine.stats().hunger);
}

// StatsChanged: sincroniza la copia interna del renderer con el estado actual.
// Se dispara tanto por el tick de decay como por acciones del botón.
static void onStatsChanged(const Event&) {
    renderer.setStats(statsEngine.stats());
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    LOG_INFO("Boot Fase 1 hito-6, version=0x%04X", FIRMWARE_VERSION);

    // OLED primero — frame de boot visible cuanto antes.
    renderer.begin();
    renderer.setScene(Scene::IdleMain);
    renderer.render();
    LOG_INFO("OLED SW I2C en SCL=GPIO%d SDA=GPIO%d", OLED_SCL, OLED_SDA);

    // Precarga las stats en el renderer para que StatsView muestre los defaults
    // si el usuario navega ahí antes de que el primer tick de decay pase.
    renderer.setStats(statsEngine.stats());

    // Suscripciones al EventBus — llamar en setup(), nunca en loop().
    bus.subscribe(EventType::ButtonShortClick, onShortClick);
    bus.subscribe(EventType::ButtonLongPress,  onLongPress);
    bus.subscribe(EventType::StatsChanged,     onStatsChanged);

    // Configurar OneButton con callbacks via InputHandler.
    input.begin();
    LOG_INFO("Boton configurado en GPIO%d", PIN_BUTTON);

    uint32_t now = millis();
    lastStatsTickMs = now;
    lastRenderMs    = now;
    lastFpsReportMs = now;

    LOG_INFO("Setup done");
}

// =============================================================================
// loop
// =============================================================================
void loop() {
    // OneButton necesita tick() en cada iteración para detectar gestos.
    input.loop();

    uint32_t now = millis();

    // Decay a 1 Hz (STATS_TICK_MS = 1000 ms normal, 16 ms en TURBO_MODE).
    if (now - lastStatsTickMs >= STATS_TICK_MS) {
        uint32_t dt = now - lastStatsTickMs;
        statsEngine.tick(dt);
        lastStatsTickMs = now;
        // Publicar StatsChanged solo si algún stat entero cambió (dirty flag).
        if (statsEngine.consumeDirtyFlag()) {
            bus.publish({EventType::StatsChanged, 0});
        }
    }

    // Render a 5 FPS (RENDER_FRAME_MS = 200 ms).
    if (now - lastRenderMs >= RENDER_FRAME_MS) {
        renderer.render();
        framesRendered++;
        lastRenderMs = now;
    }

    // Reporte de FPS real cada FPS_REPORT_MS (5 s).
    if (now - lastFpsReportMs >= FPS_REPORT_MS) {
        float elapsed = (now - lastFpsReportMs) / 1000.0f;
        float fps     = framesRendered / elapsed;
        Serial.printf("[FPS] %.2f fps (%u frames en %.1fs)\n",
                      fps, framesRendered, elapsed);
        framesRendered  = 0;
        lastFpsReportMs = now;
    }

    // yield() cede el control al scheduler del ESP8266 (watchdog, TCP stack...).
    // Crítico en loops no bloqueantes para evitar soft WDT reset.
    yield();
}

// =============================================================================
// Tamagochi — Fase 1: Core jugable offline (hito 7)
//
// Capa de orquestación: instancia módulos, suscribe handlers al EventBus
// y ejecuta los schedulers del loop principal.
//
// Comportamiento visible (hito 7, acumula hitos anteriores):
//   - OLED IdleMain: sprite animado BABY
//   - OLED StatsView: hunger / happiness / health con valores reales
//   - Botón corto   → alterna IdleMain ↔ StatsView
//   - Botón largo   → alimentar: hunger -20 pts + guarda estado (on-action)
//   - Botón doble   → evento publicado pero sin suscriptor (reservado)
//   - Cada segundo  → StatsEngine aplica decay; si cambia algún entero → StatsChanged
//   - Al boot       → carga estado persistido de LittleFS (sobrevive reinicios)
//   - Cada 5 min    → autosave vía PersistRequested
//
// Lo que NO está en este hito (hito 8+):
//   - OTA / WiFi / secrets.h
// =============================================================================

#include <Arduino.h>
#include <U8g2lib.h>
#include <LittleFS.h>
#include "config.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "Persistence.h"
#include "LittleFsStorage.h"
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
// storage y persistence se construyen antes que los módulos que los usan.
// -----------------------------------------------------------------------------
static EventBus        bus;
static StatsEngine     statsEngine;                 // defaults: hunger=50, happiness=50, health=100
static LittleFsStorage storage;
static Persistence     persistence(storage);
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
static uint32_t lastSaveMs      = 0;
static uint32_t framesRendered  = 0;

// =============================================================================
// Helpers de persistencia
// Mantienen el acoplamiento con LittleFS limitado al begin() inicial en setup().
// Toda la lógica de serialización pasa por Persistence (cumple rules/database.md).
// =============================================================================

// Copia el estado vivo del engine a un PersistedState listo para serializar.
// lastSavedAtEpoch/bornAtEpoch quedan en 0 hasta NTP (Fase 1.5).
static void fillFromEngine(PersistedState& out, const StatsEngine& engine) {
    Persistence::fillDefaults(out);                    // magic, version, defaults
    const Stats& s = engine.stats();
    out.hunger    = s.hunger;
    out.happiness = s.happiness;
    out.health    = s.health;
    // lifeStage permanece Baby=1 en Fase 1 (defaults ya lo pusieron)
    // ageMinutes y deathCount quedan en 0 — no se trackean en Fase 1
    // CRC se recalcula en Persistence::save() — aquí no hace falta
}

// Aplica un PersistedState recién cargado al engine vivo.
static void applyToEngine(const PersistedState& in, StatsEngine& engine) {
    Stats s;
    s.hunger    = in.hunger;
    s.happiness = in.happiness;
    s.health    = in.health;
    engine.setStats(s);
}

// Nombre legible del LoadResult — para logging al boot.
static const char* loadResultName(LoadResult lr) {
    switch (lr) {
        case LoadResult::Ok:              return "Ok";
        case LoadResult::NoneFound:       return "NoneFound";
        case LoadResult::AllCorrupt:      return "AllCorrupt";
        case LoadResult::Migrated:        return "Migrated";
        case LoadResult::MigrationFailed: return "MigrationFailed";
    }
    return "Unknown";
}

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
// También publica PersistRequested para guardar on-action.
static void onLongPress(const Event&) {
    statsEngine.applyFeed(20);
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});    // guarda on-action
    LOG_INFO("Feed applied -> hunger=%u", statsEngine.stats().hunger);
}

// StatsChanged: sincroniza la copia interna del renderer con el estado actual.
// Se dispara tanto por el tick de decay como por acciones del botón.
static void onStatsChanged(const Event&) {
    renderer.setStats(statsEngine.stats());
}

// PersistRequested: serializa el estado vivo y lo escribe en LittleFS.
// Invocado tanto por el scheduler de 5 min como por acciones on-action.
static void onPersistRequested(const Event&) {
    PersistedState s;
    fillFromEngine(s, statsEngine);
    if (!persistence.save(s)) {
        LOG_ERROR("Persist: save failed");
    } else {
        LOG_DEBUG("Persist: saved (hunger=%u happ=%u health=%u)",
                  s.hunger, s.happiness, s.health);
    }
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    LOG_INFO("Boot Fase 1 hito-7, version=0x%04X", FIRMWARE_VERSION);

    // OLED primero — frame de boot visible cuanto antes.
    renderer.begin();
    renderer.setScene(Scene::IdleMain);
    renderer.render();
    LOG_INFO("OLED SW I2C en SCL=GPIO%d SDA=GPIO%d", OLED_SCL, OLED_SDA);

    // Precarga las stats en el renderer para que StatsView muestre los defaults
    // si el usuario navega ahí antes de que el primer tick de decay pase.
    renderer.setStats(statsEngine.stats());

    // LittleFS — necesario antes de cualquier load/save.
    // LittleFsStorage delega toda operación en LittleFS, que debe estar montado.
    if (!LittleFS.begin()) {
        LOG_ERROR("LittleFS begin failed — intentando format");
        if (!LittleFS.format()) {
            LOG_ERROR("LittleFS format failed — persistencia desactivada en esta sesion");
        } else {
            LittleFS.begin();
            LOG_INFO("LittleFS formateado y montado");
        }
    }

    // Cargar estado persistido (o defaults si no hay ningún slot válido).
    PersistedState loaded;
    LoadResult lr = persistence.load(loaded);
    LOG_INFO("Persist load: %s (hunger=%u happ=%u health=%u)",
             loadResultName(lr), loaded.hunger, loaded.happiness, loaded.health);
    applyToEngine(loaded, statsEngine);

    // Actualizar renderer con las stats cargadas (puede diferir de los defaults).
    renderer.setStats(statsEngine.stats());

    // Suscripciones al EventBus — llamar en setup(), nunca en loop().
    bus.subscribe(EventType::ButtonShortClick, onShortClick);
    bus.subscribe(EventType::ButtonLongPress,  onLongPress);
    bus.subscribe(EventType::StatsChanged,     onStatsChanged);
    bus.subscribe(EventType::PersistRequested, onPersistRequested);

    // Configurar OneButton con callbacks via InputHandler.
    input.begin();
    LOG_INFO("Boton configurado en GPIO%d", PIN_BUTTON);

    uint32_t now = millis();
    lastStatsTickMs = now;
    lastRenderMs    = now;
    lastFpsReportMs = now;
    lastSaveMs      = now;    // evita un save espurio en el primer tick de 5 min

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

    // Autosave cada PERSIST_AUTOSAVE_MS (5 min wall-clock).
    // No se escala con TURBO_MODE — evita escrituras de flash innecesarias en tests.
    if (now - lastSaveMs >= PERSIST_AUTOSAVE_MS) {
        bus.publish({EventType::PersistRequested, 0});
        lastSaveMs = now;
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

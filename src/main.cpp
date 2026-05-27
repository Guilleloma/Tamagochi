// =============================================================================
// Tamagochi — Fase 3: Hito 2 — OTAManager + mDNS
//
// Capa de orquestación: instancia módulos, suscribe handlers al EventBus
// y ejecuta los schedulers del loop principal.
//
// Comportamiento completo (acumula hitos anteriores + hito 3.2):
//   - OLED IdleMain: sprite animado multi-etapa según LifeStage
//   - OLED StatsView: hunger / happiness / health / discipline / weight / poop
//   - OLED DeadScreen: pantalla RIP con causa de muerte y edad
//   - OLED OtaProgress: barra de progreso OTA durante actualización inalámbrica
//   - Icono WiFi 8×6 px en esquina superior derecha (Connecting=blink, Connected=on, Off=x)
//   - Botón corto   → alterna IdleMain ↔ StatsView
//   - Botón largo   → alimentar: hunger -20 pts + animación EAT 2.4s + guarda
//   - Botón doble   → evento publicado pero sin suscriptor (reservado)
//   - Cada segundo  → StatsEngine tick (decay); LifeFSM tick (envejecimiento)
//                     Decay paralizado cuando stage == Egg o Dead
//   - Al boot       → carga estado persistido (PersistedState v2) de LittleFS
//   - Transiciones  → LifeFSM publica LifeStateChanged → renderer + persist
//   - Resurrección  → DEAD → EGG: stats reset, escena IdleMain
//   - Cada 5 min    → autosave vía PersistRequested
//   - WiFi no bloqueante → WifiManager::begin()/loop(); publica WifiStateChanged
//   - OTA ArduinoOTA con password (secrets.h) → activado al conectar WiFi
//   - mDNS → tamagotchi.local disponible en LAN cuando WiFi Connected
//
// Lo que NO está en este hito (hitos posteriores):
//   - WebUI / REST API (hito 3+)
// =============================================================================

#include <Arduino.h>
#include <U8g2lib.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#include "config.h"
#include "secrets.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "LifeFSM.h"
#include "Persistence.h"
#include "LittleFsStorage.h"
#include "InputHandler.h"
#include "DisplayRenderer.h"
#include "WifiManager.h"
#include "OTAManager.h"
#include "WebUI.h"

// -----------------------------------------------------------------------------
// Hardware — SSD1306 128×64 SOFTWARE I2C.
// La placa ideaspark V3.2 cablea el OLED a GPIO14 (SCL) y GPIO12 (SDA).
// Full buffer (1 KB RAM): drawXBMP sin restricciones de página.
// -----------------------------------------------------------------------------
// U8G2_R1 = rotación 90° CW: la placa con USB-C arriba se ve en portrait
// (lógico = 64 wide × 128 tall). DisplayRenderer dibuja contra esas coords.
static U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R1, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// -----------------------------------------------------------------------------
// Singletons — orden de construcción importa: bus primero (inyectado por ref).
// storage y persistence se construyen antes que los módulos que los usan.
// LifeFSM se construye tras StatsEngine para que la firma de tick sea correcta.
// -----------------------------------------------------------------------------
static EventBus        bus;
static StatsEngine     statsEngine;                 // defaults: hunger=50, happiness=50, health=100
static LifeFSM         lifeFSM(bus);                // tras statsEngine — necesita bus para publicar
static LittleFsStorage storage;
static Persistence     persistence(storage);
// TTP223 capacitivo: salida push-pull active-HIGH → activeLow=false, pullupActive=false.
static InputHandler    input(bus, PIN_BUTTON, /*activeLow=*/false, /*pullupActive=*/false);
static DisplayRenderer renderer(u8g2, bus);
static WifiManager     wifi(bus);                   // Fase 3 hito 1 — no bloqueante
static OTAManager      ota(bus);                    // Fase 3 hito 2 — activa al conectar WiFi
static WebUI           webUI(bus, statsEngine, lifeFSM, wifi);  // Fase 3 hito 5 — HTTP server

// -----------------------------------------------------------------------------
// Estado de escena — accesible a los handlers estáticos.
// -----------------------------------------------------------------------------
static Scene currentScene = Scene::IdleMain;

// Etapa anterior del LifeFSM — para detectar la transición Dead → Egg
// (resurrección) en onLifeStateChanged sin necesidad de estado extra.
static LifeStage previousStage_ = LifeStage::Baby;

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

// Aplica un PersistedState v2 recién cargado al StatsEngine vivo.
// Lee todos los campos v2: discipline, weight, poopCount, flags, attentionScore.
static void applyToStatsEngine(const PersistedState& s, StatsEngine& engine) {
    Stats stats;
    stats.hunger         = s.hunger;
    stats.happiness      = s.happiness;
    stats.health         = s.health;
    stats.discipline     = s.discipline;
    stats.weight         = s.weight;
    stats.poopCount      = s.poopCount;
    stats.isSick         = (s.flags & 0x01) != 0;
    stats.isSleeping     = (s.flags & 0x02) != 0;
    stats.attentionScore = s.attentionScore;
    engine.setStats(stats);
}

// Copia el estado vivo de StatsEngine + LifeFSM a un PersistedState listo para serializar.
// Incluye todos los campos v2: lifeStage, deathSustainMs, attentionScore, adultType.
static void fillFromEngine(PersistedState& s, const StatsEngine& engine, const LifeFSM& fsm) {
    Persistence::fillDefaults(s);           // magic, version, ceros — base limpia
    const Stats& st = engine.stats();
    s.lifeStage          = static_cast<uint8_t>(fsm.stage());
    s.ageMinutes         = fsm.ageMinutes();
    s.hunger             = st.hunger;
    s.happiness          = st.happiness;
    s.health             = st.health;
    s.discipline         = st.discipline;
    s.weight             = st.weight;
    s.poopCount          = st.poopCount;
    s.flags              = (st.isSick ? 0x01 : 0) | (st.isSleeping ? 0x02 : 0);
    s.attentionScore     = st.attentionScore;
    s.deathSustainMs     = fsm.deathSustainMs();
    s.lastDeathCauseCode = fsm.lastDeathCause();
    s.adultType          = static_cast<uint8_t>(fsm.adultType());
    // CRC se recalcula en Persistence::save() — aquí no hace falta
}

// =============================================================================
// Event handlers — funciones de archivo; tienen acceso a los singletons globales.
// Deben ejecutarse en < 2 ms para no romper el budget de 5 FPS (EventBus síncrono).
// =============================================================================

// Publica el EventType asociado a la acción del HUD inferior y lanza animación
// si aplica. Stats no se gestiona aquí (lo maneja onLongPress abriendo escena).
// Los payloads por defecto reproducen los del dashboard (ver WebUI).
static void executeSelectedAction_() {
    ActionKind action = renderer.currentSelectedAction();
    switch (action) {
        case ActionKind::Feed:
            bus.publish({EventType::ActionFeedRequested, 20});
            renderer.triggerEatAnimation();   // mood EAT del sprite del personaje
            break;
        case ActionKind::Play:
            bus.publish({EventType::ActionPlayRequested, 20});
            renderer.triggerActionAnimation(action);
            break;
        case ActionKind::Clean:
            bus.publish({EventType::ActionCleanRequested, 0});
            renderer.triggerActionAnimation(action);
            break;
        case ActionKind::Medicate:
            bus.publish({EventType::ActionMedicateRequested, 0});
            break;
        case ActionKind::Discipline:
            bus.publish({EventType::ActionDisciplineRequested, 10});
            renderer.triggerActionAnimation(action);
            break;
        case ActionKind::Sleep:
            // Toggle: si estaba durmiendo, despertar (0); si no, dormir (1).
            bus.publish({EventType::ActionSleepRequested,
                         statsEngine.stats().isSleeping ? 0 : 1});
            break;
        case ActionKind::Heal:
            bus.publish({EventType::ActionHealRequested, 20});
            renderer.triggerActionAnimation(action);
            break;
        case ActionKind::Stats:
            // Manejado en onLongPress como transición de escena, no debería llegar.
            break;
    }
    LOG_INFO("Action HUD: executed %u", static_cast<unsigned>(action));
}

// Botón corto:
//   IdleMain  → refresca preconditions y avanza el cursor del HUD inferior.
//   StatsView → cierra StatsView y vuelve al HUD principal.
// No reacciona en DeadScreen ni en OtaProgress (UI inerte durante actualizacion).
static void onShortClick(const Event&) {
    if (lifeFSM.stage() == LifeStage::Dead) return;
    if (currentScene == Scene::OtaProgress) return;

    switch (currentScene) {
        case Scene::IdleMain:
            // Recalcular visibles antes de avanzar — preconditions pueden haber
            // cambiado desde el último click (decay, acción remota desde dashboard).
            renderer.refreshActionMenu(statsEngine);
            renderer.moveCursor();
            break;
        case Scene::StatsView:
            currentScene = Scene::IdleMain;
            renderer.setScene(currentScene);
            LOG_INFO("Scene -> IdleMain (from StatsView)");
            break;
        default:
            break;
    }
}

// Botón largo:
//   IdleMain  → ejecuta la acción activa del HUD inferior. Si la activa es
//               Stats, abre StatsView en lugar de publicar evento.
//   StatsView → no-op (long press solo tiene semantica en IdleMain).
static void onLongPress(const Event&) {
    if (lifeFSM.stage() == LifeStage::Dead) return;
    if (currentScene == Scene::OtaProgress) return;

    if (currentScene != Scene::IdleMain) return;

    ActionKind action = renderer.currentSelectedAction();
    if (action == ActionKind::Stats) {
        currentScene = Scene::StatsView;
        renderer.setScene(currentScene);
        LOG_INFO("Scene -> StatsView (from HUD)");
        return;
    }

    executeSelectedAction_();
}

// StatsChanged: sincroniza la copia interna del renderer con el estado actual.
// Se dispara tanto por el tick de decay como por acciones del botón.
static void onStatsChanged(const Event&) {
    renderer.setStats(statsEngine.stats());
}

// PersistRequested: serializa el estado vivo y lo escribe en LittleFS.
// Invocado tanto por el scheduler de 5 min como por acciones on-action y transiciones.
static void onPersistRequested(const Event&) {
    PersistedState s;
    fillFromEngine(s, statsEngine, lifeFSM);
    if (!persistence.save(s)) {
        LOG_ERROR("Persist: save failed");
    } else {
        LOG_DEBUG("Persist: saved (stage=%u hunger=%u happ=%u health=%u age=%u)",
                  s.lifeStage, s.hunger, s.happiness, s.health, s.ageMinutes);
    }
}

// WifiStateChanged: sincroniza el estado WiFi con el renderer e inicia/detiene OTA y mDNS.
// WebUI se activa en hito 3+.
static void onWifiStateChanged(const Event& e) {
    WifiState s = static_cast<WifiState>(e.payload & 0xFF);
    LOG_INFO("[wifi] state -> %u", static_cast<unsigned>(s));
    renderer.setWifiState(s);

    if (s == WifiState::Connected) {
        // Activar OTA — requiere WiFi conectado con IP asignada.
        ota.begin();

        // Activar WebUI — servidor HTTP en puerto 80 con endpoints REST.
        if (!webUI.isRunning()) webUI.begin();

        // Publicar hostname mDNS + servicio HTTP para que la LAN nos descubra.
        if (MDNS.begin(OTA_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            LOG_INFO("[mdns] hostname=%s.local", OTA_HOSTNAME);
        } else {
            LOG_WARN("[mdns] begin failed — continuando sin mDNS");
        }
    } else {
        // Cualquier estado distinto de Connected.
        if (ota.isReady()) ota.end();
        if (webUI.isRunning()) webUI.end();
        // mDNS se queda corriendo pasivamente; si WiFi vuelve, un nuevo MDNS.begin()
        // lo reiniciará. No hay MDNS.end() en el SDK de Arduino para ESP8266.
    }
}

// OtaStarted: cambiar a pantalla de progreso OTA.
static void onOtaStarted(const Event&) {
    LOG_INFO("[ota] update started — switching to OtaProgress scene");
    renderer.setScene(Scene::OtaProgress);
    currentScene = Scene::OtaProgress;
    renderer.setOtaProgress(0);
}

// OtaProgress: actualizar la barra de progreso en OLED.
static void onOtaProgress(const Event& e) {
    uint8_t pct = static_cast<uint8_t>(e.payload & 0xFF);
    renderer.setOtaProgress(pct);
}

// OtaEnded: el ESP reinicia automáticamente tras este evento. No restauramos scene.
static void onOtaEnded(const Event&) {
    LOG_INFO("[ota] update ended — reboot imminent");
}

// OtaError: restaurar escena anterior y loguear el código de error.
static void onOtaError(const Event& e) {
    LOG_ERROR("[ota] error code=%d", static_cast<int>(e.payload));
    renderer.setScene(Scene::IdleMain);
    currentScene = Scene::IdleMain;
}

// =============================================================================
// Handlers de acciones WebUI (hito 6)
// Publicados por WebUI al EventBus; ejecutados síncronamente antes de que
// WebUI envíe la respuesta HTTP. Aplican la acción y persisten.
// =============================================================================

static void onActionFeed(const Event& e) {
    statsEngine.applyFeed(static_cast<uint8_t>(e.payload));
    renderer.triggerEatAnimation();
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] feed +%d -> hunger=%u", static_cast<int>(e.payload), statsEngine.stats().hunger);
}

static void onActionPlay(const Event& e) {
    statsEngine.applyPlay(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] play +%d -> happiness=%u", static_cast<int>(e.payload), statsEngine.stats().happiness);
}

static void onActionHeal(const Event& e) {
    statsEngine.applyHeal(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] heal +%d -> health=%u", static_cast<int>(e.payload), statsEngine.stats().health);
}

static void onActionClean(const Event&) {
    statsEngine.applyClean();
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] clean -> poop=%u", statsEngine.stats().poopCount);
}

static void onActionMedicate(const Event&) {
    statsEngine.applyMedicate();
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] medicate -> isSick=%u health=%u", statsEngine.stats().isSick, statsEngine.stats().health);
}

static void onActionDiscipline(const Event& e) {
    statsEngine.applyDiscipline(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] discipline +%d -> discipline=%u", static_cast<int>(e.payload), statsEngine.stats().discipline);
}

static void onActionSleep(const Event& e) {
    bool on = (e.payload != 0);
    statsEngine.applySleep(on);
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] sleep %s -> isSleeping=%u", on ? "on" : "off", statsEngine.stats().isSleeping);
}

// LifeStateChanged: reacciona a transiciones del LifeFSM.
// Actualiza el renderer, activa DeadScreen, detecta resurrección.
// Persiste inmediatamente al cambiar de etapa (stage change = evento crítico).
static void onLifeStateChanged(const Event& e) {
    LifeStage newStage = static_cast<LifeStage>((e.payload >> 8) & 0xFF);
    uint8_t   subtype  = static_cast<uint8_t>(e.payload & 0xFF);

    LOG_INFO("Life: stage=%u -> %u (subtype=%u)",
             static_cast<unsigned>(previousStage_),
             static_cast<unsigned>(newStage),
             subtype);

    renderer.setLifeStage(newStage);
    if (newStage == LifeStage::Adult) {
        renderer.setAdultType(subtype);
    }

    // Animaciones de transición one-shot (visualizan el cambio).
    // Eclosión Egg → Baby: usa frames 2-3 del huevo durante 2.4 s.
    if (newStage == LifeStage::Baby && previousStage_ == LifeStage::Egg) {
        renderer.triggerHatchAnimation();
    }
    // Crecimiento Baby/Kid/Adult → siguiente etapa: parpadeo 1.2 s.
    else if ((newStage == LifeStage::Kid   && previousStage_ == LifeStage::Baby) ||
             (newStage == LifeStage::Adult && previousStage_ == LifeStage::Kid)  ||
             (newStage == LifeStage::Old   && previousStage_ == LifeStage::Adult)) {
        renderer.triggerEvolveAnimation();
    }

    if (newStage == LifeStage::Dead) {
        renderer.setLastDeathCause(lifeFSM.lastDeathCause());
        renderer.setAgeMinutes(lifeFSM.ageMinutes());
        renderer.setScene(Scene::DeadScreen);
        currentScene = Scene::DeadScreen;
    }

    // Resurrección: Dead → Egg forzada por LifeFSM tras kResurrectionMs
    if (newStage == LifeStage::Egg && previousStage_ == LifeStage::Dead) {
        LOG_INFO("Resurrection: new egg — resetting stats");
        statsEngine.resetForNewLife();
        currentScene = Scene::IdleMain;
        renderer.setScene(Scene::IdleMain);
        bus.publish({EventType::StatsChanged, 0});     // sincronizar renderer con stats frescos
    }

    previousStage_ = newStage;

    // Persistir cambio de etapa inmediatamente — es un evento crítico.
    bus.publish({EventType::PersistRequested, 0});
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    LOG_INFO("Boot Fase 2 hitos-7-9, version=0x%04X", FIRMWARE_VERSION);

// TURBO_MODE guard: el static_assert falla en compile-time si la versión de
// firmware no coincide con el entorno. Previene mezclar binarios turbo en
// producción (rules/security.md — seguridad por defecto).
#ifdef TURBO_MODE
    LOG_WARN("=================================================");
    LOG_WARN("== TURBO_MODE ACTIVE - factor %u                 ==", (unsigned)TURBO_FACTOR);
    LOG_WARN("== ESTE BINARIO NO DEBE LLEGAR A PRODUCCION    ==");
    LOG_WARN("=================================================");
    static_assert(FIRMWARE_VERSION == 0x02FE, "TURBO build must use FIRMWARE_VERSION 0x02FE");
#else
    static_assert(FIRMWARE_VERSION == 0x0200, "Prod build must use FIRMWARE_VERSION 0x0200");
#endif

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

    // Cargar estado persistido (PersistedState v2) o defaults si no hay slot válido.
    PersistedState loaded;
    LoadResult lr = persistence.load(loaded);
    LOG_INFO("Persist load: %s (hunger=%u happ=%u health=%u stage=%u age=%u)",
             loadResultName(lr), loaded.hunger, loaded.happiness, loaded.health,
             loaded.lifeStage, loaded.ageMinutes);

    // Aplicar estado cargado a StatsEngine y LifeFSM.
    applyToStatsEngine(loaded, statsEngine);
    lifeFSM.setStage(static_cast<LifeStage>(loaded.lifeStage));
    lifeFSM.setAgeMinutes(loaded.ageMinutes);
    lifeFSM.setDeathSustainMs(loaded.deathSustainMs);

    // Inicializar previousStage_ con el estado cargado para detectar correctamente
    // la resurrección Dead → Egg en el primer tick tras el boot.
    previousStage_ = lifeFSM.stage();

    // Sincronizar renderer con el estado cargado desde flash.
    renderer.setStats(statsEngine.stats());
    renderer.setLifeStage(lifeFSM.stage());
    if (lifeFSM.stage() == LifeStage::Adult) {
        renderer.setAdultType(loaded.adultType);
    }
    renderer.setLastDeathCause(loaded.lastDeathCauseCode);
    renderer.setAgeMinutes(loaded.ageMinutes);
    renderer.setDeathCount(loaded.deathCount);

    // Inicializar el HUD inferior con las acciones aplicables al estado cargado,
    // para que el primer frame de IdleMain ya muestre el cursor en una entrada
    // válida y los dots de paginación reflejen las preconditions correctas.
    renderer.refreshActionMenu(statsEngine);

    // Si arranca ya muerto (power-loss durante DeadScreen), restaurar la escena.
    if (lifeFSM.stage() == LifeStage::Dead) {
        currentScene = Scene::DeadScreen;
        renderer.setScene(Scene::DeadScreen);
    }

    // Suscripciones al EventBus — llamar en setup(), nunca en loop().
    bus.subscribe(EventType::ButtonShortClick, onShortClick);
    bus.subscribe(EventType::ButtonLongPress,  onLongPress);
    bus.subscribe(EventType::StatsChanged,     onStatsChanged);
    bus.subscribe(EventType::PersistRequested, onPersistRequested);
    bus.subscribe(EventType::LifeStateChanged, onLifeStateChanged);
    bus.subscribe(EventType::WifiStateChanged, onWifiStateChanged);
    bus.subscribe(EventType::OtaStarted,       onOtaStarted);
    bus.subscribe(EventType::OtaProgress,      onOtaProgress);
    bus.subscribe(EventType::OtaEnded,         onOtaEnded);
    bus.subscribe(EventType::OtaError,         onOtaError);

    // Acciones WebUI (hito 6) — publicadas por los POST handlers de WebUI
    bus.subscribe(EventType::ActionFeedRequested,       onActionFeed);
    bus.subscribe(EventType::ActionPlayRequested,       onActionPlay);
    bus.subscribe(EventType::ActionHealRequested,       onActionHeal);
    bus.subscribe(EventType::ActionCleanRequested,      onActionClean);
    bus.subscribe(EventType::ActionMedicateRequested,   onActionMedicate);
    bus.subscribe(EventType::ActionDisciplineRequested, onActionDiscipline);
    bus.subscribe(EventType::ActionSleepRequested,      onActionSleep);

    // WiFi no bloqueante — devuelve inmediatamente. El estado se actualiza en loop().
    // Si WIFI_SSID está vacío en secrets.h, begin() marca Disabled y no hace nada más.
    wifi.begin();

    // Configurar OneButton con callbacks via InputHandler.
    input.begin();
    LOG_INFO("Boton (touch TTP223) configurado en GPIO%d", PIN_BUTTON);

    uint32_t now = millis();
    lastStatsTickMs = now;
    lastRenderMs    = now;
    lastFpsReportMs = now;
    lastSaveMs      = now;    // evita un save espurio en el primer tick de 5 min

    LOG_INFO("Setup done (stage=%u ageMin=%u)",
             static_cast<unsigned>(lifeFSM.stage()), lifeFSM.ageMinutes());
}

// =============================================================================
// loop
// =============================================================================
void loop() {
    // WiFi poll no bloqueante — guard interno actúa solo cada 500 ms.
    wifi.loop();

    // OTA: sondea el servidor ArduinoOTA. Coste < 5 µs sin actualización en curso.
    ota.loop();

    // mDNS: mantiene el anuncio de hostname en la LAN.
    MDNS.update();

    // OneButton necesita tick() en cada iteración para detectar gestos.
    input.loop();

    uint32_t now = millis();

    // Decay a 1 Hz (STATS_TICK_MS = 1000 ms normal, 16 ms en TURBO_MODE).
    //
    // NOTA: StatsEngine::tick() solo se llama cuando stage NO es Egg ni Dead.
    // Huevos y muertos no tienen decay — sus stats se congelan como "epitafio".
    // LifeFSM::tick() SIEMPRE se llama para avanzar la edad y los timers internos
    // (deathSustainMs, deadElapsedMs), incluido el timer de resurrección.
    if (now - lastStatsTickMs >= STATS_TICK_MS) {
        uint32_t dt = now - lastStatsTickMs;
        lastStatsTickMs = now;

        LifeStage stage = lifeFSM.stage();
        if (stage != LifeStage::Egg && stage != LifeStage::Dead) {
            statsEngine.tick(dt);
        }
        lifeFSM.tick(dt, statsEngine.stats(), statsEngine.stats().attentionScore);

        // Propagar la edad al renderer para el mini-contador en IdleMain.
        renderer.setAgeMinutes(lifeFSM.ageMinutes());

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

// =============================================================================
// main_esp32.cpp — FASE 5: WiFi + OTA + WebUI (dashboard REST) en ESP32-S3.
//
// Porta src/main.cpp (ESP8266) al ESP32-S3. Diferencias respecto a Fase 4:
//   - Red: WifiManager (STA no bloqueante), OTAManager, WebUI + mDNS.
//   - Display: JD9853 240×296 color vía DisplayRendererColor (no U8g2/SSD1306).
//   - Canvas framebuffer en PSRAM; sprites desde LittleFS → SpriteStore.
//   - LittleFS.begin(true) compartido: SpriteStore + Persistence usan el mismo FS.
//   - RNG de StatsEngine: semillado con esp_random() al boot.
//   - Versión firmware: 0x0300 (prod) / 0x03FE (turbo).
//
// Comportamiento:
//   - Boot       → carga PersistedState v2 → aplica a StatsEngine/LifeFSM
//   - Pausa de crecimiento (GROWTH_CAP_STAGE, config.h) → tope de etapa en
//     LifeFSM; un pet guardado por encima se degrada al cargar y se persiste
//   - Schedulers → decay, envejecimiento, render, autosave (no-bloqueante)
//   - Botón      → short click=moveCursor, long press=acción HUD
//   - Acciones   → Feed/Play/Clean/Medicate/Discipline/Lights
//   - Animaciones → EAT, Evolve, Hatch
//   - Muerte/Resurrección → DeadScreen → nuevo Egg tras kResurrectionMs
//   - Persistencia automática tras cada acción crítica y cada 5 min
//   - WiFi STA no bloqueante → WifiManager publica WifiStateChanged
//   - OTA ArduinoOTA con password (secrets.h) → activa al conectar WiFi
//   - WebUI REST API /api/v1/* → activa al conectar WiFi
//   - mDNS tamagotchi.local → activa al conectar WiFi (sin MDNS.update())
//   - Offline con WIFI_SSID vacío → icono WiFi Disabled, juego normal
//   - Stats en 2 páginas (click corto avanza): STATS 1/2 → RED 2/2 (estado
//     WiFi, SSID, IP, <host>.local, RSSI) → IdleMain. La IP también sale por
//     serie al conectar ("[wifi] Conectado a ...", WifiManager).
//
// COMPILAR:  pio run -e esp32s3
// FLASHEAR:  pio run -e esp32s3 -t upload
//            pio run -e esp32s3 -t uploadfs  (dashboard web data/)
// TURBO:     pio run -e esp32s3_turbo -t upload
// OTA:       pio run -e esp32s3_ota -t upload  (requiere WiFi activo en secrets.h)
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_random.h>      // esp_random() — TRNG del ESP32-S3 (hardware)
#include <esp_task_wdt.h>    // Task Watchdog Timer — API antigua (core 2.x/IDF4.4):
                             // esp_task_wdt_init(timeout_s, panic), NO la struct de IDF5.
#include <Arduino_GFX_Library.h>
#include <ESPmDNS.h>         // mDNS en ESP32 (sin MDNS.update() — no es necesario)

#include "jd9853_panel.h"    // Arduino_JD9853, makeTftBus(), makeJD9853()

#include "config.h"
#include "secrets.h"         // WIFI_SSID, OTA_PASSWORD, WEB_AUTH_* (gitignored)
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "LifeFSM.h"
#include "Persistence.h"
#include "JsonCodec.h"       // lifeStageToString() — nombres de etapa para los logs
#include "LittleFsStorage.h"
#include "InputHandler.h"
#include "DisplayRendererColor.h"
#include "SpriteStore.h"
#include "WifiManager.h"
#include "OTAManager.h"
#include "WebUI.h"
#include "BootSplash.h"
#include "BatteryMonitor.h"
#include "MessageBox.h"
#include "BootLoopGuard.h"   // anti-boot-loop del watchdog de conectividad (hito 9)
#include "ScreenPowerLogic.h" // apagado de pantalla por inactividad (ahorro de batería)

// =============================================================================
// Verificación de versión de firmware en compile-time.
// Previene que binarios turbo lleguen a producción (rules/security.md).
// Los valores para esp32 son distintos a los de esp8266 para distinguirlos.
// =============================================================================
#ifdef TURBO_MODE
  static_assert(FIRMWARE_VERSION == 0x03FE,
                "TURBO build esp32s3 debe usar FIRMWARE_VERSION 0x03FE");
#else
  static_assert(FIRMWARE_VERSION == 0x0300,
                "Prod build esp32s3 debe usar FIRMWARE_VERSION 0x0300");
#endif

// Pausa de crecimiento (config.h): el tope debe ser una etapa alcanzable por
// edad (Egg..Old). Dead no tiene sentido como tope (equivale a no tenerlo).
#if GROWTH_CAP_ENABLED
  static_assert(static_cast<int>(GROWTH_CAP_STAGE) >= static_cast<int>(LifeStage::Egg) &&
                static_cast<int>(GROWTH_CAP_STAGE) <  static_cast<int>(LifeStage::Dead),
                "GROWTH_CAP_STAGE debe ser 0=Egg..4=Old (LifeStage, ver Persistence.h)");
#endif

// =============================================================================
// Hardware — JD9853 240×296 IPS, canvas en PSRAM
// =============================================================================
static Arduino_DataBus* tftBus = makeTftBus();
static Arduino_GFX*     tft    = makeJD9853(tftBus);
// Canvas framebuffer (240×296 × 2 bytes RGB565 = ~138 KB en PSRAM).
static Arduino_Canvas*  canvas = new Arduino_Canvas(240, 296, tft);

// =============================================================================
// Singletons — orden de construcción importante.
// bus debe existir antes de LifeFSM y renderer (inyectado por ref).
// storage y persistence antes de los módulos de dominio.
// =============================================================================
static EventBus        bus;
static StatsEngine     statsEngine;       // defaults: hunger=50, happiness=50, health=100
static LifeFSM         lifeFSM(bus);      // necesita bus para publicar LifeStateChanged
static LittleFsStorage storage;
static Persistence     persistence(storage);
static SpriteStore     spriteStore;

// InputHandler y DisplayRendererColor se construyen en heap en setup() para
// evitar problemas de orden de inicialización de globales en ESP-IDF.
static InputHandler*          inputHandler = nullptr;
static DisplayRendererColor*  renderer     = nullptr;

// Módulos de red — Fase 5.
// WifiManager: STA no bloqueante; publica WifiStateChanged al bus.
// OTAManager: actualización OTA; se activa solo cuando WiFi Connected.
// WebUI: servidor REST; se activa solo cuando WiFi Connected.
// Con WIFI_SSID vacío en secrets.h, todos los módulos de red se auto-desactivan
// y el juego funciona completamente offline sin intentar conectar.
static WifiManager wifi(bus);
static OTAManager  ota(bus);
// BatteryMonitor: ADC del divisor de batería (aún sin soldar, ver
// docs/hardware-battery.md) — hasta entonces se autodetecta como Unknown.
// No depende de WiFi; funciona igual online/offline.
static BatteryMonitor battery(bus);
// WebUI se construye en heap en setup() para acceder a renderer/engine ya inicializados.
// Declaramos un puntero aquí; la instancia se crea en setup().
static WebUI*      webUI = nullptr;
// messageBox: buffer SPSC para el mensaje de texto del padre (POST
// /api/v1/message). Productor = handler HTTP (task async_tcp, vía WebUI);
// consumidor = loop() (ver más abajo). Puro (lib/core) — no requiere begin().
static MessageBox  messageBox;

// =============================================================================
// Estado de escena — accesible a los handlers estáticos del EventBus.
// =============================================================================
static SceneColor currentScene = SceneColor::IdleMain;

// Escena activa justo antes de entrar en MessageView — para restaurarla
// cuando el marquee termina (timeout, vueltas agotadas o descarte por botón).
// Solo se actualiza al ENTRAR en MessageView (ver loop()), nunca mientras ya
// se está en ella, para no perder la escena original si llegan varios
// mensajes seguidos antes de que el usuario descarte el primero.
static SceneColor sceneBeforeMessage_ = SceneColor::IdleMain;

// Etapa anterior del LifeFSM — para detectar la transición Dead→Egg
// (resurrección) en onLifeStateChanged sin estado extra.
static LifeStage previousStage_ = LifeStage::Baby;

// =============================================================================
// Schedulers (millis-based, no-bloqueante)
// =============================================================================
static uint32_t lastStatsTickMs = 0;
static uint32_t lastRenderMs    = 0;
static uint32_t lastSaveMs      = 0;
static uint32_t lastFpsReportMs = 0;
static uint32_t framesRendered  = 0;
static uint32_t lastNetSupervisorMs = 0;
static uint32_t lastNetInfoRefreshMs = 0;   // página de red (NetworkView), ver loop()

// =============================================================================
// Helpers de persistencia
// Mantienen el acoplamiento con LittleFS limitado al begin() en setup().
// Toda serialización pasa por Persistence (cumple rules/database.md).
// =============================================================================

// Nombre legible del LoadResult para logging al boot.
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

// Aplica un PersistedState v3 cargado al StatsEngine vivo.
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
    stats.lightsOn       = (s.flags & 0x04) != 0;
    stats.attentionScore = s.attentionScore;
    stats.energy         = s.energy;
    engine.setStats(stats);
}

// Copia el estado vivo de StatsEngine + LifeFSM a PersistedState listo para serializar.
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
    s.flags              = (st.isSick ? 0x01 : 0) | (st.isSleeping ? 0x02 : 0) | (st.lightsOn ? 0x04 : 0);
    s.attentionScore     = st.attentionScore;
    s.energy             = st.energy;
    s.deathSustainMs     = fsm.deathSustainMs();
    s.lastDeathCauseCode = fsm.lastDeathCause();
    s.adultType          = static_cast<uint8_t>(fsm.adultType());
    // CRC se recalcula en Persistence::save() — no hace falta aquí.
}

// =============================================================================
// Apagado de pantalla por inactividad (ahorro de batería) — ver
// ScreenPowerLogic.h para la decisión pura (sin hardware). Aquí solo el
// efecto físico: backlight (TFT_BL) + SLPIN/SLPOUT del panel JD9853.
// screenPower.update() se llama una vez por vuelta de loop() (ver loop()
// más abajo); sleepScreen()/wakeScreen() SOLO se invocan desde ahí
// (loopTask) — nunca desde un handler de evento.
// =============================================================================
static ScreenPowerLogic screenPower(SCREEN_TIMEOUT_MS);

// sleepScreen — apaga el panel físico. Backlight PRIMERO (así no se ve el
// "flash" del panel mientras entra en SLPIN) y luego tft->displayOff()
// (SLPIN) para que el propio controlador reduzca consumo.
static void sleepScreen() {
    digitalWrite(TFT_BL, LOW);
    tft->displayOff();
    LOG_INFO("[screen] off");
}

// wakeScreen — enciende el panel físico. tft->displayOn() (SLPOUT) ya
// incluye un delay(120) interno (ST7789_SLPOUT_DELAY, ver
// Arduino_ST7789::displayOn() en la librería) para la estabilización que
// exige el controlador tras salir de sleep; el delay(120) explícito de aquí
// es margen adicional documentado frente al WDT de 10 s (WDT_TIMEOUT_S) —
// sin riesgo real de disparar el watchdog. El backlight se enciende
// DESPUÉS de displayOn() para no dejar ver el panel aún reinicializándose.
// Forzamos un render inmediato para no mostrar el frame viejo (hasta
// RENDER_FRAME_MS de retraso si esperásemos al scheduler normal).
// lastRenderMs = millis() tras ese render forzado — mismo patrón que el
// primer frame en setup() (render() seguido de lastRenderMs = now). Sin
// esto, lastRenderMs queda obsoleto (~SCREEN_TIMEOUT_MS) y el bloque de
// render programado del loop (más abajo) repite el MISMO frame por SPI en
// la misma vuelta, justo después de este render forzado.
static void wakeScreen() {
    tft->displayOn();
    delay(120);
    digitalWrite(TFT_BL, HIGH);
    if (renderer) renderer->render();
    lastRenderMs = millis();
    LOG_INFO("[screen] on");
}

// =============================================================================
// Event handlers — función plana; tienen acceso a los singletons globales.
// Deben ejecutarse en < 2 ms (EventBus síncrono — mismo budget que ESP8266).
// =============================================================================

// Publica el EventType de la acción HUD seleccionada y dispara la animación one-shot
// correspondiente en el renderer. triggerActionAnimation() resuelve el mapeo
// acción→MoodColor internamente vía MoodLogic (Feed→Eat, Play→Jump,
// Medicate→Happy, Discipline→Sad, Clean→Idle) con la cadena de fallback
// Jump→Happy→Idle si la etapa no tiene esa animación (solo el baby salta).
static void executeSelectedAction_() {
    ActionKind action = renderer->currentSelectedAction();
    switch (action) {
        case ActionKind::Feed:
            bus.publish({EventType::ActionFeedRequested, 20});
            break;
        case ActionKind::Play:
            bus.publish({EventType::ActionPlayRequested, 20});
            break;
        case ActionKind::Clean:
            bus.publish({EventType::ActionCleanRequested, 0});
            break;
        case ActionKind::Medicate:
            bus.publish({EventType::ActionMedicateRequested, 0});
            break;
        case ActionKind::Discipline:
            bus.publish({EventType::ActionDisciplineRequested, 10});
            break;
        case ActionKind::Lights:
            // Toggle de lightsOn (NO de isSleeping — el sueño es automático,
            // ver StatsEngine::tick()). Lights no dispara animación one-shot:
            // el estado isSleeping activa la animación sleep de forma
            // permanente en computeMood_(); el guard de pantalla oscura vive
            // en el renderer (isSleeping && !lightsOn).
            bus.publish({EventType::ActionLightsRequested,
                         statsEngine.stats().lightsOn ? 0 : 1});
            break;
        case ActionKind::Stats:
            // Se maneja en onLongPress como transición de escena — no llega aquí.
            break;
    }

    // Disparar animación one-shot para acciones que la tengan (Feed/Play/
    // Clean/Medicate/Discipline). triggerActionAnimation es no-op para Lights/Stats.
    renderer->triggerActionAnimation(action);

    LOG_INFO("Action HUD: executed %u", static_cast<unsigned>(action));
}

// dismissMessage_ — descarte del mensaje del padre (botón físico, corto o
// largo indistintamente — ver onShortClick/onLongPress). Restaura la escena
// que estaba activa antes de que el mensaje interrumpiera (sceneBeforeMessage_).
static void dismissMessage_() {
    renderer->dismissMessage();
    currentScene = sceneBeforeMessage_;
    renderer->setScene(currentScene);
    LOG_INFO("Message: descartado por boton -> scene=%u", static_cast<unsigned>(currentScene));
}

// refreshNetworkInfo_ — vuelca la instantánea de red de WifiManager (capa
// infra, la única que lee WiFi.*) al renderer, que la formatea con
// NetInfoLogic para la página de red de Stats. Barato (unos µs, sin I/O de
// red): se llama en cada WifiStateChanged, al entrar en NetworkView y cada
// NET_INFO_REFRESH_MS mientras está visible (ver loop()). Solo loopTask.
static void refreshNetworkInfo_() {
    if (renderer) renderer->setWifiInfo(wifi.info());
    lastNetInfoRefreshMs = millis();
}

// Botón corto:
//   IdleMain     → refresca preconditions y avanza cursor del HUD.
//   StatsView    → pasa a la página 2/2 de Stats (NetworkView, red).
//   NetworkView  → cierra Stats y vuelve a IdleMain.
//   MessageView  → descarta el mensaje del padre y restaura la escena previa.
//   DeadScreen / otros → ignorar.
static void onShortClick(const Event&) {
    // Pantalla apagada: el primer toque SOLO despierta (no mueve cursor ni
    // ejecuta nada) — el wake real lo hace update() en la siguiente vuelta
    // de loop() (loopTask), evitando tocar el bus SPI desde este handler.
    if (!screenPower.isScreenOn()) {
        screenPower.noteInteraction(millis());
        return;
    }
    screenPower.noteInteraction(millis());

    if (lifeFSM.stage() == LifeStage::Dead) return;

    switch (currentScene) {
        case SceneColor::IdleMain:
            // Recalcular visibles antes de avanzar — preconditions pueden haber
            // cambiado desde el último click (decay, acción previa).
            renderer->refreshActionMenu(statsEngine);
            renderer->moveCursor();
            break;
        case SceneColor::StatsView:
            // Refresco inmediato: al entrar se ve el estado de red actual,
            // no el de hace NET_INFO_REFRESH_MS.
            refreshNetworkInfo_();
            currentScene = SceneColor::NetworkView;
            renderer->setScene(currentScene);
            LOG_INFO("Scene -> NetworkView (from StatsView)");
            break;
        case SceneColor::NetworkView:
            currentScene = SceneColor::IdleMain;
            renderer->setScene(currentScene);
            LOG_INFO("Scene -> IdleMain (from NetworkView)");
            break;
        case SceneColor::MessageView:
            dismissMessage_();
            break;
        default:
            break;
    }
}

// Botón largo:
//   MessageView → descarta el mensaje del padre (mismo criterio que el click corto).
//   IdleMain    → ejecuta la acción activa del HUD. Si es Stats, abre StatsView.
//   StatsView / NetworkView → no-op (long press solo tiene semántica en IdleMain).
static void onLongPress(const Event&) {
    // Pantalla apagada: el primer toque (aunque sea largo) SOLO despierta —
    // mismo criterio que onShortClick, ver comentario ahí.
    if (!screenPower.isScreenOn()) {
        screenPower.noteInteraction(millis());
        return;
    }
    screenPower.noteInteraction(millis());

    if (lifeFSM.stage() == LifeStage::Dead) return;
    if (currentScene == SceneColor::MessageView) {
        dismissMessage_();
        return;
    }
    if (currentScene != SceneColor::IdleMain) return;

    ActionKind action = renderer->currentSelectedAction();
    if (action == ActionKind::Stats) {
        currentScene = SceneColor::StatsView;
        renderer->setScene(currentScene);
        LOG_INFO("Scene -> StatsView (from HUD)");
        return;
    }

    executeSelectedAction_();
}

// StatsChanged: sincroniza la copia del renderer con el estado actual.
// Se dispara por tick de decay y por acciones del botón.
static void onStatsChanged(const Event&) {
    renderer->setStats(statsEngine.stats());
}

// PersistRequested: serializa el estado vivo y escribe en LittleFS.
// Invocado por el scheduler de 5 min y por acciones críticas.
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

// WifiStateChanged: sincroniza el icono WiFi en el renderer e inicia/detiene OTA,
// WebUI y mDNS según el estado de la conexión.
// Con WIFI_SSID vacío este handler nunca llega a Connected (WifiManager permanece
// en Disabled y no publica el evento).
static void onWifiStateChanged(const Event& e) {
    WifiState ws = static_cast<WifiState>(e.payload & 0xFF);
    LOG_INFO("[wifi] state -> %u", static_cast<unsigned>(ws));

    // Siempre sincronizar el icono WiFi y la página de red del renderer.
    if (renderer) renderer->setWifiState(ws);
    refreshNetworkInfo_();

    if (ws == WifiState::Connected) {
        // Activar OTA — requiere WiFi con IP asignada. ArduinoOTA.begin() en
        // ESP32 YA inicializa mDNS internamente (MDNS.begin(hostname) +
        // MDNS.enableArduino(), que registra el servicio _arduino._tcp usado
        // por espota para descubrir el dispositivo) — ver ArduinoOTA::begin()
        // en el framework arduino-esp32.
        ota.begin();

        // Activar WebUI — servidor HTTP en puerto 80 con endpoints REST.
        if (webUI && !webUI->isRunning()) webUI->begin();

        // mDNS: registrar además el servicio HTTP para que el dashboard sea
        // descubrible.
        //
        // FIX (doble init de mDNS): antes se llamaba MDNS.begin(OTA_HOSTNAME)
        // aquí incondicionalmente, pero si ota.begin() ya lo inicializó (caso
        // normal, OTA_PASSWORD no vacío) esta segunda llamada a mdns_init()
        // SIEMPRE fallaba (el stack mDNS de esp-idf no admite doble init) —
        // MDNS.begin() devolvía false y todo el bloque, incluido
        // MDNS.addService("http",...), se saltaba en silencio. El dashboard
        // seguía siendo alcanzable por hostname (ArduinoOTA ya lo registró)
        // pero nunca aparecía como servicio _http._tcp para clientes mDNS-SD.
        //
        // Ahora solo se llama MDNS.begin() aquí si ota.begin() NO lo hizo
        // (OTA_PASSWORD vacío en secrets.h -> OTAManager::begin() retorna
        // antes de tocar mDNS, ver OTAManager.cpp).
        bool mdnsReady = ota.isReady();
        if (!mdnsReady) {
            mdnsReady = MDNS.begin(OTA_HOSTNAME);
            if (!mdnsReady) LOG_WARN("[mdns] begin failed — continuando sin mDNS");
        }
        if (mdnsReady) {
            MDNS.addService("http", "tcp", 80);
            LOG_INFO("[mdns] hostname=%s.local  http service registered", OTA_HOSTNAME);
        }
    } else {
        // Cualquier estado distinto de Connected: detener OTA y WebUI si estaban activos.
        if (ota.isReady()) ota.end();
        if (webUI && webUI->isRunning()) webUI->end();
        // mDNS: en ESP32 no hay MDNS.end() estándar; se reinicia con begin() al reconectar.
    }
}

// -----------------------------------------------------------------------------
// netSupervisor_ — red de seguridad idempotente contra WebUI/OTA muertos con
// WiFi sano (ver plan de robustez de red — incidente real: ping/mDNS vivos
// pero puertos 80/3232 con RST porque nunca se re-invocó begin() tras un
// reconnect de bajo nivel).
//
// Complementa a WifiManager::onEvent (fuente rápida, ver WifiManager.cpp):
// esto es la última línea de defensa por si CUALQUIER causa (evento perdido,
// orden de arranque, etc.) deja isRunning()/isReady() en false con WiFi
// Connected. begin() en ambos módulos es idempotente (no-op si ya estaban
// arriba — ver WebUI::begin()/OTAManager::begin() y el fork AsyncTCP
// verificado en el PASO 0 del plan: AsyncServer::begin() no re-crea el pcb
// si _pcb ya existe), así que llamarlo de más nunca fuga PCBs LWIP.
//
// Corre en loopTask (llamado desde loop()) — nunca desde async_tcp, así que
// es seguro tocar renderer/estado global si hiciera falta (aquí no hace falta).
// -----------------------------------------------------------------------------
static void netSupervisor_() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (webUI && !webUI->isRunning()) {
        LOG_WARN("[net-supervisor] WebUI caído con WiFi conectado — relevantando begin()");
        webUI->begin();
    }
    if (!ota.isReady()) {
        LOG_WARN("[net-supervisor] OTA caído con WiFi conectado — relevantando begin()");
        ota.begin();
    }
}

// -----------------------------------------------------------------------------
// connectivityWatchdog_ — última línea de defensa cuando netSupervisor_() no
// basta: si el WiFi lleva caído más de WIFI_DEAD_RESTART_MS SEGUIDOS (no el
// uptime total, ver ConnectivityWatchdogLogic.h) habiendo estado conectado
// antes, algo en el stack de red quedó en un estado que ni el re-begin
// idempotente recupera. Reiniciar el chip es la única salida, con guarda
// anti-boot-loop (BootLoopGuard) para no entrar en ciclo infinito si el
// router está apagado de verdad.
//
// Sin conexión previa (wifi.hasEverConnected()==false) NUNCA reinicia por
// esto — un dispositivo que arrancó sin WiFi disponible (router apagado
// desde el boot) debe poder seguir jugando offline indefinidamente.
//
// BUG DE CAMPO corregido aquí: la condición de reinicio usaba
// wifi.lastConnectedMs() (instante de la ÚLTIMA conexión, no de la caída) —
// con uptime > WIFI_DEAD_RESTART_MS, cualquier microcorte de WiFi disparaba
// un reinicio inmediato en vez de esperar los 5 min de caída continua.
// wifi.isWifiDeadFor() usa wifiDownSinceMs_ (instante de la PRIMERA pérdida
// de la racha actual), que es la marca correcta para esta pregunta.
// -----------------------------------------------------------------------------
static void connectivityWatchdog_() {
    if (!wifi.hasEverConnected()) return;

    uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        // Conexión estable durante BOOT_LOOP_RESET_STABLE_MS -> restaurar
        // confianza del anti-boot-loop (idempotente, ver BootLoopGuard.h).
        if (now - wifi.lastConnectedMs() >= BOOT_LOOP_RESET_STABLE_MS) {
            BootLoopGuard::reportStableConnection();
        }
        return;
    }

    if (!wifi.isWifiDeadFor(WIFI_DEAD_RESTART_MS)) return;

    if (!BootLoopGuard::allowRestart()) {
        LOG_ERROR("[net-watchdog] WiFi caido >%lu ms — boot-loop guard agotado (%u/%u), "
                  "modo degradado (sin reinicio)",
                  (unsigned long)WIFI_DEAD_RESTART_MS,
                  (unsigned)BootLoopGuard::count(), (unsigned)BOOT_LOOP_MAX);
        return;
    }

    LOG_ERROR("[net-watchdog] WiFi caido >%lu ms — reiniciando (intento %u/%u)",
              (unsigned long)WIFI_DEAD_RESTART_MS,
              (unsigned)BootLoopGuard::count(), (unsigned)BOOT_LOOP_MAX);
    // Persistir antes de reiniciar — el bus es síncrono (misma garantía que
    // handlePostReboot_ en WebUI), así que al llegar al delay() de abajo el
    // estado ya está a salvo en LittleFS.
    bus.publish({EventType::PersistRequested, 0});
    delay(2000);  // margen para que el log salga por serie antes del reset
    ESP.restart();
}

// OtaStarted: cambiar a pantalla de progreso OTA (misma lógica que ESP8266).
//
// noteInteraction() aquí es imprescindible, no cosmético: inhibitSleep=true
// (ver loop()) SOLO impide apagar una pantalla que ya está encendida — NO
// enciende una que ya estaba dormida. Sin este noteInteraction(), un OTA
// lanzado por red tras >SCREEN_TIMEOUT_MS de inactividad dejaría la barra de
// progreso invisible (update() seguiría devolviendo None indefinidamente y
// el render seguiría saltado). onOtaStarted corre en loopTask (invocado
// síncronamente por ArduinoOTA.onStart() dentro de ota.loop(), al principio
// de este mismo loop() — ver OTAManager::onStart_), así que el
// screenPower.update() de esta misma vuelta ya ve currentScene==OtaProgress
// e idle≈0 y hace el Wake ANTES del bloque de render de esta vuelta.
static void onOtaStarted(const Event&) {
    LOG_INFO("[ota] update started — switching to OtaProgress scene");
    screenPower.noteInteraction(millis());
    if (renderer) {
        renderer->setScene(SceneColor::OtaProgress);
        renderer->setOtaProgress(0);
    }
    currentScene = SceneColor::OtaProgress;
}

// OtaProgress: actualizar la barra de progreso en la pantalla TFT.
static void onOtaProgress(const Event& ev) {
    uint8_t pct = static_cast<uint8_t>(ev.payload & 0xFF);
    if (renderer) renderer->setOtaProgress(pct);
}

// OtaEnded: el ESP32 reinicia automáticamente tras este evento.
static void onOtaEnded(const Event&) {
    LOG_INFO("[ota] update ended — reboot imminent");
}

// OtaError: restaurar escena anterior y loguear el código de error.
static void onOtaError(const Event& ev) {
    LOG_ERROR("[ota] error code=%d", static_cast<int>(ev.payload));
    if (renderer) renderer->setScene(SceneColor::IdleMain);
    currentScene = SceneColor::IdleMain;
}

// BatteryStateChanged: decodifica el payload empaquetado por BatteryMonitor
// (ver comentario de codificación en EventBus.h) y lo deja listo para la UI.
static void onBatteryStateChanged(const Event& e) {
    uint8_t percent  = static_cast<uint8_t>(e.payload & 0x7F);
    bool    charging = (e.payload & (1 << 7)) != 0;
    bool    valid    = (e.payload & (1 << 8)) != 0;
    LOG_DEBUG("[battery] evento valid=%d percent=%u%% charging=%d",
              valid ? 1 : 0, percent, charging ? 1 : 0);
    if (renderer) renderer->setBattery(percent, charging, valid);
}

// Acciones publicadas por el HUD → aplicar a StatsEngine + persistir.
// Misma semántica que en main.cpp (ESP8266), sin WebUI en esta fase.
//
// noteInteraction() al inicio de cada una: pueden llegar desde WebUI (task
// async_tcp, POST /api/v1/*) además del botón (loopTask) — un comando
// explícito del dashboard cuenta como interacción igual que el botón. NO se
// toca hardware aquí (solo lastInteractionMs_, volatile — ver
// ScreenPowerLogic.h); el wake real lo aplica loop() en la siguiente vuelta.
// A diferencia del botón, la acción SIEMPRE se ejecuta con normalidad aunque
// la pantalla estuviera apagada — un comando HTTP explícito no debe perderse.

static void onActionFeed(const Event& e) {
    screenPower.noteInteraction(millis());
    statsEngine.applyFeed(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] feed +%d -> hunger=%u", static_cast<int>(e.payload), statsEngine.stats().hunger);
}

static void onActionPlay(const Event& e) {
    screenPower.noteInteraction(millis());
    statsEngine.applyPlay(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] play +%d -> happiness=%u", static_cast<int>(e.payload), statsEngine.stats().happiness);
}

static void onActionClean(const Event&) {
    screenPower.noteInteraction(millis());
    statsEngine.applyClean();
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] clean -> poop=%u", statsEngine.stats().poopCount);
}

static void onActionMedicate(const Event&) {
    screenPower.noteInteraction(millis());
    statsEngine.applyMedicate();
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] medicate -> isSick=%u health=%u",
             statsEngine.stats().isSick, statsEngine.stats().health);
}

static void onActionDiscipline(const Event& e) {
    screenPower.noteInteraction(millis());
    statsEngine.applyDiscipline(static_cast<uint8_t>(e.payload));
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] discipline +%d -> discipline=%u",
             static_cast<int>(e.payload), statsEngine.stats().discipline);
}

static void onActionLights(const Event& e) {
    screenPower.noteInteraction(millis());
    bool on = (e.payload != 0);
    statsEngine.applyLights(on);
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("[action] lights %s -> lightsOn=%u", on ? "on" : "off",
             statsEngine.stats().lightsOn);
}

// LifeStateChanged: reacciona a transiciones del LifeFSM.
// Actualiza renderer, activa DeadScreen, detecta resurrección.
// Persiste inmediatamente — el cambio de etapa es un evento crítico.
static void onLifeStateChanged(const Event& e) {
    LifeStage newStage = static_cast<LifeStage>((e.payload >> 8) & 0xFF);
    uint8_t   subtype  = static_cast<uint8_t>(e.payload & 0xFF);

    LOG_INFO("Life: stage=%u -> %u (subtype=%u)",
             static_cast<unsigned>(previousStage_),
             static_cast<unsigned>(newStage),
             subtype);

    renderer->setLifeStage(newStage);
    if (newStage == LifeStage::Adult) {
        renderer->setAdultType(subtype);
    }

    // Animaciones one-shot de transición (mismas que el main ESP8266).
    // Eclosión Egg → Baby: hatch animation 2.4 s.
    if (newStage == LifeStage::Baby && previousStage_ == LifeStage::Egg) {
        renderer->triggerHatchAnimation();
    }
    // Crecimiento Baby/Kid/Adult → siguiente etapa: evolve blink 1.2 s.
    else if ((newStage == LifeStage::Kid   && previousStage_ == LifeStage::Baby) ||
             (newStage == LifeStage::Adult && previousStage_ == LifeStage::Kid)  ||
             (newStage == LifeStage::Old   && previousStage_ == LifeStage::Adult)) {
        renderer->triggerEvolveAnimation();
    }

    if (newStage == LifeStage::Dead) {
        renderer->setLastDeathCause(lifeFSM.lastDeathCause());
        renderer->setAgeMinutes(lifeFSM.ageMinutes());
        renderer->setScene(SceneColor::DeadScreen);
        currentScene = SceneColor::DeadScreen;
    }

    // Resurrección: Dead → Egg forzada por LifeFSM tras kResurrectionMs.
    if (newStage == LifeStage::Egg && previousStage_ == LifeStage::Dead) {
        LOG_INFO("Resurrection: new egg — resetting stats");
        statsEngine.resetForNewLife();
        currentScene = SceneColor::IdleMain;
        renderer->setScene(SceneColor::IdleMain);
        // Sincronizar renderer con stats frescos del nuevo ciclo de vida.
        bus.publish({EventType::StatsChanged, 0});
    }

    previousStage_ = newStage;

    // Persistir inmediatamente — cambio de etapa es evento crítico.
    bus.publish({EventType::PersistRequested, 0});
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    LOG_INFO("Boot ESP32-S3 Fase 5 (WiFi+OTA+WebUI) — version=0x%04X", FIRMWARE_VERSION);

    // Anti-boot-loop del watchdog de conectividad (commit 3) — lo primero,
    // antes de cualquier init que pudiera decidir reiniciar. Detecta arranque
    // en frío (power-on/brownout) y resetea el contador; en un soft-reset lo
    // preserva. Ver BootLoopGuard.h para el diseño completo.
    BootLoopGuard::begin();

#ifdef TURBO_MODE
    LOG_WARN("=================================================");
    LOG_WARN("== TURBO_MODE ACTIVE - factor %u", (unsigned)TURBO_FACTOR);
    LOG_WARN("== ESTE BINARIO NO DEBE LLEGAR A PRODUCCION   ==");
    LOG_WARN("=================================================");
#endif

    // ── Backlight ─────────────────────────────────────────────────────────────
    // Activar antes del begin() del panel para que el usuario vea el splash.
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // ── Panel TFT ─────────────────────────────────────────────────────────────
    if (!tft->begin(27000000)) {
        LOG_ERROR("tft->begin() fallo — pantalla no responde");
        // Sin pantalla no se puede mostrar nada. Reiniciar y reintentar el
        // boot es mejor que colgar para siempre (antes: while(true){delay});
        // un fallo transitorio (glitch de arranque del panel) se recupera
        // solo en el siguiente boot. delay(2000) da tiempo a que el log
        // salga por el puerto serie antes del reset.
        delay(2000);
        ESP.restart();
    }
    LOG_INFO("TFT begin OK");

    // Limpiar el GRAM nativo completo (240×320) antes de montar el canvas.
    // El panel físico tiene 320 líneas de GRAM, pero el canvas solo usa 296 con
    // row_offset=12 → las filas 0-11 y 308-319 nunca se escriben y muestran
    // basura aleatoria que asoma por las esquinas redondeadas del cristal.
    static_cast<Arduino_JD9853*>(tft)->clearNativePanel(0x0000);

    // ── Canvas framebuffer en PSRAM ───────────────────────────────────────────
    if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        LOG_ERROR("canvas->begin() fallo — PSRAM no disponible");
        // Mismo criterio que el fallo de tft->begin() arriba: reset y
        // reintentar en vez de colgar para siempre.
        delay(2000);
        ESP.restart();
    }
    LOG_INFO("Canvas OK. PSRAM libre: %u B", ESP.getFreePsram());

    // Splash animado estilo Game Boy DMG.
    // TODO(guillermo): disparar ding con tone() si se añade buzzer pasivo a un GPIO
    playBootSplash(canvas);

    // ── LittleFS ──────────────────────────────────────────────────────────────
    // Un único begin() compartido por SpriteStore y Persistence.
    // format-on-fail: si el FS está corrupto, se reformatea limpio.
    // En primer boot tras flash del FS: NoneFound → defaults → se crea en primer save.
    if (!LittleFS.begin(/*formatOnFail=*/true)) {
        LOG_ERROR("LittleFS begin failed incluso tras format — persistencia y sprites sin soporte");
        // No fatal: el juego arranca con defaults, sin sprites y sin persistencia.
    } else {
        LOG_INFO("LittleFS montado OK");
    }

    // ── SpriteStore — carga manifest y frames desde LittleFS a PSRAM ──────────
    if (!spriteStore.begin("/sprites/manifest.json")) {
        LOG_WARN("SpriteStore.begin() fallo — sube el filesystem (pio run -t uploadfs)");
        // No fatal: el renderer dibuja en modo degradado (sin sprites) si los
        // ColorSpriteFrames no están cargados. Se muestra pantalla con stats.
    } else {
        LOG_INFO("SpriteStore OK. PSRAM libre post-sprites: %u B", ESP.getFreePsram());
    }

    // ── Inyección de RNG en StatsEngine ───────────────────────────────────────
    // StatsEngine usa rand() por defecto. En ESP32-S3 sembramos con el TRNG
    // hardware (esp_random()) para que la aleatoriedad (spawns de caca, enfermedades)
    // sea distinta en cada boot. El puntero de función sRand01_ se reemplaza con
    // una lambda sin captura que normaliza esp_random() a [0,1).
    srand(esp_random());  // siembra el rand() estándar con entropía hardware
    // StatsEngine.sRand01_ continuará usando defaultRand01_ (rand()/RAND_MAX),
    // que ahora tiene semilla aleatoria. No hace falta setRandomSource() adicional.

    // ── Pausa de crecimiento — ANTES de cargar Persistence ───────────────────
    // restoreLifeState() (más abajo) degrada al tope un pet guardado en una
    // etapa superior, así que el tope tiene que estar puesto antes de la carga.
#if GROWTH_CAP_ENABLED
    lifeFSM.setGrowthCap(static_cast<LifeStage>(GROWTH_CAP_STAGE));
    LOG_INFO("[life] growth cap activo: %s", JsonCodec::lifeStageToString(lifeFSM.growthCap()));
#endif

    // ── Cargar estado persistido ───────────────────────────────────────────────
    PersistedState loaded;
    LoadResult lr = persistence.load(loaded);
    LOG_INFO("Persist load: %s (hunger=%u happ=%u health=%u stage=%u age=%u deathCount=%u)",
             loadResultName(lr),
             loaded.hunger, loaded.happiness, loaded.health,
             loaded.lifeStage, loaded.ageMinutes, loaded.deathCount);

    // Manejar casos de migración como el original: Migrated = aplicar normalmenete.
    // MigrationFailed = usar defaults (ya los tiene fillDefaults en persistence.load).
    if (lr == LoadResult::MigrationFailed) {
        LOG_WARN("Persist: migración fallida — usando estado inicial (nuevo ciclo)");
    }

    // Aplicar estado cargado a StatsEngine y LifeFSM.
    // restoreLifeState() carga etapa + edad aplicando el tope de crecimiento:
    // si la etapa guardada lo supera (y no es Dead) la degrada al tope con la
    // edad de entrada a esa etapa. Los stats (applyToStatsEngine) no cambian.
    applyToStatsEngine(loaded, statsEngine);
    const LifeStage savedStage = static_cast<LifeStage>(loaded.lifeStage);
    const bool degradedByGrowthCap = lifeFSM.restoreLifeState(savedStage, loaded.ageMinutes);
    lifeFSM.setDeathSustainMs(loaded.deathSustainMs);
    if (degradedByGrowthCap) {
        LOG_WARN("[life] growth cap: etapa guardada %s (age=%u) supera el tope %s "
                 "-> degradada a %s (age=%u), stats conservados",
                 JsonCodec::lifeStageToString(savedStage),
                 static_cast<unsigned>(loaded.ageMinutes),
                 JsonCodec::lifeStageToString(lifeFSM.growthCap()),
                 JsonCodec::lifeStageToString(lifeFSM.stage()),
                 static_cast<unsigned>(lifeFSM.ageMinutes()));
    }

    // Inicializar previousStage_ con el estado cargado para detectar correctamente
    // la resurrección Dead→Egg en el primer tick tras el boot.
    previousStage_ = lifeFSM.stage();

    // ── DisplayRendererColor ───────────────────────────────────────────────────
    renderer = new DisplayRendererColor(*canvas, bus, spriteStore);
    renderer->begin();

    // Sincronizar renderer con el estado cargado desde flash.
    renderer->setStats(statsEngine.stats());
    renderer->setLifeStage(lifeFSM.stage());
    if (lifeFSM.stage() == LifeStage::Adult) {
        renderer->setAdultType(loaded.adultType);
    }
    renderer->setLastDeathCause(loaded.lastDeathCauseCode);
    // Edad de LifeFSM, no la de flash: con la pausa de crecimiento puede haber
    // sido degradada o clampada en restoreLifeState().
    renderer->setAgeMinutes(lifeFSM.ageMinutes());
    renderer->setDeathCount(loaded.deathCount);
    // WiFi Fase 5: estado inicial Disabled hasta que WifiManager::begin() conecte.
    // Si WIFI_SSID está vacío en secrets.h, WifiManager permanece Disabled y
    // nunca publica WifiStateChanged, por lo que el icono muestra Disabled offline.
    renderer->setWifiState(WifiState::Disabled);

    // Inicializar escena según el estado cargado.
    if (lifeFSM.stage() == LifeStage::Dead) {
        currentScene = SceneColor::DeadScreen;
    } else {
        currentScene = SceneColor::IdleMain;
    }
    renderer->setScene(currentScene);

    // Poblar el HUD con las acciones aplicables al estado cargado para que el
    // primer frame ya muestre el cursor en una entrada válida.
    renderer->refreshActionMenu(statsEngine);

    // Render inmediato del primer frame (sustituye el splash de texto).
    renderer->render();

    // ── InputHandler — TTP223 / GPIO4 ────────────────────────────────────────
    // active-HIGH (módulo TTP223 drivea OUT; en reposo LOW, al tocar HIGH).
    // Sin pull-up interno — el módulo impone el nivel de salida.
    inputHandler = new InputHandler(bus, PIN_BUTTON,
                                    BUTTON_ACTIVE_LOW,
                                    BUTTON_PULLUP_ACTIVE);
    inputHandler->begin();
    LOG_INFO("TTP223 configurado en GPIO%d (activeLow=%s, pullup=%s)",
             PIN_BUTTON,
             BUTTON_ACTIVE_LOW    ? "true" : "false",
             BUTTON_PULLUP_ACTIVE ? "true" : "false");

    // ── WebUI — construir en heap tras renderer y bus para inyectar dependencias ─
    // WebUI publica eventos al bus; los handlers de acción en main los procesan.
    // Se activa solo cuando WiFi Connected (en onWifiStateChanged).
    //
    // WebUI solo depende de BatteryGauge (lib/core, puro) — no de BatteryMonitor
    // (infra ESP32-only) — ver comentario del ctor en WebUI.h. Con
    // BATTERY_SENSE_ENABLED=0 se pasa nullptr explícitamente: aunque `battery`
    // siempre existe como objeto, su gauge_ nunca deja Unknown en ese caso, así
    // que nullptr documenta la intención sin cambiar el comportamiento observable.
    //
    // &messageBox (6º parámetro, ver WebUI.h) se pasa en AMBAS ramas — el
    // mensaje del padre es independiente del sensor de batería.
#if BATTERY_SENSE_ENABLED
    webUI = new WebUI(bus, statsEngine, lifeFSM, wifi, &battery.gauge(), &messageBox);
#else
    webUI = new WebUI(bus, statsEngine, lifeFSM, wifi, nullptr, &messageBox);
#endif

    // ── Suscripciones EventBus ────────────────────────────────────────────────
    // Llamar en setup(), nunca en loop() — EventBus no es thread-safe.
    bus.subscribe(EventType::ButtonShortClick,        onShortClick);
    bus.subscribe(EventType::ButtonLongPress,          onLongPress);
    bus.subscribe(EventType::StatsChanged,             onStatsChanged);
    bus.subscribe(EventType::PersistRequested,         onPersistRequested);
    bus.subscribe(EventType::LifeStateChanged,         onLifeStateChanged);
    bus.subscribe(EventType::WifiStateChanged,         onWifiStateChanged);
    bus.subscribe(EventType::BatteryStateChanged,       onBatteryStateChanged);
    // OTA events — Fase 5
    bus.subscribe(EventType::OtaStarted,              onOtaStarted);
    bus.subscribe(EventType::OtaProgress,             onOtaProgress);
    bus.subscribe(EventType::OtaEnded,                onOtaEnded);
    bus.subscribe(EventType::OtaError,                onOtaError);
    // Acciones HUD y WebUI (mismas que main.cpp ESP8266)
    bus.subscribe(EventType::ActionFeedRequested,      onActionFeed);
    bus.subscribe(EventType::ActionPlayRequested,      onActionPlay);
    bus.subscribe(EventType::ActionCleanRequested,     onActionClean);
    bus.subscribe(EventType::ActionMedicateRequested,  onActionMedicate);
    bus.subscribe(EventType::ActionDisciplineRequested, onActionDiscipline);
    bus.subscribe(EventType::ActionLightsRequested,    onActionLights);

    // Pausa de crecimiento: si la carga degradó la etapa, persistir ya (save
    // síncrono vía onPersistRequested, recién suscrito) en vez de esperar al
    // autosave de 5 min. Un fallo lo loguea onPersistRequested; aun así la
    // degradación es idempotente: el siguiente boot volvería a aplicarla.
    if (degradedByGrowthCap) {
        LOG_INFO("[life] growth cap: guardando el estado degradado (stage=%s age=%u)",
                 JsonCodec::lifeStageToString(lifeFSM.stage()),
                 static_cast<unsigned>(lifeFSM.ageMinutes()));
        bus.publish({EventType::PersistRequested, 0});
    }

    // ── WiFi no bloqueante — Fase 5 ───────────────────────────────────────────
    // begin() devuelve en < 5 ms. Si WIFI_SSID está vacío en secrets.h, marca
    // Disabled y no hace nada más. El juego sigue funcionando offline.
    wifi.begin();
    // begin() deja Connecting (o Disabled con SSID vacío) SIN publicar
    // WifiStateChanged — sincronizar a mano icono y página de red para que no
    // muestren "Disabled" hasta la primera transición (Connected o timeout).
    renderer->setWifiState(wifi.state());
    refreshNetworkInfo_();

    // ── Medidor de batería — independiente de WiFi, funciona offline ─────────
    battery.begin();

    // ── Task Watchdog Timer — loopTask ────────────────────────────────────────
    // Se activa aquí, tras todos los inits pesados y bloqueantes de arriba
    // (TFT/canvas/LittleFS/SpriteStore/WiFi.begin() no bloqueante) — así el
    // primer esp_task_wdt_reset() (final de loop()) llega muy por debajo de
    // WDT_TIMEOUT_S. panic=true: si loop() se cuelga sin alimentar el
    // watchdog, el chip resetea solo (antes: cero recuperación posible sin
    // ir físicamente a pulsar reset).
    //
    // Solo vigila loopTask (NULL = task actual = loopTask en Arduino-ESP32).
    // NO se añade la task async_tcp — la gestiona ESPAsyncWebServer/AsyncTCP
    // internamente y añadirla aquí daría falsos positivos (ver plan).
    if (esp_task_wdt_init(WDT_TIMEOUT_S, true) == ESP_OK) {
        esp_task_wdt_add(NULL);
        LOG_INFO("TWDT activo: timeout=%ds panic=true", WDT_TIMEOUT_S);
    } else {
        LOG_ERROR("esp_task_wdt_init() fallo — TWDT deshabilitado");
    }

    // ── Schedulers: inicializar todos a now para evitar ticks espurios al boot ─
    uint32_t now = millis();
    lastStatsTickMs = now;
    lastRenderMs    = now;
    lastSaveMs      = now;   // evita un save espurio en el primer tick de autosave
    lastFpsReportMs = now;
    lastNetSupervisorMs = now;
    lastNetInfoRefreshMs = now;
    // Pantalla arranca encendida (backlight ya en HIGH, ver arriba) — sella
    // el reloj de inactividad en el boot para no contar el tiempo de arranque
    // (splash, LittleFS, sprites...) como idle.
    screenPower.noteInteraction(now);

    LOG_INFO("Setup done (stage=%u ageMin=%u heap=%u psram=%u wifi=%s)",
             static_cast<unsigned>(lifeFSM.stage()),
             lifeFSM.ageMinutes(),
             ESP.getFreeHeap(),
             ESP.getFreePsram(),
             (strlen(WIFI_SSID) == 0) ? "disabled" : "connecting");
}

// =============================================================================
// loop
// =============================================================================
void loop() {
    // ── Red: WiFi poll + OTA + WebUI ─────────────────────────────────────────
    // Guard internos en cada módulo: coste < 1 µs cuando no hay actividad.
    // Con WIFI_SSID vacío, wifi.loop() retorna inmediatamente (state==Disabled).
    wifi.loop();
    ota.loop();
    // AsyncWebServer es event-driven; webUI->loop() es actualmente un no-op
    // (reservado para extensiones futuras como WebSockets).
    if (webUI) webUI->loop();
    // En ESP32 NO llamar MDNS.update() — no existe / no es necesario.

    // ── Medidor de batería — guard interno kSampleIntervalMs=2000 ────────────
    battery.loop();

    // OneButton necesita tick() en cada iteración para detectar gestos.
    // Los handlers onShortClick/onLongPress se invocan síncronamente desde aquí.
    if (inputHandler) {
        inputHandler->loop();
    }

    // ── Mensaje del padre: consumir de MessageBox y mostrar en pantalla ──────
    // Consumidor SPSC (ver MessageBox.h) — el productor es el handler HTTP de
    // WebUI, que corre en la task async_tcp. Sin acuse de recibo: el POST ya
    // respondió 200 al aceptar, independientemente de si aquí se muestra o se
    // descarta (p.ej. mascota muerta o en OtaProgress).
    if (messageBox.hasPending()) {
        char text[MessageBox::kMaxLen + 1];
        messageBox.consume(text, sizeof(text));

        const bool canShow = (lifeFSM.stage() != LifeStage::Dead)
                           && (currentScene == SceneColor::IdleMain
                               || currentScene == SceneColor::StatsView
                               || currentScene == SceneColor::NetworkView
                               || currentScene == SceneColor::MessageView);

        if (canShow) {
            // Cuenta como interacción — igual que el botón o una acción HTTP
            // (ver plan). NOTA: se usa millis() directo (no la variable local
            // `now`, que en este punto del loop aún no se ha calculado —
            // se declara más abajo, justo antes del bloque de decay) — la
            // diferencia de timing entre ambas llamadas es de microsegundos,
            // irrelevante frente al timeout de 3 min.
            screenPower.noteInteraction(millis());

            // No pisar sceneBeforeMessage_ si ya estábamos en MessageView
            // (un segundo mensaje mientras se muestra el primero no debe
            // perder la escena ORIGINAL a la que hay que volver al terminar).
            if (currentScene != SceneColor::MessageView) {
                sceneBeforeMessage_ = currentScene;
            }
            renderer->showMessage(text);
            currentScene = SceneColor::MessageView;
            renderer->setScene(currentScene);
            LOG_INFO("Message: mostrando mensaje del padre (%u chars)",
                     static_cast<unsigned>(strlen(text)));
        } else {
            LOG_WARN("Message: descartado (scene=%u stage=%u no permite mostrarlo)",
                      static_cast<unsigned>(currentScene),
                      static_cast<unsigned>(lifeFSM.stage()));
        }
    }

    // ── Mensaje del padre: restaurar la escena previa al terminar el marquee ──
    // messageActive()==false cuando expira el timer de 60s o se agotan las
    // vueltas permitidas (kMessageMaxPasses) — el descarte por botón (ver
    // dismissMessage_) ya restaura la escena de forma síncrona, así que este
    // bloque solo dispara en el camino de "se dejó terminar solo".
    if (currentScene == SceneColor::MessageView && !renderer->messageActive()) {
        currentScene = sceneBeforeMessage_;
        renderer->setScene(currentScene);
        LOG_INFO("Scene -> %u (mensaje del padre terminado)", static_cast<unsigned>(currentScene));
    }

    uint32_t now = millis();

    // ── Página de red visible: refrescar RSSI/IP cada NET_INFO_REFRESH_MS ────
    // Los cambios de estado ya llegan al instante por onWifiStateChanged; esto
    // solo mueve las barras de señal. Antes del wake de pantalla para que el
    // primer frame tras despertar no muestre datos viejos.
    if (currentScene == SceneColor::NetworkView
            && now - lastNetInfoRefreshMs >= NET_INFO_REFRESH_MS) {
        refreshNetworkInfo_();
    }

    // ── Apagado de pantalla por inactividad (SCREEN_TIMEOUT_MS) ──────────────
    // inhibitSleep: no apagar mientras hay una actualización OTA visible en
    // pantalla (el usuario necesita ver la barra de progreso). ScreenPowerLogic
    // decide; sleepScreen()/wakeScreen() aplican el efecto físico — SIEMPRE
    // desde aquí (loopTask), nunca desde un handler de evento.
    bool inhibitSleep = (currentScene == SceneColor::OtaProgress);
    switch (screenPower.update(now, inhibitSleep)) {
        case ScreenPowerLogic::Action::Sleep: sleepScreen(); break;
        case ScreenPowerLogic::Action::Wake:  wakeScreen();  break;
        default: break;
    }

    // ── Decay a 1 Hz (STATS_TICK_MS = 1000 ms normal, 16 ms en TURBO_MODE) ───
    //
    // StatsEngine::tick() solo cuando stage NO es Egg ni Dead:
    //   - Huevos no tienen decay (stats congelados hasta eclosión).
    //   - Muertos no tienen decay (stats son "epitafio" de la muerte).
    // LifeFSM::tick() SIEMPRE: avanza edad, timers internos (deathSustainMs,
    //   deadElapsedMs), incluido el timer de resurrección.
    if (now - lastStatsTickMs >= STATS_TICK_MS) {
        uint32_t dt = now - lastStatsTickMs;
        lastStatsTickMs = now;

        bool wasSleeping = statsEngine.stats().isSleeping;

        LifeStage stage = lifeFSM.stage();
        if (stage != LifeStage::Egg && stage != LifeStage::Dead) {
            statsEngine.tick(dt);
        }
        // LifeFSM aplica LIFE_FSM_TIME_SCALE internamente (en LifeFSM.cpp).
        // Se pasa dt crudo, igual que en main.cpp (ESP8266).
        lifeFSM.tick(dt, statsEngine.stats(), statsEngine.stats().attentionScore);

        // Propagar la edad al renderer para el mini-contador en IdleMain.
        renderer->setAgeMinutes(lifeFSM.ageMinutes());

        // Publicar StatsChanged solo si algún stat entero cambió (dirty flag).
        if (statsEngine.consumeDirtyFlag()) {
            bus.publish({EventType::StatsChanged, 0});
        }

        // Auto sleep/wake (ver StatsEngine::tick): persistir de inmediato para
        // no perder la transición si hay un power-loss antes del autosave de
        // 5 min — energy/isSleeping/lightsOn ya viajan en PersistedState v3.
        bool isSleepingNow = statsEngine.stats().isSleeping;
        if (isSleepingNow != wasSleeping) {
            LOG_INFO("[sleep] auto %s (energy=%u)", isSleepingNow ? "sleep" : "wake",
                     statsEngine.stats().energy);
            bus.publish({EventType::PersistRequested, 0});
        }
    }

    // ── Render a cadencia RENDER_FRAME_MS (200 ms → 5 FPS) ───────────────────
    // screenPower.isScreenOn(): con la pantalla apagada, saltar el render por
    // completo — el juego (decay/energía/LifeFSM, arriba) sigue corriendo.
    if (screenPower.isScreenOn() && now - lastRenderMs >= RENDER_FRAME_MS) {
        if (renderer) {
            renderer->render();
            framesRendered++;
        }
        lastRenderMs = now;
    }

    // ── Autosave cada PERSIST_AUTOSAVE_MS (5 min wall-clock) ─────────────────
    // No escala con TURBO_MODE — evita escrituras de flash innecesarias en tests.
    if (now - lastSaveMs >= PERSIST_AUTOSAVE_MS) {
        bus.publish({EventType::PersistRequested, 0});
        lastSaveMs = now;
    }

    // ── Supervisor de red cada NET_SUPERVISOR_INTERVAL_MS (~5 s) ─────────────
    // Ver netSupervisor_() — red de seguridad idempotente, complementa a
    // WifiManager::onEvent (fuente rápida) contra WebUI/OTA muertos con WiFi vivo.
    // connectivityWatchdog_() comparte el mismo intervalo — última línea de
    // defensa si el re-begin no basta (ver su comentario).
    if (now - lastNetSupervisorMs >= NET_SUPERVISOR_INTERVAL_MS) {
        netSupervisor_();
        connectivityWatchdog_();
        lastNetSupervisorMs = now;
    }

    // ── Reporte de FPS real cada FPS_REPORT_MS (5 s) ─────────────────────────
    if (now - lastFpsReportMs >= FPS_REPORT_MS) {
        float elapsed = (now - lastFpsReportMs) / 1000.0f;
        float fps     = framesRendered / elapsed;
        Serial.printf("[FPS] %.2f fps (%u frames en %.1fs)  heap=%u  psram=%u\n",
                      fps, framesRendered, elapsed,
                      ESP.getFreeHeap(), ESP.getFreePsram());
        framesRendered  = 0;
        lastFpsReportMs = now;
    }

    // ── Task Watchdog Timer — alimentar al final de cada vuelta ──────────────
    // Debe ser SIEMPRE la última línea de loop(): si algo entre aquí y el
    // próximo esp_task_wdt_reset() se cuelga (p.ej. un futuro cambio que
    // bloquee), el TWDT lo detecta en WDT_TIMEOUT_S y resetea el chip.
    esp_task_wdt_reset();
}

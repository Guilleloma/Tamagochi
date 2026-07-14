// =============================================================================
// WifiManager.cpp — implementación de conexión WiFi STA no bloqueante.
//
// Cumple rules/security.md:
//   - Credenciales solo desde secrets.h (gitignored), nunca hardcodeadas.
//   - Log de SSID permitido; password nunca se loguea.
//
// Cumple rules/error-handling.md:
//   - Nunca swallow errors: todos los cambios de estado se loguean.
//   - Fail fast: si SSID vacío, Disabled inmediato.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native (Arduino-only)

// Guard explícito: si secrets.h no existe, el error en compile-time es descriptivo.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Crea include/secrets.h desde include/secrets.h.example antes de compilar."
#endif

#include "WifiManager.h"
#include "EventBus.h"
#include "Logger.h"
// Platform-conditional WiFi header: ESP32 uses <WiFi.h>, ESP8266 uses <ESP8266WiFi.h>.
// Both expose the same WiFi API surface used here (WiFi.mode/begin/status/localIP/RSSI).
#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif
#include <Arduino.h>

WifiManager::WifiManager(EventBus& bus)
    : bus_(bus)
{}

// -----------------------------------------------------------------------------
// begin — configura modo STA y lanza WiFi.begin(). No bloqueante.
//
// Si WIFI_SSID está vacío (valor por defecto en secrets.h.example),
// marca state = Disabled y retorna sin tocar el hardware WiFi.
// Esto garantiza que el Tamagochi funcione offline sin intentar conectar.
// -----------------------------------------------------------------------------
void WifiManager::begin() {
    if (strlen(WIFI_SSID) == 0) {
        state_ = WifiState::Disabled;
        LOG_INFO("[wifi] SSID vacio en secrets.h — WiFi desactivado");
        return;
    }

    // persistent(false): no escribe credenciales en la flash interna del ESP.
    // Evita desgaste innecesario de flash y evita que credenciales antiguas
    // persistan entre reflasheos del firmware.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // setSleep(false): sin esto el modem duerme entre DTIM beacons y el router
    // (Xiaomi, DTIM alto) puede retrasar/perder paquetes entrantes en general
    // (HTTP, mDNS, UDP), afectando la latencia y estabilidad de la conexión.
    // Coste: ~20-40 mA extra de media.
    //
    // CORRECCIÓN (ver commit fix/ota-port-esp32): esto NO fue la causa del OTA
    // roto en ESP32-S3. La causa real era `ArduinoOTA.setPort(8266)` hardcodeado
    // en OTAManager.cpp — puerto del ESP8266 arrastrado en el porting — mientras
    // espota/ArduinoOTA en ESP32 sondea por defecto el puerto 3232 (ver
    // OTAManager.cpp). setSleep(false) se mantiene porque sigue siendo una
    // mejora genuina de latencia/estabilidad de red, no como "fix" de OTA.
    WiFi.setSleep(false);
    WiFi.hostname(OTA_HOSTNAME);

#if defined(ARDUINO_ARCH_ESP32)
    // Fuente de verdad rápida de conectividad (ver comentario de clase en el
    // .h): WiFi.onEvent() corre en la task "arduino_events" del core, así que
    // el lambda SOLO delega en onWifiEvent_(), que a su vez solo escribe
    // flags `volatile` — nunca bus_.publish() (EventBus no es thread-safe).
    // La lambda captura `this` (permitido aquí: no es un handler de EventBus,
    // que sí exige function pointers planos sin captura).
    WiFi.onEvent(
        [this](arduino_event_id_t event, arduino_event_info_t) { onWifiEvent_(event); },
        ARDUINO_EVENT_WIFI_STA_GOT_IP
    );
    WiFi.onEvent(
        [this](arduino_event_id_t event, arduino_event_info_t) { onWifiEvent_(event); },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED
    );
#endif

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    state_              = WifiState::Connecting;
    connectStartedAtMs_ = millis();
    lastPollMs_         = millis();

    // Logueamos SSID (no sensible). Password nunca se loguea (rules/security.md).
    LOG_INFO("[wifi] conectando a SSID=%s (hostname=%s)", WIFI_SSID, OTA_HOSTNAME);
}

#if defined(ARDUINO_ARCH_ESP32)
// -----------------------------------------------------------------------------
// onWifiEvent_ — callback de WiFi.onEvent(), corre en la task "arduino_events".
//
// Contrato estricto: SOLO escribe flags `volatile`. Nada de bus_.publish(),
// nada de LOG_* (Logger no está documentado como thread-safe y su coste no
// está acotado — mismo criterio conservador que con el EventBus), nada de
// tocar state_/disconnectAtMs_ directamente (eso lo hace consumePendingEvents_,
// desde loopTask, en el próximo loop()).
// -----------------------------------------------------------------------------
void WifiManager::onWifiEvent_(arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        eventGotIpPending_ = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        eventDisconnectedPending_ = true;
    }
}
#endif

// -----------------------------------------------------------------------------
// consumePendingEvents_ — procesa los flags escritos por onWifiEvent_() desde
// la task "arduino_events". Llamado en CADA loop() (sin gate de tiempo) para
// no perder blips de reconexión más cortos que kWifiPollIntervalMs (ver
// comentario de clase en el .h — este es el fix del incidente real).
//
// Orden importa: se procesa primero el flag de desconexión y luego el de
// GOT_IP. Si ambos llegaron entre dos llamadas a loop() (blip muy corto), este
// orden garantiza que se pase por Disconnected (que apaga WebUI/OTA en
// onWifiStateChanged) antes de volver a Connected (que los relevanta) — así
// no se pierde el ciclo end()/begin() aunque el blip completo quepa entre dos
// iteraciones del loop principal.
// -----------------------------------------------------------------------------
void WifiManager::consumePendingEvents_() {
    if (eventDisconnectedPending_) {
        eventDisconnectedPending_ = false;
        if (state_ == WifiState::Connected) {
            LOG_WARN("[wifi] evento STA_DISCONNECTED (onEvent) — marcando Disconnected");
            disconnectAtMs_ = millis();
            publishState_(WifiState::Disconnected);
        }
    }

    if (eventGotIpPending_) {
        eventGotIpPending_ = false;
        if (state_ != WifiState::Connected) {
            LOG_INFO("[wifi] evento GOT_IP (onEvent) — marcando Connected");
            publishState_(WifiState::Connected);
        }
    }
}

// -----------------------------------------------------------------------------
// loop — consume eventos WiFi pendientes y polea el estado como respaldo.
//
// consumePendingEvents_() corre en CADA llamada (barato: 2 bools). El resto
// (polling clásico) mantiene su guard de tiempo: solo actúa cada
// kWifiPollIntervalMs (500 ms). En el 99% de las llamadas: un compare de
// uint32_t → < 1 µs.
// -----------------------------------------------------------------------------
void WifiManager::loop() {
    consumePendingEvents_();

    if (state_ == WifiState::Disabled) return;

    uint32_t now = millis();
    if (now - lastPollMs_ < kWifiPollIntervalMs) return;
    lastPollMs_ = now;

    wl_status_t st      = WiFi.status();
    WifiState   desired = state_;

    if (st == WL_CONNECTED) {
        // Conexión establecida (o reconexión automática exitosa).
        desired = WifiState::Connected;

    } else if (state_ == WifiState::Connecting) {
        if (now - connectStartedAtMs_ > kWifiConnectTimeoutMs) {
            // 15 s sin conectar → Disconnected, reintentará tras backoff.
            LOG_WARN("[wifi] timeout de conexion tras %u ms — reintentando en %u s",
                     kWifiConnectTimeoutMs, kWifiRetryBackoffMs / 1000u);
            desired       = WifiState::Disconnected;
            disconnectAtMs_ = now;
        }

    } else if (state_ == WifiState::Connected && st != WL_CONNECTED) {
        // Pérdida de conexión en runtime.
        LOG_WARN("[wifi] conexion perdida (status=%u) — reintentando en %u s",
                 static_cast<unsigned>(st), kWifiRetryBackoffMs / 1000u);
        desired         = WifiState::Disconnected;
        disconnectAtMs_ = now;

    } else if (state_ == WifiState::Disconnected) {
        if (now - disconnectAtMs_ > kWifiRetryBackoffMs) {
            // Backoff expirado → reintentar conexión.
            LOG_INFO("[wifi] reintentando conexion a SSID=%s", WIFI_SSID);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            desired             = WifiState::Connecting;
            connectStartedAtMs_ = now;
        }
    }

    if (desired != state_) {
        publishState_(desired);
    }
}

// -----------------------------------------------------------------------------
// rssi — RSSI en dBm. Devuelve 0 si no está conectado.
// -----------------------------------------------------------------------------
int8_t WifiManager::rssi() const {
    if (state_ != WifiState::Connected) return 0;
    return static_cast<int8_t>(WiFi.RSSI());
}

// -----------------------------------------------------------------------------
// publishState_ — actualiza state_ y publica el evento WifiStateChanged.
// El payload lleva el nuevo estado en los 8 bits bajos (mismo patrón que
// LifeStateChanged en Fase 2).
// -----------------------------------------------------------------------------
void WifiManager::publishState_(WifiState newState) {
    LOG_INFO("[wifi] estado %u -> %u", static_cast<unsigned>(state_),
             static_cast<unsigned>(newState));
    state_ = newState;

    // Sellar el instante de conexión exitosa aquí, en el único punto que
    // decide una transición A Connected (evento o polling) — evita duplicar
    // esta lógica en consumePendingEvents_() y en el polling de abajo.
    // Usado por el watchdog de conectividad (commit 3, WIFI_DEAD_RESTART_MS).
    if (newState == WifiState::Connected) {
        hasEverConnected_ = true;
        lastConnectedMs_  = millis();
    }

    // Sellar/limpiar el sello de "tiempo caído" en el MISMO punto único de
    // transición (evento o polling) — igual que lastConnectedMs_ arriba, pero
    // con la semántica distinta que necesita el watchdog: ver
    // ConnectivityWatchdogLogic.h para el porqué de separar esta marca de
    // lastConnectedMs_ (bug de campo: reinicio casi inmediato al perder WiFi
    // en vez de esperar WIFI_DEAD_RESTART_MS caído).
    wifiDownLogic_.onWifiStatus(newState == WifiState::Connected, millis());

    bus_.publish({EventType::WifiStateChanged, static_cast<int32_t>(newState)});
}

// -----------------------------------------------------------------------------
// isWifiDeadFor — decisión completa del watchdog de conectividad. Delega
// enteramente en ConnectivityWatchdogLogic::shouldRestart() (pura, cubierta
// por tests en test/test_connectivity_watchdog_logic/) para que
// connectivityWatchdog_() en main_esp32.cpp no reimplemente esta aritmética.
// -----------------------------------------------------------------------------
bool WifiManager::isWifiDeadFor(uint32_t thresholdMs) const {
    return wifiDownLogic_.shouldRestart(hasEverConnected_, millis(), thresholdMs);
}

#endif  // UNIT_TEST

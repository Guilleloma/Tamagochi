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
    // (Xiaomi, DTIM alto) tira los UDP unicast entrantes — la invitación de
    // ArduinoOTA (puerto 3232) se pierde casi siempre aunque HTTP/mDNS funcionen.
    // Coste: ~20-40 mA extra de media. Diagnosticado en el despliegue de Fase 5.
    WiFi.setSleep(false);
    WiFi.hostname(OTA_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    state_              = WifiState::Connecting;
    connectStartedAtMs_ = millis();
    lastPollMs_         = millis();

    // Logueamos SSID (no sensible). Password nunca se loguea (rules/security.md).
    LOG_INFO("[wifi] conectando a SSID=%s (hostname=%s)", WIFI_SSID, OTA_HOSTNAME);
}

// -----------------------------------------------------------------------------
// loop — polea el estado WiFi y gestiona transiciones + reintentos.
//
// Guard de tiempo: solo actúa cada kWifiPollIntervalMs (500 ms).
// En el 99% de las llamadas: un compare de uint32_t → < 1 µs.
// -----------------------------------------------------------------------------
void WifiManager::loop() {
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
    bus_.publish({EventType::WifiStateChanged, static_cast<int32_t>(newState)});
}

#endif  // UNIT_TEST

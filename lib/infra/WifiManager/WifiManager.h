#pragma once
// =============================================================================
// WifiManager — conexión WiFi STA no bloqueante.
//
// Ciclo de vida:
//   1. begin()  → configura modo STA y lanza WiFi.begin(). Devuelve inmediatamente.
//                 Si WIFI_SSID está vacío en secrets.h, marca Disabled y no conecta.
//   2. loop()   → llamar cada vuelta del loop principal. Polea estado cada
//                 kWifiPollIntervalMs. Publica WifiStateChanged cuando hay cambio.
//
// Garantías de timing:
//   - begin()  : < 5 ms (sin espera de red).
//   - loop()   : < 1 µs en el 99% de los casos (guard de tiempo).
//                En la comprobación real: WiFi.status() ≈ 5-50 µs.
//
// WiFi degradado: si la conexión falla o se cae en runtime, el firmware sigue
// funcionando offline. Ver plan Fase 3 §1.3.
//
// Dependencias externas: ESP8266WiFi.h (Arduino-only). No testeable host-native.
// =============================================================================

#include <stdint.h>

class EventBus;

enum class WifiState : uint8_t {
    Disabled     = 0,   // SSID vacío en secrets.h — no intenta conectar
    Connecting   = 1,   // WiFi.begin() lanzado, esperando asignación IP
    Connected    = 2,   // WL_CONNECTED — IP asignada
    Disconnected = 3    // hubo o intentó conexión, ahora sin red; reintenta con backoff
};

class WifiManager {
public:
    explicit WifiManager(EventBus& bus);

    // Inicia la conexión WiFi. No bloqueante — devuelve en < 5 ms.
    // Si WIFI_SSID está vacío, marca Disabled y retorna sin conectar.
    void begin();

    // Polea el estado de la conexión. Llamar en cada iteración del loop principal.
    // Guard interno: actúa solo cada kWifiPollIntervalMs.
    void loop();

    WifiState state() const { return state_; }

    // RSSI en dBm. Devuelve 0 si no está conectado.
    int8_t rssi() const;

private:
    EventBus& bus_;
    WifiState state_              = WifiState::Disabled;
    uint32_t  connectStartedAtMs_ = 0;
    uint32_t  disconnectAtMs_     = 0;
    uint32_t  lastPollMs_         = 0;

    // Intervalo entre comprobaciones de estado WiFi (ms).
    static constexpr uint32_t kWifiPollIntervalMs   = 500;
    // Tiempo máximo esperando conexión antes de marcar Disconnected (ms).
    static constexpr uint32_t kWifiConnectTimeoutMs = 15000;
    // Backoff entre reintentos de reconexión (ms).
    static constexpr uint32_t kWifiRetryBackoffMs   = 60000;

    void publishState_(WifiState newState);
};

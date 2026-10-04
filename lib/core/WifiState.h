#pragma once
#include <stdint.h>

// =============================================================================
// WifiState — estado de la conexión WiFi.
//
// Extraído en header propio para que módulos que solo necesitan el enum
// (p.ej. DisplayRendererColor) puedan incluirlo sin arrastrar WifiManager.h,
// que en el env esp12e incluye <ESP8266WiFi.h> (no disponible en ESP32).
//
// WifiManager.h incluye este header en lugar de redeclarar el enum.
// DisplayRendererColor.h incluye directamente WifiState.h.
//
// Sin dependencias de plataforma: compatible con ESP8266, ESP32 y host-native.
// =============================================================================

enum class WifiState : uint8_t {
    Disabled     = 0,   // SSID vacío en secrets.h — no intenta conectar
    Connecting   = 1,   // WiFi.begin() lanzado, esperando asignación IP
    Connected    = 2,   // WL_CONNECTED — IP asignada
    Disconnected = 3    // hubo o intentó conexión, ahora sin red; reintenta con backoff
};

// =============================================================================
// WifiInfo — instantánea del estado de red para la UI (página de red de Stats).
//
// La produce WifiManager::info() (capa infra, la única que habla con WiFi.*)
// y la consume DisplayRendererColor::setWifiInfo() vía main_esp32.cpp, que
// formatea los textos con NetInfoLogic (lib/ui). Así el renderer nunca llama
// a WiFi.* directamente.
//
// SEGURIDAD: solo datos visibles en la LAN. NUNCA añadir aquí contraseñas
// (WIFI_PASSWORD, OTA_PASSWORD, WEB_AUTH_PASSWORD) — rules/security.md.
//
// ssid/hostname apuntan a literales de compile-time (WIFI_SSID / OTA_HOSTNAME
// de secrets.h) — almacenamiento estático, nunca nullptr. El consumidor no
// debe asumir otra cosa que "válido durante la llamada": copia lo que necesite.
// =============================================================================
struct WifiInfo {
    WifiState   state          = WifiState::Disabled;
    bool        ssidConfigured = false;  // false => WIFI_SSID vacío en secrets.h
    const char* ssid           = "";     // SSID configurado (nunca la password)
    const char* hostname       = "";     // hostname mDNS SIN ".local"
    uint8_t     ip[4]          = {0, 0, 0, 0};  // 0.0.0.0 si no está Connected
    int8_t      rssiDbm        = 0;      // 0 si no está Connected
};

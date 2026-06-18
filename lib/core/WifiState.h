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
